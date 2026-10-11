package it.motorai.seed;

import android.content.Context;
import android.content.SharedPreferences;
import org.json.JSONObject;
import java.util.List;

/** Constrained targeted practice for an independently failed Goal10 integration test.
 * It never declares acceptance: MiniAiAssistantCycle must still pass all ten tests.
 */
public final class MotorAIGoal10Recovery {
    private static final String PREFS = "motorai_goal10_recovery";
    private static final int MAX_CHUNKS_PER_CAPABILITY = 160;
    private MotorAIGoal10Recovery() {}

    public static final class Step {
        public final boolean continued;
        public final String message;
        Step(boolean ok,String text) {continued=ok; message=text;}
    }

    private static int capability(String failed) {
        if ("comprensione".equals(failed)) return 1;
        if ("risposta".equals(failed)) return 2;
        if ("memoria".equals(failed)) return 3;
        if ("ragionamento".equals(failed)) return 4;
        if ("incertezza".equals(failed)) return 5;
        if ("ricerca".equals(failed)) return 6;
        if ("strumenti".equals(failed)) return 7;
        if ("piano".equals(failed)) return 8;
        if (failed.startsWith("autocontrollo-")) return 9;
        return 0;
    }

    private static double test(int goal) throws Exception {
        String j;
        switch(goal) {
            case 1: j=MainActivity.nativeGoal1FinalTest(); break;
            case 2: j=MainActivity.nativeGoal2FinalTest(); break;
            case 3: j=MainActivity.nativeGoal3FinalTest(); break;
            case 4: j=MainActivity.nativeGoal4FinalTest(); break;
            case 5: j=MainActivity.nativeGoal5FinalTest(); break;
            case 6: j=MainActivity.nativeGoal6FinalTest(); break;
            case 7: j=MainActivity.nativeGoal7FinalTest(); break;
            case 8: j=MainActivity.nativeGoal8FinalTest(); break;
            case 9: j=MainActivity.nativeGoal9FinalTest(); break;
            default: return -1.0;
        }
        double score=new JSONObject(j).optDouble("test_accuracy", -1.0);
        return Double.isFinite(score) ? score : -1.0;
    }

    private static void train(int goal) {
        switch(goal) {
            case 1: MainActivity.nativeGoal1TrainChunk(20); break;
            case 2: MainActivity.nativeGoal2TrainChunk(20); break;
            case 3: MainActivity.nativeGoal3TrainChunk(20,0.02f); break;
            case 4: MainActivity.nativeGoal4TrainChunk(20); break;
            case 5: MainActivity.nativeGoal5TrainChunk(20); break;
            case 6: MainActivity.nativeGoal6TrainChunk(20); break;
            case 7: MainActivity.nativeGoal7TrainChunk(20); break;
            case 8: MainActivity.nativeGoal8TrainChunk(20); break;
            case 9: MainActivity.nativeGoal9TrainChunk(20); break;
            default: throw new IllegalArgumentException("Capability not supported");
        }
    }

    public static Step oneChunk(Context context, MiniAiAssistantCycle.Result before) {
        if (before.percent >= 100) return new Step(false,"Goal 10 gia superato.");
        SharedPreferences state=context.getSharedPreferences(PREFS,Context.MODE_PRIVATE);
        int target=0;
        String failure="";
        for (String candidate:before.failed) {
            int goal=capability(candidate);
            if(goal > 0 && state.getInt("attempts_"+goal,0) < MAX_CHUNKS_PER_CAPABILITY) {
                target=goal; failure=candidate; break;
            }
        }
        if (target==0) return new Step(false,
                "Tentativi mirati conclusi: verificare prove non superate nella Diagnostica.");
        final int goal=target;
        try {
            double[] retained=new double[10];
            for(int i=1;i<=9;i++) retained[i]=test(i);
            if (!MotorAICheckpointStore.rotateAndSave(context))
                return new Step(false,"Checkpoint di protezione non disponibile: allenamento fermato.");
            train(goal);
            MiniAiAssistantCycle.Result after=MiniAiAssistantCycle.evaluate(context);
            boolean retainedOk=true;
            for(int i=1;i<=9;i++) {
                double now=test(i);
                if(now<0 || now+0.0001<retained[i]-0.02) retainedOk=false;
            }
            if(after.passed<before.passed || !retainedOk) {
                boolean restored=MotorAICheckpointStore.loadCurrent(context);
                state.edit().putInt("attempts_"+goal,MAX_CHUNKS_PER_CAPABILITY).apply();
                return new Step(false,restored
                        ? "Regressione nella verifica integrata: rollback, capacita "+goal+" sospesa."
                        : "Problema di ripristino checkpoint: interrompere e aprire Diagnostica.");
            }
            if (!MotorAICheckpointStore.rotateAndSave(context)) {
                MotorAICheckpointStore.loadCurrent(context);
                return new Step(false,"Salvataggio di sicurezza fallito, allenamento sospeso.");
            }
            int attempts=state.getInt("attempts_"+goal,0)+1;
            state.edit().putInt("attempts_"+goal,attempts)
                    .putString("last_repair",failure+" · blocco "+attempts
                            +" · prove "+after.passed+"/"+after.total).apply();
            return new Step(true,"Recupero "+failure+" · "+after.passed+"/"
                    +after.total+" prove, blocco "+attempts);
        } catch(Exception e) {
            MotorAICheckpointStore.loadCurrent(context);
            return new Step(false,"Recupero interrotto, checkpoint conservato: "
                    +e.getClass().getSimpleName());
        }
    }

    public static void clear(Context context) {
        context.getSharedPreferences(PREFS,Context.MODE_PRIVATE).edit().clear().apply();
    }
}
