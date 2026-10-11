package it.motorai.seed;

import org.json.JSONObject;

/** Exactly mirrors Goal10's ten tests, without training or persistence. */
public final class MiniAiGoal10Inspector {
    private MiniAiGoal10Inspector() {}
    private interface Check { Evidence run() throws Exception; }
    private static final class Evidence {
        final boolean pass; final String actual;
        Evidence(boolean pass,String actual){this.pass=pass;this.actual=actual;}
    }
    private static Evidence one(JSONObject j,boolean ok){return new Evidence(ok,j.toString());}
    private static Evidence two(JSONObject a,JSONObject b,boolean ok){
        return new Evidence(ok,a.toString()+" | "+b.toString());
    }
    private static double confidence(JSONObject j){return j.optDouble("confidence",0);}
    private static void add(StringBuilder report,String title,String input,String expected,Check probe){
        report.append("\n").append(title).append("\nRichiesta: ").append(input)
              .append("\nAtteso: ").append(expected).append("\n");
        try {
            Evidence e=probe.run();
            report.append(e.pass?"✅ PASS":"❌ FAIL").append("\nEffettivo: ").append(e.actual).append("\n");
        }catch(Throwable ex){
            report.append("❌ ERRORE: ").append(ex.getClass().getSimpleName()).append("\n");
        }
    }
    public static String inspect(){
        StringBuilder b=new StringBuilder();
        b.append("Ispezione Goal 10 — SOLA LETTURA. Non modifica pesi, checkpoint, punteggi o tentativi.\n");
        add(b,"1 COMPRENSIONE","quanto fa sette piu otto","intent calcolo; confidence >= 0,50",()->{
            JSONObject j=new JSONObject(MainActivity.nativeGoal1Classify("quanto fa sette piu otto"));
            return one(j,"calcolo".equals(j.optString("intent"))&&confidence(j)>=0.50);
        });
        add(b,"2 RISPOSTA","ciao","reply non vuota",()->{
            JSONObject j=new JSONObject(MainActivity.nativeGoal2Respond("ciao"));
            return one(j,!j.optString("reply","").trim().isEmpty());
        });
        add(b,"3 MEMORIA","come mi chiamo","action recall; slot name; confidence >= 0,50",()->{
            JSONObject j=new JSONObject(MainActivity.nativeGoal3Classify("come mi chiamo"));
            return one(j,"recall".equals(j.optString("action"))&&"name".equals(j.optString("slot"))
                &&confidence(j)>=0.50);
        });
        add(b,"4 RAGIONAMENTO","parto da 10 aggiungo 3 poi tolgo 4","valid=true; plan=add_sub; result=9",()->{
            JSONObject j=new JSONObject(MainActivity.nativeGoal4Solve("parto da 10 aggiungo 3 poi tolgo 4"));
            return one(j,j.optBoolean("valid",false)&&"add_sub".equals(j.optString("plan"))
                &&j.optLong("result",Long.MIN_VALUE)==9L);
        });
        add(b,"5 INCERTEZZA","che tempo fa oggi","decision verify; confidence >= 0,55",()->{
            JSONObject j=new JSONObject(MainActivity.nativeGoal5Classify("che tempo fa oggi"));
            return one(j,"verify".equals(j.optString("decision"))&&confidence(j)>=0.55);
        });
        add(b,"6 RICERCA","chi e michelangelo | che temperatura c e adesso",
            "source wikipedia | live; query non vuota",()->{
            JSONObject a=new JSONObject(MainActivity.nativeGoal6Plan("chi e michelangelo"));
            JSONObject c=new JSONObject(MainActivity.nativeGoal6Plan("che temperatura c e adesso"));
            return two(a,c,"wikipedia".equals(a.optString("source"))
                &&"live".equals(c.optString("source"))&&!a.optString("query","").trim().isEmpty());
        });
        add(b,"7 STRUMENTI","quanto fa diciassette piu quattro | chi e dante","tool calculator | search",()->{
            JSONObject a=new JSONObject(MainActivity.nativeGoal7Route("quanto fa diciassette piu quattro"));
            JSONObject c=new JSONObject(MainActivity.nativeGoal7Route("chi e dante"));
            return two(a,c,"calculator".equals(a.optString("tool"))&&"search".equals(c.optString("tool")));
        });
        add(b,"8 PIANO","quanto fa sette piu otto e poi cerca informazioni su marte",
            "mode sequence; first e second non vuoti",()->{
            JSONObject j=new JSONObject(MainActivity.nativeGoal8Plan("quanto fa sette piu otto e poi cerca informazioni su marte"));
            return one(j,"sequence".equals(j.optString("mode"))&&!j.optString("first","").isEmpty()
                &&!j.optString("second","").isEmpty());
        });
        add(b,"9 AUTOCONTROLLO CALCOLO","quanto fa diciassette piu quattro | 17 + 4 = 22",
            "check calculation; confidence >= 0,50",()->{
            JSONObject j=new JSONObject(MainActivity.nativeGoal9Review("quanto fa diciassette piu quattro","17 + 4 = 22"));
            return one(j,"calculation".equals(j.optString("check"))&&confidence(j)>=0.50);
        });
        add(b,"10 AUTOCONTROLLO FONTE","chi e michelangelo | Michelangelo era un artista. Fonte: Wikipedia",
            "check source; confidence >= 0,50",()->{
            JSONObject j=new JSONObject(MainActivity.nativeGoal9Review("chi e michelangelo","Michelangelo era un artista. Fonte: Wikipedia"));
            return one(j,"source".equals(j.optString("check"))&&confidence(j)>=0.50);
        });
        b.append("\nFine: nessun ripristino e nessun allenamento eseguito.");
        return b.toString();
    }
}
