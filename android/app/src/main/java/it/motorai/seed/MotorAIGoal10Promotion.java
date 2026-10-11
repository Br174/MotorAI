package it.motorai.seed;

import android.content.Context;
import android.content.SharedPreferences;
import org.json.JSONObject;
import java.io.File;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.io.FileInputStream;
import java.util.Locale;

/**
 * R3R8: guarded Goal10 candidate promotion. No learning, no deletion of
 * CURRENT/PREVIOUS, no source candidate overwrite, and fail-closed on ambiguity.
 * The promotion's original checkpoint is a separate permanent rollback slot.
 */
public final class MotorAIGoal10Promotion {
    private static final String PREFS = "motorai_goal10_promotion";
    private static final String BASE = "motorai/checkpoints";
    private static final String ORIGINAL = "goal10-original-r3r8";
    private static final String STAGE = "goal10-stage-r3r8";
    private static final String INTERRUPTED = "goal10-interrupted-r3r8";

    private MotorAIGoal10Promotion() {}

    private static File folder(Context c, String name) {
        return new File(new File(c.getFilesDir(), BASE), name);
    }
    private static boolean complete(File dir) {
        return dir.isDirectory() && new File(dir, "weights.bin").isFile()
            && new File(dir, "weights.bin").length() > 0
            && new File(dir, "checkpoint.json").isFile()
            && new File(dir, "checkpoint.json").length() > 0;
    }
    private static String digest(File file) throws Exception {
        MessageDigest hash=MessageDigest.getInstance("SHA-256");
        byte[] bytes=new byte[65536];
        try(FileInputStream input=new FileInputStream(file)){
            int n; while((n=input.read(bytes))!=-1) hash.update(bytes,0,n);
        }
        StringBuilder out=new StringBuilder();
        for(byte b:hash.digest()) out.append(String.format(Locale.ROOT,"%02x",b&255));
        return out.toString();
    }
    private static String fingerprint(File folder) throws Exception {
        if(!complete(folder)) throw new Exception("Checkpoint incompleto: " + folder.getName());
        return digest(new File(folder,"weights.bin"))+":"+digest(new File(folder,"checkpoint.json"));
    }
    private static boolean copyCheckpoint(File from, File to) throws Exception {
        if(to.exists() || !complete(from) || !to.mkdirs()) return false;
        for(String name:new String[]{"weights.bin","checkpoint.json"}) {
            Files.copy(new File(from,name).toPath(),new File(to,name).toPath(),
                StandardCopyOption.COPY_ATTRIBUTES);
        }
        return complete(to) && fingerprint(from).equals(fingerprint(to));
    }
    private static boolean sameFoundation(JSONObject before, JSONObject after) {
        if(before.optInt("step",-1)!=after.optInt("step",-2)
           ||before.optInt("curriculum",-1)!=after.optInt("curriculum",-2)) return false;
        for(String key:new String[]{"val_accuracy","test_accuracy",
                "retention_l0_accuracy","retention_l1_accuracy",
                "retention_l2_accuracy","retention_l3_accuracy","retention_l4_accuracy"}) {
            double a=before.optDouble(key,Double.NaN),b=after.optDouble(key,Double.NaN);
            if(!Double.isFinite(a)||!Double.isFinite(b)
               ||b+0.00001<a||b<0.90) return false;
        }
        return true;
    }
    private static void record(Context c, String state, String message) {
        c.getSharedPreferences(PREFS,Context.MODE_PRIVATE).edit()
           .putString("state",state).putString("message",message)
           .putLong("updated_ms",System.currentTimeMillis()).commit();
    }
    public static String last(Context c) {
        SharedPreferences p=c.getSharedPreferences(PREFS,Context.MODE_PRIVATE);
        return p.getString("message","Candidata non ancora promossa.");
    }

    /** Crash recovery runs BEFORE normal native checkpoint resume in MainActivity. */
    public static void recoverInterrupted(Context c) {
        synchronized(MotorAICheckpointStore.recoveryMutex()) {
            try {
                File original=folder(c,ORIGINAL),current=folder(c,"current");
                if(!original.exists() || MiniAiGoals.percent(c,9)>=100) return;
                // A valid original from a previous attempt is never silently deleted.
                if(!complete(original)) {
                    MotorAIRecoveryMode.pause(c);
                    record(c,"STOP","STOP: backup originale R3R8 incompleto; nessun dato modificato.");
                    return;
                }
                MotorAIRecoveryMode.pause(c);
                if(current.exists()) {
                    if(!complete(current)) {
                        record(c,"STOP","STOP: CURRENT incompleto. Ripristino automatico non sicuro.");
                        return;
                    }
                    if(fingerprint(current).equals(fingerprint(original))) return;
                    File interrupted=folder(c,INTERRUPTED);
                    if(interrupted.exists() || !current.renameTo(interrupted)) {
                        record(c,"STOP","STOP: impossibile isolare il checkpoint interrotto.");
                        return;
                    }
                }
                if(!current.exists() && original.renameTo(current)) {
                    record(c,"RECOVERED","⚡ Promozione incompleta recuperata: CURRENT originale conservato, Goal10 non certificato.");
                } else {
                    record(c,"STOP","STOP: ripristino CURRENT non completato; non allenare o ripristinare.");
                }
            } catch(Throwable t) {
                MotorAIRecoveryMode.pause(c);
                record(c,"STOP","STOP recupero R3R8: "+t.getClass().getSimpleName());
            }
        }
    }

    /** Called exclusively from a deliberate diagnostics button, never from background jobs. */
    public static String promote(Context c) {
        synchronized(MotorAICheckpointStore.recoveryMutex()) {
            if(!MotorAIRecoveryMode.paused(c))
                return "⛔ Attivare prima pausa SOS: nessun peso applicato.";
            if(!MotorAIGoal10DiagnosticHold.shouldHold(c)
               ||MiniAiGoals.completedCount(c)!=9)
                return "⛔ Prerequisiti Goal10 cambiati: nessun peso applicato.";
            File current=folder(c,"current"),previous=folder(c,"previous"),
                 original=folder(c,ORIGINAL),stage=folder(c,STAGE),
                 interrupted=folder(c,INTERRUPTED);
            File candidate=new File(c.getFilesDir(),"motorai/goal10-shadow-candidate-r3r5");
            boolean loadedCandidate=false, movedCurrent=false, swapped=false;
            try {
                if(original.exists()||stage.exists()||interrupted.exists())
                    return "⛔ Slot R3R8 già presente: controllare il registro prima di riprovare.";
                final String currentHash=fingerprint(current),candidateHash=fingerprint(candidate);
                final String previousHash=previous.exists()?fingerprint(previous):"none";
                String independent=MiniAiGoal10CandidateInspector.inspect(c);
                if(!independent.contains("✅ CANDIDATA VERIFICATA IN ISOLAMENTO"))
                    return "⛔ La candidata non supera più i test indipendenti. Nessun peso applicato.\n"+independent;
                if(!currentHash.equals(fingerprint(current))
                    ||!candidateHash.equals(fingerprint(candidate)))
                    return "⛔ Impronte cambiate prima della promozione. Nessun peso applicato.";
                JSONObject foundationBefore=new JSONObject(MainActivity.nativeEvaluate());
                if(!copyCheckpoint(current,original))
                    return "⛔ Backup dell'originale non completato. Nessun peso applicato.";
                if(!MainActivity.nativeLoadCheckpoint(candidate.getAbsolutePath()))
                    throw new Exception("candidate_load_failed");
                loadedCandidate=true;
                JSONObject foundationAfter=new JSONObject(MainActivity.nativeEvaluate());
                if(!sameFoundation(foundationBefore,foundationAfter))
                    throw new Exception("foundation_regression");
                MiniAiAssistantCycle.Result test=MiniAiAssistantCycle.evaluate(c);
                if(test.passed!=10 || test.total!=10 || test.percent!=100)
                    throw new Exception("live_goal10_not_10_of_10");
                if(!MainActivity.nativeSaveCheckpoint(stage.getAbsolutePath())||!complete(stage))
                    throw new Exception("stage_checkpoint_save_failed");
                JSONObject probe=new JSONObject(MainActivity.nativeProbeCheckpoint(stage.getAbsolutePath()));
                if(!probe.optBoolean("loaded",false))
                    throw new Exception("stage_checkpoint_unreadable");
                if(!candidateHash.equals(fingerprint(candidate))
                  ||!currentHash.equals(fingerprint(current))
                  ||(previous.exists()&&!previousHash.equals(fingerprint(previous))))
                    throw new Exception("checkpoints_changed");
                if(!current.renameTo(folder(c,"goal10-current-transit-r3r8")))
                    throw new Exception("current_transit_failed");
                movedCurrent=true;
                File transit=folder(c,"goal10-current-transit-r3r8");
                // Keep the byte-identical immutable backup even after success.
                if(!stage.renameTo(current)) throw new Exception("stage_commit_failed");
                swapped=true;
                if(!complete(current)
                   ||!new JSONObject(MainActivity.nativeProbeCheckpoint(current.getAbsolutePath()))
                     .optBoolean("loaded",false))
                    throw new Exception("promoted_checkpoint_unreadable");
                if(!sameFoundation(foundationBefore,new JSONObject(MainActivity.nativeEvaluate())))
                    throw new Exception("post_commit_foundation_regression");
                MiniAiAssistantCycle.Result finalTest=MiniAiAssistantCycle.evaluate(c);
                if(finalTest.passed!=10 || finalTest.total!=10)
                    throw new Exception("post_commit_goal10_failed");
                if(!transit.isDirectory() || !fingerprint(transit).equals(currentHash))
                    throw new Exception("original_transit_changed");
                // Goal10 acceptance is written LAST, only after the native live 10/10.
                MiniAiGoals.updateGoal(c,9,100,
                    "CERTIFICAZIONE REALE 10/10 R3R8 · CURRENT promosso con backup separato");
                record(c,"ACCEPTED","✅ Goal10 10/10 sul modello attivo. CURRENT aggiornato; original e PREVIOUS conservati. Pausa SOS ancora attiva.");
                return last(c)+"\nBackup R3R8: "+original.getName()+" e "+transit.getName();
            } catch(Throwable t) {
                // Never claim a rollback was successful unless the native load and
                // the on-disk CURRENT both resolve to the original fingerprint.
                boolean filesystemOk=true,engineOk=true;
                try {
                    File transit=folder(c,"goal10-current-transit-r3r8");
                    if(swapped && current.exists()) {
                        if(!current.renameTo(interrupted)) filesystemOk=false;
                    }
                    if(movedCurrent && !current.exists()) {
                        if(!transit.renameTo(current)) filesystemOk=false;
                    }
                    if(loadedCandidate) {
                        File back=complete(current)?current:original;
                        engineOk=MainActivity.nativeLoadCheckpoint(back.getAbsolutePath());
                    }
                } catch(Throwable failure) {filesystemOk=false;engineOk=false;}
                MotorAIRecoveryMode.pause(c);
                String msg="⛔ Promozione non accettata ("+t.getMessage()+"). "
                    +(filesystemOk&&engineOk?"Checkpoint originale ripristinato.":"STOP: recupero da verificare in diagnostica.")
                    +" Goal10 resta non certificato; pausa SOS invariata.";
                record(c,"REJECTED",msg);
                return msg;
            }
        }
    }
}
