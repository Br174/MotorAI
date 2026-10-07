package it.motorai.seed;

import android.app.Activity;
import android.graphics.Typeface;
import android.os.Bundle;
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

    private TextView text(String value, int sp, boolean bold) {
        TextView v = new TextView(this);
        v.setText(value);
        v.setTextSize(sp);
        v.setPadding(22, 10, 22, 10);
        if (bold) v.setTypeface(Typeface.DEFAULT_BOLD);
        return v;
    }

    private Button button(String value) {
        Button b = new Button(this);
        b.setText(value);
        b.setAllCaps(false);
        return b;
    }

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);

        ScrollView scroll = new ScrollView(this);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(20, 28, 20, 28);
        scroll.addView(root);

        root.addView(text("🎯 Percorso Mini-AI", 25, true));
        root.addView(text(
                "Questa scheda misura quanto MotorAI si sta avvicinando a una piccola AI assistente. " +
                "Le fondamenta neurali L0-L5 sono già consolidate, ma restano separate da questi 10 macro-obiettivi.", 14, false));

        totalText = text("Capacità complessiva: —", 20, true);
        totalBar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        totalBar.setMax(100);
        root.addView(totalText);
        root.addView(totalBar, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(24)));

        root.addView(text("✅ Fondamenta neurali: L0-L5 completate
" +
                "Non vengono sommate artificialmente al punteggio Mini-AI: servono come base per i nuovi obiettivi.", 14, false));

        root.addView(text("Come si calcola", 17, true));
        root.addView(text(
                "Ogni obiettivo pesa il 10% del totale. La sua percentuale cresce solo quando benchmark dedicati " +
                "mostrano progresso reale. Il 100% arriva soltanto quando l'obiettivo supera il controllo finale.", 13, false));

        goalsRoot = new LinearLayout(this);
        goalsRoot.setOrientation(LinearLayout.VERTICAL);
        root.addView(goalsRoot);

        Button refresh = button("↻ Aggiorna percentuali");
        Button close = button("← Torna a MotorAI");
        root.addView(refresh);
        root.addView(close);

        refresh.setOnClickListener(v -> load());
        close.setOnClickListener(v -> finish());

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
                "Capacità complessiva Mini-AI: %.1f%% · %d/10 completati",
                total, completed));
        totalBar.setProgress((int)Math.round(total));

        goalsRoot.removeAllViews();
        for (int i = 0; i < MiniAiGoals.TITLES.length; i++) {
            int percent = MiniAiGoals.percent(this, i);
            String status = MiniAiGoals.status(this, i);

            goalsRoot.addView(text((i + 1) + ". " + MiniAiGoals.TITLES[i]
                    + " · " + percent + "% · " + status, 15, true));

            ProgressBar bar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
            bar.setMax(100);
            bar.setProgress(percent);
            bar.setPadding(22, 0, 22, 4);
            goalsRoot.addView(bar, new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, dp(18)));

            goalsRoot.addView(text(MiniAiGoals.DESCRIPTIONS[i], 13, false));
            goalsRoot.addView(text("Verifica: " + MiniAiGoals.evidence(this, i), 12, false));
        }
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
