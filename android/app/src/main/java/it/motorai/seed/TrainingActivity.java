package it.motorai.seed;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

public class TrainingActivity extends Activity {
    private final Handler handler = new Handler(Looper.getMainLooper());
    private TextView stateText;
    private TextView goalText;
    private TextView percentText;
    private TextView stepText;
    private TextView validationText;
    private TextView lastText;
    private TextView updatedText;
    private TextView historyText;
    private TextView totalText;
    private ProgressBar goalBar;
    private ProgressBar totalBar;

    private final Runnable ticker = new Runnable() {
        @Override public void run() {
            refresh();
            handler.postDelayed(this, 1000L);
        }
    };

    private int dp(int v){
        return Math.round(v * getResources().getDisplayMetrics().density);
    }

    private GradientDrawable rounded(int color,int radiusDp){
        GradientDrawable d = new GradientDrawable();
        d.setColor(color);
        d.setCornerRadius(dp(radiusDp));
        d.setStroke(dp(1),Color.rgb(225,232,242));
        return d;
    }

    private TextView text(String value,int sp,boolean bold){
        TextView v=new TextView(this);
        v.setText(value);
        v.setTextSize(sp);
        v.setTextColor(Color.rgb(32,42,66));
        v.setPadding(0,dp(4),0,dp(4));
        if(bold) v.setTypeface(Typeface.DEFAULT_BOLD);
        return v;
    }

    private LinearLayout card(int color){
        LinearLayout v=new LinearLayout(this);
        v.setOrientation(LinearLayout.VERTICAL);
        v.setPadding(dp(16),dp(14),dp(16),dp(14));
        v.setBackground(rounded(color,18));
        LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,LinearLayout.LayoutParams.WRAP_CONTENT);
        lp.setMargins(0,0,0,dp(12));
        v.setLayoutParams(lp);
        return v;
    }

    private Button button(String label){
        Button b=new Button(this);
        b.setText(label);
        b.setAllCaps(false);
        return b;
    }

    @Override protected void onCreate(Bundle state){
        super.onCreate(state);

        ScrollView scroll=new ScrollView(this);
        LinearLayout root=new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(16),dp(20),dp(16),dp(24));
        root.setBackgroundColor(Color.rgb(247,249,253));
        scroll.addView(root);

        LinearLayout header=new LinearLayout(this);
        header.setGravity(Gravity.CENTER_VERTICAL);
        Button back=button("←");
        header.addView(back,new LinearLayout.LayoutParams(dp(54),dp(54)));
        LinearLayout titles=new LinearLayout(this);
        titles.setOrientation(LinearLayout.VERTICAL);
        titles.addView(text("🏋️ Allenamento MotorAI",25,true));
        titles.addView(text("Qui vedi se sta lavorando davvero.",14,false));
        header.addView(titles,new LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT,1));
        root.addView(header);
        back.setOnClickListener(v -> finish());

        LinearLayout live=card(Color.rgb(244,252,247));
        stateText=text("Stato: —",20,true);
        goalText=text("Obiettivo: —",18,true);
        live.addView(stateText);
        live.addView(goalText);
        root.addView(live);

        LinearLayout progress=card(Color.rgb(246,250,255));
        percentText=text("Progresso obiettivo: —",20,true);
        progress.addView(percentText);
        goalBar=new ProgressBar(this,null,android.R.attr.progressBarStyleHorizontal);
        goalBar.setMax(100);
        progress.addView(goalBar,new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,dp(22)));
        stepText=text("Step obiettivo: —",15,true);
        validationText=text("Qualità attuale: —",15,false);
        progress.addView(stepText);
        progress.addView(validationText);
        root.addView(progress);

        LinearLayout total=card(Color.WHITE);
        totalText=text("Percorso Mini-AI totale: —",17,true);
        total.addView(totalText);
        totalBar=new ProgressBar(this,null,android.R.attr.progressBarStyleHorizontal);
        totalBar.setMax(100);
        total.addView(totalBar,new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,dp(18)));
        root.addView(total);

        LinearLayout activity=card(Color.WHITE);
        activity.addView(text("🔎 Cosa sta succedendo",17,true));
        lastText=text("—",15,false);
        updatedText=text("Ultimo aggiornamento: —",13,false);
        historyText=text("Nessuna attività registrata.",13,false);
        activity.addView(lastText);
        activity.addView(updatedText);
        activity.addView(text("Cronologia recente",14,true));
        activity.addView(historyText);
        root.addView(activity);

        Button goals=button("🎯 Apri i 10 obiettivi");
        Button lab=button("🧪 Apri Laboratorio");
        root.addView(goals);
        root.addView(lab);
        goals.setOnClickListener(v -> startActivity(new Intent(this,MiniAiGoalsActivity.class)));
        lab.setOnClickListener(v -> startActivity(new Intent(this,MainActivity.class)));

        setContentView(scroll);
    }

    @Override protected void onResume(){
        super.onResume();
        MotorAIBackgroundJobService.schedule(this);
        MotorAIBackgroundJobService.scheduleKick(getApplicationContext());
        handler.removeCallbacks(ticker);
        handler.post(ticker);
    }

    @Override protected void onPause(){
        handler.removeCallbacks(ticker);
        super.onPause();
    }

    private void refresh(){
        MiniAiGoals.seedIfNeeded(this);
        int goal=MiniAiTrainingStatus.goalIndex(this);
        goal=Math.max(0,Math.min(9,goal));
        int percent=MiniAiGoals.percent(this,goal);
        String rawState=MiniAiTrainingStatus.state(this);
        int step=MiniAiTrainingStatus.step(this);
        double validation=MiniAiTrainingStatus.validation(this);
        String msg=MiniAiTrainingStatus.message(this);
        long updated=MiniAiTrainingStatus.updatedMs(this);
        if ("training".equals(rawState)
                && (updated <= 0 || System.currentTimeMillis() - updated > 30_000L)) {
            rawState = "waiting";
            msg = "Nessun nuovo step negli ultimi 30 secondi: training in attesa di verifica.";
        }

        String icon;
        if("training".equals(rawState)) icon="🟢";
        else if("testing".equals(rawState)) icon="🟡";
        else if("completed".equals(rawState)) icon="✅";
        else if("error".equals(rawState)) icon="🔴";
        else icon="⚪";

        stateText.setText(icon+" "+MiniAiTrainingStatus.stateLabel(rawState));
        goalText.setText("Obiettivo "+(goal+1)+"/10 · "+MiniAiGoals.TITLES[goal]);
        percentText.setText("Progresso obiettivo: "+percent+"%");
        goalBar.setProgress(percent);
        stepText.setText("Step obiettivo: "+step);
        validationText.setText(validation>0
                ? String.format(Locale.ITALY,"Qualità attuale: %.1f%%",validation*100.0)
                : "Qualità attuale: non ancora misurata");
        lastText.setText(msg == null || msg.isEmpty()
                ? MiniAiGoals.evidence(this,goal) : msg);

        if(updated>0){
            String t=new SimpleDateFormat("HH:mm:ss",Locale.ITALY).format(new Date(updated));
            long age=Math.max(0,(System.currentTimeMillis()-updated)/1000L);
            updatedText.setText("Ultimo aggiornamento: "+t+" · "+age+" s fa");
        }else{
            updatedText.setText("Ultimo aggiornamento: —");
        }

        String history=MiniAiTrainingStatus.history(this);
        historyText.setText(history==null||history.isEmpty()
                ?"Nessuna attività registrata.":history);

        double total=MiniAiGoals.totalPercent(this);
        totalText.setText(String.format(Locale.ITALY,
                "Percorso Mini-AI totale: %.1f%% · %d/10 completati",
                total,MiniAiGoals.completedCount(this)));
        totalBar.setProgress((int)Math.round(total));
    }
}
