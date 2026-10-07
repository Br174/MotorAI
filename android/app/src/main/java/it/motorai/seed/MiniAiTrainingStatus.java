package it.motorai.seed;

import android.content.Context;
import android.content.SharedPreferences;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

public final class MiniAiTrainingStatus {
    private static final String PREFS="motorai_training_status";
    private MiniAiTrainingStatus(){}

    private static SharedPreferences p(Context c){
        return c.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    public static void update(Context c, int goalIndex, String state, int percent,
                              int step, double validation, String message) {
        SharedPreferences prefs=p(c);
        int safeGoal=Math.max(0,Math.min(9,goalIndex));
        int safePercent=Math.max(0,Math.min(100,percent));
        int safeStep=Math.max(0,step);
        String safeState=state==null?"waiting":state;
        String safeMessage=message==null?"":message;
        long now=System.currentTimeMillis();

        String time=new SimpleDateFormat("HH:mm:ss",Locale.ITALY).format(new Date(now));
        String entry=time+" · Obiettivo "+(safeGoal+1)+"/10 · "
                +stateLabel(safeState)+" · "+safePercent+"% · step "+safeStep;
        if(!safeMessage.isEmpty()) entry+=" · "+safeMessage;

        String oldHistory=prefs.getString("history","");
        String combined=oldHistory.isEmpty()?entry:oldHistory+"\n"+entry;
        String[] lines=combined.split("\\n");
        int from=Math.max(0,lines.length-12);
        StringBuilder kept=new StringBuilder();
        for(int i=from;i<lines.length;i++){
            if(kept.length()>0) kept.append("\n");
            kept.append(lines[i]);
        }

        prefs.edit()
                .putInt("goal_index",safeGoal)
                .putString("state",safeState)
                .putInt("percent",safePercent)
                .putInt("step",safeStep)
                .putFloat("validation",(float)validation)
                .putString("message",safeMessage)
                .putLong("updated_ms",now)
                .putString("history",kept.toString())
                .apply();
    }

    public static int goalIndex(Context c){ return p(c).getInt("goal_index", MiniAiGoals.activeGoalIndex(c)); }
    public static String state(Context c){ return p(c).getString("state","waiting"); }
    public static int percent(Context c){ return p(c).getInt("percent", MiniAiGoals.percent(c, goalIndex(c))); }
    public static int step(Context c){ return p(c).getInt("step",0); }
    public static double validation(Context c){ return p(c).getFloat("validation",0f); }
    public static String message(Context c){ return p(c).getString("message",""); }
    public static long updatedMs(Context c){ return p(c).getLong("updated_ms",0L); }
    public static String history(Context c){ return p(c).getString("history",""); }

    public static String stateLabel(String raw) {
        if ("training".equals(raw)) return "TRAINING REALE";
        if ("testing".equals(raw)) return "TEST";
        if ("completed".equals(raw)) return "COMPLETATO";
        if ("paused".equals(raw)) return "IN PAUSA";
        if ("error".equals(raw)) return "PROBLEMA";
        return "IN ATTESA";
    }
}
