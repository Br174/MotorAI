package it.motorai.seed;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;

import java.util.Locale;

public class MiniAiGoalsActivity extends Activity {
    private LinearLayout goalsRoot;
    private TextView totalText;
    private ProgressBar totalBar;
    private TextView activeTitle;
    private TextView activeStatus;
    private ProgressBar activeBar;

    private TextView text(String value, int sp, boolean bold) {
        TextView v = new TextView(this);
        v.setText(value);
        v.setTextSize(sp);
        v.setTextColor(Color.rgb(35,45,70));
        v.setPadding(0, dp(4), 0, dp(4));
        if (bold) v.setTypeface(Typeface.DEFAULT_BOLD);
        return v;
    }

    private Button button(String value) {
        Button b = new Button(this);
        b.setText(value);
        b.setAllCaps(false);
        return b;
    }

    private GradientDrawable rounded(int color, int radiusDp) {
        GradientDrawable d = new GradientDrawable();
        d.setColor(color);
        d.setCornerRadius(dp(radiusDp));
        d.setStroke(dp(1), Color.rgb(225,232,242));
        return d;
    }

    private LinearLayout card(int color) {
        LinearLayout v = new LinearLayout(this);
        v.setOrientation(LinearLayout.VERTICAL);
        v.setPadding(dp(16),dp(14),dp(16),dp(14));
        v.setBackground(rounded(color,18));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        lp.setMargins(0,0,0,dp(12));
        v.setLayoutParams(lp);
        return v;
    }

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);

        ScrollView scroll = new ScrollView(this);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(16),dp(20),dp(16),dp(24));
        root.setBackgroundColor(Color.rgb(247,249,253));
        scroll.addView(root);

        LinearLayout header = new LinearLayout(this);
        header.setOrientation(LinearLayout.HORIZONTAL);
        header.setGravity(Gravity.CENTER_VERTICAL);
        Button back = button("←");
        back.setTextSize(20);
        header.addView(back,new LinearLayout.LayoutParams(dp(54),dp(54)));
        LinearLayout headText = new LinearLayout(this);
        headText.setOrientation(LinearLayout.VERTICAL);
        headText.addView(text("🎯 Percorso Mini-AI",25,true));
        headText.addView(text("Progressi verso una piccola AI assistente.",14,false));
        header.addView(headText,new LinearLayout.LayoutParams(
                0,LinearLayout.LayoutParams.WRAP_CONTENT,1));
        root.addView(header);
        back.setOnClickListener(v -> finish());

        LinearLayout totalCard = card(Color.rgb(246,250,255));
        totalText = text("Progresso totale Mini-AI: —",20,true);
        totalCard.addView(totalText);
        totalBar = new ProgressBar(this,null,android.R.attr.progressBarStyleHorizontal);
        totalBar.setMax(100);
        totalCard.addView(totalBar,new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,dp(22)));
        root.addView(totalCard);

        LinearLayout foundationCard = card(Color.rgb(244,252,247));
        foundationCard.addView(text("✅ Fondamenta neurali: L0-L5 completate",16,true));
        foundationCard.addView(text(
                "Sono la base del percorso Mini-AI e non vengono conteggiate nei 10 obiettivi.",13,false));
        root.addView(foundationCard);

        LinearLayout activeCard = card(Color.rgb(246,250,255));
        activeCard.addView(text("🎯 Obiettivo attivo 1/10",14,false));
        activeTitle = text("Capire una richiesta normale",20,true);
        activeCard.addView(activeTitle);
        activeBar = new ProgressBar(this,null,android.R.attr.progressBarStyleHorizontal);
        activeBar.setMax(100);
        activeCard.addView(activeBar,new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,dp(20)));
        activeStatus = text("0% · non iniziato",14,true);
        activeCard.addView(activeStatus);
        activeCard.addView(text(
                "Comprendere semplici richieste in italiano e capire cosa viene chiesto.",13,false));
        root.addView(activeCard);

        root.addView(text("Tutti gli obiettivi",20,true));
        goalsRoot = new LinearLayout(this);
        goalsRoot.setOrientation(LinearLayout.VERTICAL);
        root.addView(goalsRoot);

        LinearLayout calcCard = card(Color.WHITE);
        calcCard.addView(text("📊 Come si calcola",17,true));
        calcCard.addView(text(
                "Ogni obiettivo vale il 10% del totale. Il totale è la media dei 10 obiettivi.",13,false));
        root.addView(calcCard);

        setContentView(scroll);
        load();
    }

    @Override protected void onResume() {
        super.onResume();
        load();
    }

    private void load() {
        MiniAiGoals.seedIfNeeded(this);

        double total = MiniAiGoals.totalPercent(this);
        int completed = MiniAiGoals.completedCount(this);
        totalText.setText(String.format(Locale.ITALY,
                "Progresso totale Mini-AI: %.1f%% · %d/10 completati", total, completed));
        totalBar.setProgress((int)Math.round(total));

        int active = MiniAiGoals.activeGoalIndex(this);
        int activePercent = MiniAiGoals.percent(this, active);
        activeTitle.setText((active + 1) + "/10 · " + MiniAiGoals.TITLES[active]);
        activeBar.setProgress(activePercent);
        activeStatus.setText(activePercent + "% · " + MiniAiGoals.status(this, active));

        goalsRoot.removeAllViews();
        for(int i=0;i<MiniAiGoals.TITLES.length;i++){
            int percent = MiniAiGoals.percent(this,i);
            LinearLayout row = new LinearLayout(this);
            row.setOrientation(LinearLayout.VERTICAL);
            row.setPadding(dp(12),dp(9),dp(12),dp(9));
            row.setBackground(rounded(Color.WHITE,14));
            LinearLayout.LayoutParams rowLp = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,LinearLayout.LayoutParams.WRAP_CONTENT);
            rowLp.setMargins(0,0,0,dp(8));
            row.setLayoutParams(rowLp);

            LinearLayout line = new LinearLayout(this);
            line.setOrientation(LinearLayout.HORIZONTAL);
            line.setGravity(Gravity.CENTER_VERTICAL);

            TextView number = text(String.valueOf(i+1),14,true);
            number.setGravity(Gravity.CENTER);
            number.setBackground(rounded(Color.rgb(235,245,255),12));
            line.addView(number,new LinearLayout.LayoutParams(dp(42),dp(38)));

            TextView title = text(MiniAiGoals.TITLES[i],14,true);
            title.setPadding(dp(10),0,dp(8),0);
            line.addView(title,new LinearLayout.LayoutParams(
                    0,LinearLayout.LayoutParams.WRAP_CONTENT,1));

            TextView pct = text(percent + "%",13,true);
            line.addView(pct,new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.WRAP_CONTENT,LinearLayout.LayoutParams.WRAP_CONTENT));
            row.addView(line);

            ProgressBar bar = new ProgressBar(this,null,android.R.attr.progressBarStyleHorizontal);
            bar.setMax(100);
            bar.setProgress(percent);
            row.addView(bar,new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,dp(12)));

            TextView status = text(MiniAiGoals.status(this,i),12,false);
            status.setGravity(Gravity.END);
            row.addView(status);
            goalsRoot.addView(row);
        }
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
