package it.motorai.seed;

import android.content.Context;
import org.json.JSONObject;
import java.io.File;
import java.io.FileInputStream;
import java.security.MessageDigest;
import java.util.Locale;

/** On-device isolated Goal10 certification preview. Never alters live weights or progress. */
public final class MiniAiGoal10CandidateInspector {
    private MiniAiGoal10CandidateInspector() {}
    private static final String[] KEYS={
        "comprensione","risposta","memoria","ragionamento","incertezza",
        "ricerca_wiki","ricerca_live","strumenti_calc","strumenti_search",
        "piano","autocontrollo_calcolo","autocontrollo_fonte"
    };
    private static final String[] TESTS={
        "comprensione","risposta","memoria","ragionamento","incertezza",
        "ricerca","strumenti","piano","autocontrollo-calcolo","autocontrollo-fonte"
    };
    private static double conf(JSONObject j){ return j.optDouble("confidence",0.0); }
    private static boolean pass(String test,JSONObject p){
        switch(test){
            case "comprensione": {
                JSONObject x=p.optJSONObject("comprensione");
                return x!=null && "calcolo".equals(x.optString("intent")) && conf(x)>=.50;
            }
            case "risposta": {
                JSONObject x=p.optJSONObject("risposta");
                return x!=null && !x.optString("reply","").trim().isEmpty();
            }
            case "memoria": {
                JSONObject x=p.optJSONObject("memoria");
                return x!=null && "recall".equals(x.optString("action"))
                    && "name".equals(x.optString("slot")) && conf(x)>=.50;
            }
            case "ragionamento": {
                JSONObject x=p.optJSONObject("ragionamento");
                return x!=null && x.optBoolean("valid",false) &&
                    "add_sub".equals(x.optString("plan")) && x.optLong("result",Long.MIN_VALUE)==9L;
            }
            case "incertezza": {
                JSONObject x=p.optJSONObject("incertezza");
                return x!=null && "verify".equals(x.optString("decision")) && conf(x)>=.55;
            }
            case "ricerca": {
                JSONObject a=p.optJSONObject("ricerca_wiki"),b=p.optJSONObject("ricerca_live");
                return a!=null && b!=null &&
                    "wikipedia".equals(a.optString("source")) &&
                    "live".equals(b.optString("source")) &&
                    !a.optString("query","").trim().isEmpty();
            }
            case "strumenti": {
                JSONObject a=p.optJSONObject("strumenti_calc"),b=p.optJSONObject("strumenti_search");
                return a!=null && b!=null &&
                    "calculator".equals(a.optString("tool")) && "search".equals(b.optString("tool"));
            }
            case "piano": {
                JSONObject x=p.optJSONObject("piano");
                return x!=null && "sequence".equals(x.optString("mode")) &&
                    !x.optString("first","").isEmpty() && !x.optString("second","").isEmpty();
            }
            case "autocontrollo-calcolo": {
                JSONObject x=p.optJSONObject("autocontrollo_calcolo");
                return x!=null && "calculation".equals(x.optString("check")) && conf(x)>=.50;
            }
            case "autocontrollo-fonte": {
                JSONObject x=p.optJSONObject("autocontrollo_fonte");
                return x!=null && "source".equals(x.optString("check")) && conf(x)>=.50;
            }
            default:return false;
        }
    }
    private static String fingerprint(File directory)throws Exception{
        MessageDigest d=MessageDigest.getInstance("SHA-256");
        StringBuilder result=new StringBuilder();
        for(String name:new String[]{"weights.bin","checkpoint.json"}){
            File f=new File(directory,name);
            if(!f.isFile()||f.length()==0||f.length()>32*1024*1024)
                throw new Exception("File assente/non valido: "+name);
            d.reset();
            byte[] buffer=new byte[65536];
            try(FileInputStream stream=new FileInputStream(f)){
                int n;while((n=stream.read(buffer))!=-1)d.update(buffer,0,n);
            }
            for(byte x:d.digest())result.append(String.format(Locale.ROOT,"%02x",x&255));
        }
        return result.toString();
    }
    public static String inspect(Context context)throws Exception {
        if(!MotorAIGoal10DiagnosticHold.shouldHold(context))
            return "Goal 10 non in pausa diagnostica. Nessuna prova eseguita.";
        synchronized(MotorAICheckpointStore.recoveryMutex()){
            File current=new File(context.getFilesDir(),"motorai/checkpoints/current");
            File candidate=new File(context.getFilesDir(),"motorai/goal10-shadow-candidate-r3r5");
            if(!new File(candidate,"weights.bin").isFile())
                return "Candidata temporanea non trovata. Non ripetere l'allenamento: inviare la diagnosi.";
            String activeBefore=fingerprint(current);
            String candidateBefore=fingerprint(candidate);
            JSONObject evidence=new JSONObject(
                MainActivity.nativeGoal10CandidateCompare(candidate.getAbsolutePath()));
            if(!activeBefore.equals(fingerprint(current)) ||
               !candidateBefore.equals(fingerprint(candidate)))
                return "❌ BLOCCO: impronte checkpoint cambiate durante la lettura. Nessun dato applicato.";
            if(!evidence.optBoolean("loaded",false) ||
               !evidence.optBoolean("live_unchanged",false))
                return "❌ BLOCCO: candidata non verificabile: "+
                    evidence.optString("reason","indeterminato")+". Nessun dato applicato.";

            JSONObject active=evidence.getJSONObject("active");
            JSONObject trial=evidence.getJSONObject("candidate");
            boolean othersEqual=true;
            for(String k:KEYS){
                if("comprensione".equals(k))continue;
                if(!active.getJSONObject(k).toString().equals(trial.getJSONObject(k).toString()))
                    othersEqual=false;
            }
            int activeCount=0, candidateCount=0;
            StringBuilder results=new StringBuilder();
            for(String t:TESTS){
                boolean initial=pass(t,active),finalPass=pass(t,trial);
                if(initial)activeCount++;
                if(finalPass)candidateCount++;
                results.append("\n").append(finalPass?"✅ ":"❌ ")
                    .append(t).append(" — originale: ").append(initial?"PASS":"FAIL")
                    .append(" · candidata: ").append(finalPass?"PASS":"FAIL");
            }
            double originalTest=evidence.optDouble("active_goal1_test",0.0);
            double newTest=evidence.optDouble("candidate_goal1_test",0.0);
            double originalVal=evidence.optDouble("active_goal1_validation",0.0);
            double newVal=evidence.optDouble("candidate_goal1_validation",0.0);
            boolean safe=activeCount==9 && candidateCount==10 && othersEqual
                && newTest+0.00001>=originalTest && newVal+0.00001>=originalVal
                && newTest>=.90 && newVal>=.90;
            StringBuilder out=new StringBuilder();
            out.append("🔬 CERTIFICAZIONE ISOLATA GOAL 10 — SOLA LETTURA");
            out.append("\nProve sul modello principale: ").append(activeCount).append("/10");
            out.append("\nProve sulla candidata: ").append(candidateCount).append("/10");
            out.append(results);
            out.append("\n\nSicurezza comprensione originale: ")
               .append(String.format(Locale.ITALY,"%.2f%%",conf(active.getJSONObject("comprensione"))*100));
            out.append("\nSicurezza comprensione candidata: ")
               .append(String.format(Locale.ITALY,"%.2f%%",conf(trial.getJSONObject("comprensione"))*100));
            out.append("\nTEST Goal 1: ")
                .append(String.format(Locale.ITALY,"%.2f%% → %.2f%%",originalTest*100,newTest*100));
            out.append("\nValidation Goal 1: ")
                .append(String.format(Locale.ITALY,"%.2f%% → %.2f%%",originalVal*100,newVal*100));
            out.append("\nRisposte delle altre nove capacità identiche: ").append(othersEqual?"SÌ":"NO");
            out.append("\nImpronte CURRENT e candidata: INVARIATE");
            out.append("\nEsito di sicurezza: ")
               .append(safe?"✅ CANDIDATA VERIFICATA IN ISOLAMENTO":
                             "⛔ NON PROMUOVERE LA CANDIDATA");
            out.append("\nNessun checkpoint ripristinato; Goal10 del modello attivo invariato.");
            out.append("\nLa verifica isolata NON equivale alla certificazione reale 10/10.");
            return out.toString();
        }
    }
}
