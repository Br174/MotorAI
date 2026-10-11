package it.motorai.seed;

import android.content.Context;
import org.json.JSONObject;
import java.io.File;
import java.io.FileInputStream;
import java.security.MessageDigest;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicBoolean;

/** Preview-only Goal1 recovery; no training or promotion in the live native Engine. */
public final class MotorAIGoal10ShadowTrial {
    private static final AtomicBoolean BUSY=new AtomicBoolean(false);
    private MotorAIGoal10ShadowTrial() {}
    private static String sha(File file)throws Exception {
        if(!file.isFile() || file.length()==0 || file.length()>32*1024*1024)
            throw new Exception("File checkpoint mancante o fuori limite: "+file.getName());
        MessageDigest md=MessageDigest.getInstance("SHA-256");
        byte[] b=new byte[65536];
        try(FileInputStream stream=new FileInputStream(file)){
            int n;
            while((n=stream.read(b))!=-1)md.update(b,0,n);
        }
        StringBuilder out=new StringBuilder();
        for(byte x:md.digest())out.append(String.format(Locale.ROOT,"%02x",x&255));
        return out.toString();
    }
    private static String fingerprint(File folder)throws Exception{
        return sha(new File(folder,"weights.bin"))+":"+sha(new File(folder,"checkpoint.json"));
    }
    private static String pct(double v) {
        return String.format(Locale.ITALY,"%.2f%%",v*100.0);
    }
    public static String preview(Context c)throws Exception{
        if(!BUSY.compareAndSet(false,true))
            return "È già in corso una prova: attendere.";
        try {
            if(!MotorAIGoal10DiagnosticHold.shouldHold(c))
                return "Il Goal 10 non risulta in pausa diagnostica: nessun allenamento eseguito.";
            if(MotorAIRecoveryMode.paused(c))
                return "Pausa SOS attiva: nessun allenamento eseguito.";
            synchronized(MotorAICheckpointStore.recoveryMutex()){
                File root=new File(c.getFilesDir(),"motorai/checkpoints");
                File current=new File(root,"current");
                File candidate=new File(c.getFilesDir(),"motorai/goal10-shadow-candidate-r3r5");
                if(new File(candidate,"weights.bin").exists())
                    return "Esiste già una candidata temporanea: nessun file sovrascritto. "+
                           "Non ripristinare: inviare la diagnosi per la verifica.";
                String before=fingerprint(current);
                JSONObject trial=new JSONObject(MainActivity.nativeGoal1ShadowTrial(
                   current.getAbsolutePath(),candidate.getAbsolutePath()));
                String after=fingerprint(current);
                if(!before.equals(after))
                    throw new Exception("ATTENZIONE: impronta CURRENT variata durante il controllo.");
                String status=trial.optString("status","unknown");
                if("source_out_of_sync".equals(status))
                    return "Il checkpoint CURRENT non coincide con la rete attiva. "+
                           "Nessun allenamento eseguito. Eseguire un backup di sicurezza "+
                           "prima di qualunque altro tentativo.";
                if(!"true".equals(String.valueOf(trial.optBoolean("live_unchanged",false))))
                    throw new Exception("Cervello attivo non dimostrato invariato. Prova sospesa.");
                boolean ready=trial.optBoolean("candidate_ready",false);
                if(ready){
                    fingerprint(candidate);
                    JSONObject verification=new JSONObject(
                        MainActivity.nativeProbeCheckpoint(candidate.getAbsolutePath()));
                    if(!verification.optBoolean("loaded",false))
                        throw new Exception("Checkpoint candidato non leggibile: NON promuovere.");
                }
                StringBuilder msg=new StringBuilder();
                msg.append(ready?"✅ CANDIDATA TEMPORANEA DISPONIBILE":"⏸ MIGLIORAMENTO INSUFFICIENTE");
                msg.append("\nStato: ").append(trial.optString("status","unknown"));
                msg.append("\nSicurezza iniziale: ").append(pct(trial.optDouble("confidence_before",0)));
                msg.append("\nSicurezza candidata: ").append(pct(trial.optDouble("confidence_after",0)));
                msg.append("\nTEST Goal1 originale: ").append(pct(trial.optDouble("test_before",0)));
                msg.append("\nTEST Goal1 candidato: ").append(pct(trial.optDouble("test_after",0)));
                msg.append("\nValidation originale: ").append(pct(trial.optDouble("validation_before",0)));
                msg.append("\nValidation candidata: ").append(pct(trial.optDouble("validation_after",0)));
                msg.append("\nPassi allenati SOLO in memoria temporanea: ")
                   .append(trial.optInt("shadow_train_steps",0));
                msg.append("\nSHA-256 CURRENT invariato.");
                msg.append("\nNessun peso attivo modificato; nessun Goal certificato.");
                if(ready)
                    msg.append("\nLa candidata è conservata separatamente, NON applicata.");
                return msg.toString();
            }
        }finally {BUSY.set(false);}
    }
}
