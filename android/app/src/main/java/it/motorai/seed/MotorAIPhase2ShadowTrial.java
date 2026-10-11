package it.motorai.seed;

import android.content.Context;
import org.json.JSONObject;
import java.io.File;
import java.io.FileInputStream;
import java.security.MessageDigest;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicBoolean;

/** R3R10 copy-only training: original 10/10 never replaced or modified. */
public final class MotorAIPhase2ShadowTrial {
    private static final AtomicBoolean BUSY=new AtomicBoolean(false);
    private MotorAIPhase2ShadowTrial() {}
    private static String hash(File f)throws Exception {
        if(!f.isFile()||f.length()<1||f.length()>32*1024*1024)
            throw new Exception("Checkpoint non valido: "+f.getName());
        MessageDigest m=MessageDigest.getInstance("SHA-256");
        try(FileInputStream in=new FileInputStream(f)){
            byte[] a=new byte[65536];int n;
            while((n=in.read(a))!=-1)m.update(a,0,n);
        }
        StringBuilder b=new StringBuilder();
        for(byte x:m.digest())b.append(String.format(Locale.ROOT,"%02x",x&255));
        return b.toString();
    }
    private static String fingerprint(File dir)throws Exception{
        return hash(new File(dir,"weights.bin"))+":"+hash(new File(dir,"checkpoint.json"));
    }
    private static String allProtected(Context c)throws Exception {
        File base=new File(c.getFilesDir(),"motorai/checkpoints");
        StringBuilder b=new StringBuilder();
        for(String name:new String[]{"current","previous","autotrain-baseline",
                "goal10-original-r3r8","goal10-current-transit-r3r8"}){
            File f=new File(base,name);
            b.append(name).append(":").append(f.exists()?fingerprint(f):"absent").append(";");
        }
        return b.toString();
    }
    private static double confidence(JSONObject o){
        return o.optDouble("confidence",0.0);
    }
    private static boolean tenChecks(JSONObject p)throws Exception{
        JSONObject a=p.getJSONObject("comprensione");
        JSONObject b=p.getJSONObject("risposta");
        JSONObject c=p.getJSONObject("memoria");
        JSONObject d=p.getJSONObject("ragionamento");
        JSONObject e=p.getJSONObject("incertezza");
        JSONObject f=p.getJSONObject("ricerca_wiki");
        JSONObject g=p.getJSONObject("ricerca_live");
        JSONObject h=p.getJSONObject("strumenti_calc");
        JSONObject i=p.getJSONObject("strumenti_search");
        JSONObject j=p.getJSONObject("piano");
        JSONObject k=p.getJSONObject("autocontrollo_calcolo");
        JSONObject l=p.getJSONObject("autocontrollo_fonte");
        return "calcolo".equals(a.optString("intent"))&&confidence(a)>=.50
            &&!b.optString("reply","").trim().isEmpty()
            &&"recall".equals(c.optString("action"))&&"name".equals(c.optString("slot"))&&confidence(c)>=.50
            &&d.optBoolean("valid",false)&&"add_sub".equals(d.optString("plan"))&&d.optLong("result",-1)==9
            &&"verify".equals(e.optString("decision"))&&confidence(e)>=.55
            &&"wikipedia".equals(f.optString("source"))&&!f.optString("query","").trim().isEmpty()
            &&"live".equals(g.optString("source"))
            &&"calculator".equals(h.optString("tool"))&&"search".equals(i.optString("tool"))
            &&"sequence".equals(j.optString("mode"))&&!j.optString("first","").isEmpty()
            &&!j.optString("second","").isEmpty()
            &&"calculation".equals(k.optString("check"))&&confidence(k)>=.50
            &&"source".equals(l.optString("check"))&&confidence(l)>=.50;
    }
    private static boolean otherCapabilitiesUnchanged(JSONObject a,JSONObject b)throws Exception{
        for(String key:new String[]{"risposta","memoria","ragionamento","incertezza",
             "ricerca_wiki","ricerca_live","piano","autocontrollo_calcolo","autocontrollo_fonte"}){
            if(!a.getJSONObject(key).toString().equals(b.getJSONObject(key).toString()))return false;
        }
        return true;
    }
    private static String percentage(JSONObject o,String k){
        return String.format(Locale.ITALY,"%.1f%%",o.optDouble(k,0)*100);
    }
    public static String run(Context c){
        if(!BUSY.compareAndSet(false,true))
            return "⏳ Prova isolata già in esecuzione, attendere.";
        try{
            if(!MotorAIRecoveryMode.paused(c))
                return "⛔ Per R3R10 mantenere la pausa SOS attiva, non riavviare l'allenamento.";
            if(MiniAiGoals.completedCount(c)!=10)
                return "⛔ Tutti i dieci Goal devono restare completati prima della prova.";
            synchronized(MotorAICheckpointStore.recoveryMutex()){
                File current=new File(c.getFilesDir(),"motorai/checkpoints/current");
                File candidate=new File(c.getFilesDir(),"motorai/phase2-r3r10-shadow-candidate");
                if(candidate.exists())
                    return "⛔ Copia R3R10 già presente: non sovrascrivo né riaddestro. Invia la diagnosi.";
                String before=allProtected(c);
                String nativeBefore=MainActivity.nativeStatus();
                JSONObject trial=new JSONObject(MainActivity.nativePhase2ShadowExperiment(
                    current.getAbsolutePath(),candidate.getAbsolutePath()));
                String after=allProtected(c);
                String nativeAfter=MainActivity.nativeStatus();
                if(!before.equals(after)||!nativeBefore.equals(nativeAfter)
                    ||!MotorAIRecoveryMode.paused(c)||MiniAiGoals.completedCount(c)!=10)
                    return "🛑 Controllo sicurezza non superato: CURRENT/PREVIOUS o stato attivo cambiati. NON promuovere.";
                StringBuilder out=new StringBuilder("🔬 R3R10 — ALLENAMENTO SOLTANTO IN COPIA ISOLATA");
                out.append("\nStato: ").append(trial.optString("status","assente"));
                out.append("\nVecchie sei prove Goal1/7: ")
                   .append(trial.optInt("r3r9_target_before",0)).append("/6 → ")
                   .append(trial.optInt("r3r9_target_after",0)).append("/6");
                out.append("\nSedici prove alternative non allenate: ")
                   .append(trial.optInt("blind_before",0)).append("/16 → ")
                   .append(trial.optInt("blind_after",0)).append("/16");
                out.append("\nGoal1 validation: ").append(percentage(trial,"goal1_validation_before"))
                   .append(" → ").append(percentage(trial,"goal1_validation_after"));
                out.append("\nGoal1 TEST: ").append(percentage(trial,"goal1_test_before"))
                   .append(" → ").append(percentage(trial,"goal1_test_after"));
                out.append("\nGoal7 validation: ").append(percentage(trial,"goal7_validation_before"))
                   .append(" → ").append(percentage(trial,"goal7_validation_after"));
                out.append("\nGoal7 TEST: ").append(percentage(trial,"goal7_test_before"))
                   .append(" → ").append(percentage(trial,"goal7_test_after"));
                out.append("\nPassi soltanto sulla copia, Goal1/Goal7: ")
                   .append(trial.optInt("goal1_steps",0)).append("/")
                   .append(trial.optInt("goal7_steps",0));
                out.append("\nArresto: ").append(trial.optString("stop","n/d"));
                boolean ready=trial.optBoolean("candidate_saved",false)
                    &&trial.optBoolean("heldout_non_degradation",false)
                    &&trial.optBoolean("live_unchanged",false);
                if(ready){
                    fingerprint(candidate);
                    JSONObject probe=new JSONObject(MainActivity.nativeProbeCheckpoint(candidate.getAbsolutePath()));
                    JSONObject compared=new JSONObject(MainActivity.nativeGoal10CandidateCompare(candidate.getAbsolutePath()));
                    ready=probe.optBoolean("loaded",false)
                        &&compared.optBoolean("loaded",false)
                        &&compared.optBoolean("live_unchanged",false)
                        &&tenChecks(compared.getJSONObject("active"))
                        &&tenChecks(compared.getJSONObject("candidate"))
                        &&otherCapabilitiesUnchanged(compared.getJSONObject("active"),
                            compared.getJSONObject("candidate"));
                }
                if(!before.equals(allProtected(c))||!nativeBefore.equals(MainActivity.nativeStatus()))
                    return "🛑 Integrità del motore originale non confermata. NON promuovere.";
                if(ready)out.append("\n✅ CANDIDATA R3R10 VERIFICATA IN ISOLAMENTO.");
                else out.append("\n⛔ Miglioramento non certificato: NON promuovere la candidata.");
                out.append("\nL'originale rimane 10/10 e 100%. Nessun peso principale o Goal modificato.");
                out.append("\nLa prova su 16 domande misura trasferimento limitato, non intelligenza generale.");
                out.append("\nPausa SOS e backup invariati.");
                return out.toString();
            }
        }catch(Throwable t){
            return "⛔ R3R10 fermata: "+t.getClass().getSimpleName()
                +". Invia la diagnosi; non modificare i checkpoint.";
        }finally{BUSY.set(false);}
    }
}
