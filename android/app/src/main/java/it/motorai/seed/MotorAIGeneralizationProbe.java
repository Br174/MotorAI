package it.motorai.seed;

import android.content.Context;
import org.json.JSONObject;
import java.io.File;
import java.io.FileInputStream;
import java.security.MessageDigest;
import java.util.Locale;

/**
 * Seed 021R3R9. Twenty fixed pre-registered alternative Italian prompts.
 * No training, checkpoint writes, preference writes or external service calls.
 * Classification of a memory *intent* is NOT proof of remembering its contents.
 * All "unseen" claims mean not verbatim present in the bundled native dataset.
 */
public final class MotorAIGeneralizationProbe {
    private MotorAIGeneralizationProbe() {}
    private static final class Case {
        final String family, request, answer, second;
        final long numeric;
        Case(String family,String request,String answer,String second,long numeric){
            this.family=family;this.request=request;this.answer=answer;
            this.second=second;this.numeric=numeric;
        }
    }
    private static final Case[] CASES={
        new Case("intent","mi aiuti a sommare tredici e sei","calcolo","",0),
        new Case("intent","un saluto a te motore","saluto","",0),
        new Case("intent","per favore trova un articolo sulla botanica","ricerca","",0),
        new Case("intent","puoi aprire il lettore musicale","azione","",0),
        new Case("memory","il nome da ricordare e elena","store","name",0),
        new Case("memory","quale nome ti avevo comunicato","recall","name",0),
        new Case("memory","segnati che abito a lecce","store","city",0),
        new Case("memory","sapresti ricordare in quale citta risiedo","recall","city",0),
        new Case("reason","inizio con 11 ricevo 5 poi spendo 2","add_sub","",14),
        new Case("reason","parto da 13 tolgo 4 poi aggiungo 6","sub_add","",15),
        new Case("reason","ho 5 moltiplico per 4 poi perdo 3","mul_sub","",17),
        new Case("reason","parto da 3 aggiungo 6 poi moltiplico tutto per 2","add_mul","",18),
        new Case("uncertainty","saluti motorai","local_known","",0),
        new Case("uncertainty","ricordati la citta che ti ho indicato","local_known","",0),
        new Case("uncertainty","quale e il prezzo attuale del rame","verify","",0),
        new Case("uncertainty","che temperatura fa ora a palermo","verify","",0),
        new Case("source","cerca la biografia di ada lovelace","wikipedia","",0),
        new Case("source","quanto costa oggi un biglietto per il treno","live","",0),
        new Case("tool","somma ventidue e trenta con la calcolatrice","calculator","",0),
        new Case("tool","trova online notizie sulle comete","search","",0)
    };

    public static int count(){ return CASES.length; }

    private static String digest(File f)throws Exception{
        if(!f.isFile()||f.length()==0||f.length()>32*1024*1024)
            throw new Exception("checkpoint_missing_or_invalid:"+f.getName());
        MessageDigest md=MessageDigest.getInstance("SHA-256");
        try(FileInputStream in=new FileInputStream(f)){
            byte[] buffer=new byte[65536];int n;
            while((n=in.read(buffer))!=-1)md.update(buffer,0,n);
        }
        StringBuilder b=new StringBuilder();
        for(byte x:md.digest())b.append(String.format(Locale.ROOT,"%02x",x&255));
        return b.toString();
    }
    private static String fingerprint(File dir)throws Exception{
        if(!dir.isDirectory())throw new Exception("checkpoint_dir_missing:"+dir.getName());
        return digest(new File(dir,"weights.bin"))+":"+
               digest(new File(dir,"checkpoint.json"));
    }
    private static String guardedSnapshots(Context ctx)throws Exception{
        File base=new File(ctx.getFilesDir(),"motorai/checkpoints");
        StringBuilder s=new StringBuilder();
        for(String name:new String[]{"current","previous","autotrain-baseline",
                "goal10-original-r3r8","goal10-current-transit-r3r8"}){
            File dir=new File(base,name);
            s.append(name).append("=");
            s.append(dir.exists()?fingerprint(dir):"absent");
            s.append(";");
        }
        return s.toString();
    }
    private static final class Outcome {
        final JSONObject json;final boolean pass;
        Outcome(JSONObject json,boolean pass){this.json=json;this.pass=pass;}
    }
    private static Outcome check(Case c)throws Exception{
        JSONObject r;boolean ok;
        switch(c.family){
            case "intent":
                r=new JSONObject(MainActivity.nativeGoal1Classify(c.request));
                ok=c.answer.equals(r.optString("intent"));
                break;
            case "memory":
                r=new JSONObject(MainActivity.nativeGoal3Classify(c.request));
                ok=c.answer.equals(r.optString("action"))
                    &&c.second.equals(r.optString("slot"));
                break;
            case "reason":
                r=new JSONObject(MainActivity.nativeGoal4Solve(c.request));
                ok=r.optBoolean("valid",false)
                    &&c.answer.equals(r.optString("plan"))
                    &&r.optLong("result",Long.MIN_VALUE)==c.numeric;
                break;
            case "uncertainty":
                r=new JSONObject(MainActivity.nativeGoal5Classify(c.request));
                ok=c.answer.equals(r.optString("decision"));
                break;
            case "source":
                r=new JSONObject(MainActivity.nativeGoal6Plan(c.request));
                ok=c.answer.equals(r.optString("source"))
                   &&!r.optString("query","").trim().isEmpty();
                break;
            case "tool":
                r=new JSONObject(MainActivity.nativeGoal7Route(c.request));
                ok=c.answer.equals(r.optString("tool"));
                break;
            default:throw new IllegalStateException("Unsupported probe family");
        }
        return new Outcome(r,ok);
    }
    private static String expected(Case c){
        if("memory".equals(c.family))return c.answer+"/"+c.second;
        if("reason".equals(c.family))return c.answer+" → "+c.numeric;
        return c.answer;
    }
    public static String run(Context context) {
        synchronized(MotorAICheckpointStore.recoveryMutex()){
            if(!MotorAIRecoveryMode.paused(context))
                return "⛔ FASE 2 BLOCCATA: mantenere la pausa SOS prima della prova.";
            if(MiniAiGoals.completedCount(context)!=10)
                return "⛔ FASE 2 BLOCCATA: i 10 obiettivi non sono tutti registrati.";
            String before="";
            String nativeBefore="";
            StringBuilder b=new StringBuilder("MOTORAI FASE 2 · SONDA GENERALIZZAZIONE R3R9\n"
                +"20 richieste alternative, non incluse testualmente nel dataset sorgente.\n"
                +"Valutazione di classificazione e operazioni LIMITATE, non intelligenza generale.\n"
                +"Pausa SOS richiesta; nessun training, salvataggio, rete o cambio punteggi.\n"
                +"La memoria valutata qui classifica richieste: non ricorda dati utente.\n");
            int passed=0,errors=0;
            try {
                before=guardedSnapshots(context);
                nativeBefore=MainActivity.nativeStatus();
                for(int i=0;i<CASES.length;i++){
                    Case c=CASES[i];
                    b.append("\n").append(i+1).append("/20 · ")
                        .append(c.family.toUpperCase(Locale.ROOT)).append("\n")
                        .append("Domanda: ").append(c.request).append("\n")
                        .append("Atteso: ").append(expected(c)).append("\n");
                    try {
                        Outcome o=check(c);
                        if(o.pass)passed++;
                        b.append(o.pass?"✅ PASS":"❌ FAIL");
                        b.append("\nEffettivo: ").append(o.json).append("\n");
                    }catch(Throwable e){
                        errors++;
                        b.append("⚠️ ERRORE ").append(e.getClass().getSimpleName()).append("\n");
                    }
                }
                String after=guardedSnapshots(context);
                String nativeAfter=MainActivity.nativeStatus();
                boolean safe=before.equals(after)&&nativeBefore.equals(nativeAfter)
                        &&MotorAIRecoveryMode.paused(context)
                        &&MiniAiGoals.completedCount(context)==10;
                b.append("\nRISULTATO: ").append(passed).append("/20 · ")
                 .append(String.format(Locale.ITALY,"%.1f%%",passed*5.0))
                 .append(" · errori esecuzione ").append(errors).append("\n");
                b.append("Pesi/checkpoint/stato nativo invariati: ")
                 .append(safe?"✅ SÌ":"⛔ NON DIMOSTRATO").append("\n");
                if(!safe)b.append("⛔ Non usare il risultato come certificazione; inviare la diagnosi.\n");
                b.append("Goal 10 originale preservato; nessuna nuova promozione.\n");
                b.append("⚠️ Esito delle 20 prove: non equivalente a nuova certificazione Mini-AI.");
            }catch(Throwable e){
                b.append("\n⛔ PROVA ANNULLATA: ").append(e.getClass().getSimpleName())
                 .append(". Nessun punteggio registrato. Non modificare checkpoint.");
            }
            return b.toString();
        }
    }
}
