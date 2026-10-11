package it.motorai.seed;

import android.content.Context;
import org.json.JSONObject;
import java.io.File;
import java.io.FileInputStream;
import java.security.MessageDigest;
import java.util.Locale;
import java.util.LinkedHashMap;
import java.util.concurrent.atomic.AtomicBoolean;

/** R3R13: audit existing R3R12 questions on CURRENT in read-only mode.
 * No replay training: R3R12 rejected its unsaved shadow candidate, therefore
 * the historical post-training per-case output CANNOT be reconstructed.
 * These questions are previously disclosed; do not relabel as fresh holdout.
 */
public final class MotorAIPhase2ErrorAudit {
    private MotorAIPhase2ErrorAudit() {}
    private static final AtomicBoolean BUSY=new AtomicBoolean(false);

    private static final class Case {
        final String set;
        final int lane;
        final String question,expected;
        Case(String set,int lane,String question,String expected){
            this.set=set;this.lane=lane;this.question=question;this.expected=expected;
        }
    }
    private static final Case[] CASES={
        new Case("BLIND20",1,"ti invio i miei migliori auguri di buongiorno","saluto"),
        new Case("BLIND20",1,"che gioia risentirti mio caro assistente","saluto"),
        new Case("BLIND20",1,"ti porgo un saluto di benvenuto","saluto"),
        new Case("BLIND20",1,"una stretta di mano virtuale per iniziare","saluto"),
        new Case("BLIND20",1,"trova un dossier informativo sulle balene","ricerca"),
        new Case("BLIND20",1,"mi potresti cercare uno studio sui ghiacciai","ricerca"),
        new Case("BLIND20",1,"rintraccia articoli scientifici sull arcipelago","ricerca"),
        new Case("BLIND20",1,"cerca documentazione sulle foreste tropicali","ricerca"),
        new Case("BLIND20",1,"vorrei il risultato di ventisette piu otto","calcolo"),
        new Case("BLIND20",1,"apri il programma per modificare i file audio","azione"),
        new Case("BLIND20",7,"utilizza un operatore aritmetico per addizionare dieci e nove","calculator"),
        new Case("BLIND20",7,"per cortesia fammi la somma di quaranta e undici","calculator"),
        new Case("BLIND20",7,"risolvi il conto di ventitre piu sette","calculator"),
        new Case("BLIND20",7,"usa un algoritmo numerico per una moltiplicazione","calculator"),
        new Case("BLIND20",7,"reperisci sul web un articolo sull oceano artico","search"),
        new Case("BLIND20",7,"vorrei una ricerca esterna sulle galassie","search"),
        new Case("BLIND20",7,"cerca informazioni in rete sui terremoti","search"),
        new Case("BLIND20",7,"consultiamo internet per le notizie sui pianeti","search"),
        new Case("BLIND20",7,"quale colore hai memorizzato per me","memory"),
        new Case("BLIND20",7,"buon pomeriggio assistente virtuale","chat"),
        new Case("GUARD24",7,"ehi amico voglio parlare con te","chat"),
        new Case("GUARD24",7,"ti mando un cordiale buongiorno","chat"),
        new Case("GUARD24",7,"possiamo discorrere un momento","chat"),
        new Case("GUARD24",7,"dimmi una breve battuta","chat"),
        new Case("GUARD24",7,"ciao come sta andando la tua giornata","chat"),
        new Case("GUARD24",7,"rispondi con un messaggio amichevole","chat"),
        new Case("GUARD24",7,"ricorda la mia data preferita","memory"),
        new Case("GUARD24",7,"che cosa avevi salvato del mio nome","memory"),
        new Case("GUARD24",7,"memorizza il nome del mio gatto","memory"),
        new Case("GUARD24",7,"puoi ricordare il mio animale domestico","memory"),
        new Case("GUARD24",7,"salva la preferenza che ti comunico","memory"),
        new Case("GUARD24",7,"cosa sai dalle tue note su di me","memory"),
        new Case("GUARD24",7,"quanto risulta la somma fra quindici e sei","calculator"),
        new Case("GUARD24",7,"esegui un conto con la divisione","calculator"),
        new Case("GUARD24",7,"calcola otto moltiplicato per quattro","calculator"),
        new Case("GUARD24",7,"usa una calcolatrice per trovare un totale","calculator"),
        new Case("GUARD24",7,"fammi il risultato di ventotto meno undici","calculator"),
        new Case("GUARD24",7,"esegui un calcolo matematico con tre cifre","calculator"),
        new Case("GUARD24",7,"cerca una pagina dedicata alla geografia","search"),
        new Case("GUARD24",7,"trova informazioni online sugli uccelli","search"),
        new Case("GUARD24",7,"quali sono le ultime notizie del giorno","search"),
        new Case("GUARD24",7,"reperisci informazioni sul sito di un museo","search"),
        new Case("GUARD24",7,"consulta una fonte web sui cambiamenti climatici","search"),
        new Case("GUARD24",7,"cerca documenti esterni sulla storia romana","search"),
        new Case("REPLAY6",1,"mi aiuti a sommare tredici e sei","calcolo"),
        new Case("REPLAY6",1,"un saluto a te motore","saluto"),
        new Case("REPLAY6",1,"per favore trova un articolo sulla botanica","ricerca"),
        new Case("REPLAY6",1,"puoi aprire il lettore musicale","azione"),
        new Case("REPLAY6",7,"somma ventidue e trenta con la calcolatrice","calculator"),
        new Case("REPLAY6",7,"trova online notizie sulle comete","search")
    };
    public static int count(){return CASES.length;}

    private static String sha256(File f)throws Exception {
        if(!f.isFile()||f.length()==0||f.length()>32*1024*1024)
            throw new IllegalStateException("checkpoint_invalid:"+f.getName());
        MessageDigest digest=MessageDigest.getInstance("SHA-256");
        try(FileInputStream in=new FileInputStream(f)){
            byte[] buf=new byte[65536];int n;
            while((n=in.read(buf))!=-1)digest.update(buf,0,n);
        }
        StringBuilder b=new StringBuilder();
        for(byte x:digest.digest())b.append(String.format(Locale.ROOT,"%02x",x&255));
        return b.toString();
    }
    private static String snapshots(Context c)throws Exception{
        File root=new File(c.getFilesDir(),"motorai/checkpoints");
        StringBuilder out=new StringBuilder();
        for(String name:new String[]{"current","previous","autotrain-baseline",
                 "goal10-original-r3r8","goal10-current-transit-r3r8"}){
            File dir=new File(root,name);
            out.append(name).append("=");
            if(dir.exists())out.append(sha256(new File(dir,"weights.bin")))
                .append(":").append(sha256(new File(dir,"checkpoint.json")));
            else out.append("absent");
            out.append(";");
        }
        return out.toString();
    }

    public static String run(Context c){
        if(!BUSY.compareAndSet(false,true))
            return "⏳ Analisi già in corso; attendere.";
        try {
            synchronized(MotorAICheckpointStore.recoveryMutex()){
                if(!MotorAIRecoveryMode.paused(c))
                    return "⛔ Attivare la pausa SOS prima dell'analisi.";
                if(MiniAiGoals.completedCount(c)!=10)
                    return "⛔ Percorso Mini-AI non risulta 10/10. Analisi bloccata.";
                final String before=snapshots(c);
                final String nativeBefore=MainActivity.nativeStatus();
                StringBuilder out=new StringBuilder("🔬 R3R13 — ANALISI DETTAGLIATA IN SOLA LETTURA\n"
                    +"Sul modello PRINCIPALE CURRENT, senza alcun nuovo allenamento.\n"
                    +"R3R12 era stata respinta: non esiste una candidata post-training salvata "
                    +"da cui ricostruire le risposte singole dopo allenamento.\n"
                    +"Queste 50 domande sono già state usate in R3R12: NON sono un nuovo TEST.\n");
                String set="";
                int count=0,correct=0,errors=0,lowConfidence=0;
                int sectionTotal=0,sectionPass=0;
                LinkedHashMap<String,int[]> categories=new LinkedHashMap<>();
                LinkedHashMap<String,Integer> confusions=new LinkedHashMap<>();
                for(int ix=0;ix<CASES.length;ix++){
                    Case q=CASES[ix];
                    if(!set.equals(q.set)){
                        if(!set.isEmpty())
                            out.append("RIEPILOGO ").append(set).append(": ")
                               .append(sectionPass).append("/").append(sectionTotal).append("\n");
                        set=q.set;sectionPass=0;sectionTotal=0;
                        out.append("\n── ").append(set).append(" ──\n");
                    }
                    sectionTotal++;count++;
                    String actual="ERRORE";double confidence=Double.NaN;
                    boolean pass=false;
                    try{
                        String raw=q.lane==1?MainActivity.nativeGoal1Classify(q.question):
                            MainActivity.nativeGoal7Route(q.question);
                        JSONObject result=new JSONObject(raw);
                        actual=result.optString(q.lane==1?"intent":"tool","(nessuna classe)");
                        confidence=result.optDouble("confidence",Double.NaN);
                        pass=q.expected.equals(actual);
                        if(!Double.isFinite(confidence))errors++;
                        else if(confidence<0.50)lowConfidence++;
                    }catch(Throwable e){
                        errors++;
                        actual="ERRORE:"+e.getClass().getSimpleName();
                    }
                    if(pass){correct++;sectionPass++;}
                    else{
                        String confusion=q.expected+" → "+actual;
                        confusions.put(confusion,confusions.containsKey(confusion)?
                            confusions.get(confusion)+1:1);
                    }
                    String key=q.set+" / "+(q.lane==1?"Goal1 ":"Goal7 ")+q.expected;
                    int[] values=categories.get(key);
                    if(values==null){values=new int[]{0,0};categories.put(key,values);}
                    values[0]++;if(pass)values[1]++;
                    out.append(ix+1).append("/50 ").append(pass?"✅":"❌")
                      .append(" [").append(q.lane==1?"Goal1":"Goal7").append("] ")
                      .append(q.question).append("\n")
                      .append("Atteso: ").append(q.expected)
                      .append(" | Effettivo: ").append(actual)
                      .append(" | Confidenza: ")
                      .append(Double.isFinite(confidence)?
                        String.format(Locale.ITALY,"%.4f",confidence):"non disponibile")
                      .append("\n");
                }
                out.append("RIEPILOGO ").append(set).append(": ")
                    .append(sectionPass).append("/").append(sectionTotal).append("\n");
                out.append("\nRIEPILOGO PER CATEGORIA\n");
                for(String name:categories.keySet()){
                    int[] n=categories.get(name);
                    out.append(name).append(": ").append(n[1]).append("/").append(n[0]).append("\n");
                }
                out.append("\nTIPI DI CONFUSIONE\n");
                if(confusions.isEmpty())out.append("Nessuno\n");
                else for(String name:confusions.keySet())
                    out.append(name).append(": ").append(confusions.get(name)).append("\n");
                out.append("\nTotale risposte corrette: ").append(correct).append("/").append(count)
                    .append("; errori esecuzione/confidenza: ").append(errors)
                    .append("; confidenza < 0,50: ").append(lowConfidence).append("/").append(count).append("\n");
                boolean safe=before.equals(snapshots(c))
                    &&nativeBefore.equals(MainActivity.nativeStatus())
                    &&MotorAIRecoveryMode.paused(c)
                    &&MiniAiGoals.completedCount(c)==10;
                out.append("Pesi/checkpoint/stato invariati: ")
                    .append(safe?"✅ SÌ":"⛔ NON VERIFICATO").append("\n");
                out.append("Nessuna copia allenata, nessuna promozione, nessun ripristino.\n");
                out.append("Per il prossimo esperimento serve un nuovo TEST indipendente, "
                    +"distinto da questi esempi diagnostici.");
                return out.toString();
            }
        }catch(Throwable e){
            return "⛔ ANALISI R3R13 INTERROTTA: "+e.getClass().getSimpleName()
                +". Non eseguire allenamento o ripristino; inviare la diagnosi.";
        }finally{BUSY.set(false);}
    }
}
