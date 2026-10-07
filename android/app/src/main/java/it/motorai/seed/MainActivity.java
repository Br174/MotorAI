package it.motorai.seed;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.BatteryManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.PopupMenu;
import android.widget.ScrollView;
import android.widget.TextView;

import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicBoolean;

public class MainActivity extends Activity {
    static { System.loadLibrary("motorai_native"); }

    public static native String nativeStatus();
    public static native String nativeEvaluate();
    public static native String nativeTrainChunk(int steps);
    public static native String nativeTrainingEvaluate();
    public static native void nativeRequestPause();
    public static native boolean nativeSaveCheckpoint(String path);
    public static native boolean nativeLoadCheckpoint(String path);
    public static native void nativeReset();
    public static native String nativeGenerate(String prefix);
    public static native void nativeSetCurriculum(int level);
    public static native int nativeCurriculum();
    public static native String nativeGoal1Evaluate();
    public static native String nativeGoal1TrainChunk(int steps);
    public static native String nativeGoal1FinalTest();
    public static native String nativeGoal1Classify(String text);

    private TextView metrics;
    private TextView device;
    private TextView state;
    private TextView curriculum;
    private TextView answer;
    private EditText prompt;
    private Button learn;
    private Button pause;
    private Button autoTrain;
    private EvolutionView evolutionView;
    private TextView evolutionSummary;
    private TextView capabilityNow;
    private TextView learningNow;
    private TextView latestProgress;
    private TextView nextGoal;
    private TextView miniAiSummary;
    private TextView goal1ProgressText;
    private ProgressBar goal1ProgressBar;
    private TextView liveIndicator;
    private TextView evolutionRangeLabel;
    private static final long DIAGNOSTIC_INTERVAL_MS = 20L * 60L * 1000L;
    private static final long RANGE_HOUR_MS = 60L * 60L * 1000L;
    private static final long RANGE_DAY_MS = 24L * RANGE_HOUR_MS;
    private static final long RANGE_WEEK_MS = 7L * RANGE_DAY_MS;
    private static final long RANGE_MONTH_MS = 30L * RANGE_DAY_MS;
    private static final long RANGE_YEAR_MS = 365L * RANGE_DAY_MS;
    private static final AtomicBoolean UI_ACTIVE = new AtomicBoolean(false);
    private final AtomicBoolean training = new AtomicBoolean(false);
    private final Handler heartbeatHandler = new Handler(Looper.getMainLooper());
    private long evolutionRangeMs = -1L;
    private int heartbeatTick = 0;
    private volatile int latestUiStep = -1;
    private final Runnable heartbeat = new Runnable() {
        @Override public void run() {
            if (!UI_ACTIVE.get() || liveIndicator == null) return;
            heartbeatTick++;
            boolean l5Accepted = getSharedPreferences("motorai_evolution", MODE_PRIVATE)
                    .getBoolean("l5_accepted", false);
            int goal1 = MiniAiGoals.percent(MainActivity.this, 0);
            String lamp;
            String mode;
            if (training.get()) {
                lamp = (heartbeatTick % 2 == 0) ? "🟢" : "🟩";
                mode = l5Accepted
                        ? "TRAINING REALE · Obiettivo 1/10 · " + goal1 + "%"
                        : "TRAINING REALE · fondamenta";
            } else if (goal1 >= 100) {
                lamp = "✅";
                mode = "Obiettivo 1/10 completato";
            } else {
                lamp = "🟡";
                mode = l5Accepted
                        ? "training in attesa · Obiettivo 1/10 · " + goal1 + "%"
                        : "training in attesa";
            }
            String step = latestUiStep >= 0 ? " · base step " + latestUiStep : "";
            liveIndicator.setText(lamp + " MotorAI attiva · " + mode + step);
            heartbeatHandler.postDelayed(this, 1000L);
        }
    };

    public static boolean isUiActive() { return UI_ACTIVE.get(); }

    private File checkpointRoot() { return new File(getFilesDir(), "motorai/checkpoints"); }
    private File currentCheckpoint() { return new File(checkpointRoot(), "current"); }
    private File previousCheckpoint() { return new File(checkpointRoot(), "previous"); }
    private File tempCheckpoint() { return new File(checkpointRoot(), "tmp"); }
    private File autoBaselineCheckpoint() { return new File(checkpointRoot(), "autotrain-baseline"); }
    private File evolutionHistoryFile() { return new File(getFilesDir(), "motorai/evolution.tsv"); }

    private TextView text(String value, int sp, boolean bold) {
        TextView v = new TextView(this);
        v.setText(value);
        v.setTextSize(sp);
        v.setPadding(22, 12, 22, 12);
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
        d.setStroke(dp(1), Color.rgb(225, 232, 242));
        return d;
    }

    private LinearLayout card(int color) {
        LinearLayout v = new LinearLayout(this);
        v.setOrientation(LinearLayout.VERTICAL);
        v.setPadding(dp(16), dp(14), dp(16), dp(14));
        v.setBackground(rounded(color, 18));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        lp.setMargins(0, 0, 0, dp(12));
        v.setLayoutParams(lp);
        return v;
    }

    private TextView compact(String value, int sp, boolean bold) {
        TextView v = text(value, sp, bold);
        v.setPadding(0, dp(4), 0, dp(4));
        v.setTextColor(Color.rgb(35, 45, 70));
        return v;
    }

    @Override protected void onCreate(Bundle stateBundle) {
        super.onCreate(stateBundle);
        UI_ACTIVE.set(true);
        MotorAIBackgroundJobService.schedule(this);
        setTitle("MotorAI Seed 012");

        ScrollView scroll = new ScrollView(this);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(16), dp(20), dp(16), dp(24));
        root.setBackgroundColor(Color.rgb(247, 249, 253));
        scroll.addView(root);

        LinearLayout header = new LinearLayout(this);
        header.setOrientation(LinearLayout.HORIZONTAL);
        header.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout headerText = new LinearLayout(this);
        headerText.setOrientation(LinearLayout.VERTICAL);
        headerText.addView(compact("🧠 MotorAI Seed 012", 26, true));
        headerText.addView(compact("AI locale · Apprendimento continuo", 14, false));
        header.addView(headerText, new LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        Button menu = button("⋮");
        menu.setTextSize(22);
        header.addView(menu, new LinearLayout.LayoutParams(dp(54), dp(54)));
        root.addView(header);

        state = compact("MotorAI attiva", 18, true);
        curriculum = compact("Stato: —", 14, true);
        metrics = compact("Step totali: —", 14, true);
        device = compact("Dispositivo: —", 14, true);
        liveIndicator = compact("🟡 MotorAI attiva · inizializzazione…", 14, true);

        LinearLayout statusCard = card(Color.rgb(244, 252, 247));
        statusCard.addView(state);
        statusCard.addView(liveIndicator);

        LinearLayout statsRow = new LinearLayout(this);
        statsRow.setOrientation(LinearLayout.HORIZONTAL);
        LinearLayout statA = card(Color.WHITE);
        LinearLayout statB = card(Color.WHITE);
        LinearLayout statC = card(Color.WHITE);
        statA.addView(curriculum);
        statB.addView(metrics);
        statC.addView(device);
        statsRow.addView(statA, new LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        statsRow.addView(statB, new LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        statsRow.addView(statC, new LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        statusCard.addView(statsRow);
        root.addView(statusCard);

        LinearLayout goalCard = card(Color.rgb(246, 250, 255));
        goalCard.addView(compact("🎯 Obiettivo Mini-AI 1/10", 14, false));
        goal1ProgressText = compact("Capire una richiesta normale · 0%", 20, true);
        goalCard.addView(goal1ProgressText);
        goal1ProgressBar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        goal1ProgressBar.setMax(100);
        goal1ProgressBar.setProgress(0);
        goalCard.addView(goal1ProgressBar, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(18)));
        miniAiSummary = compact("Percorso Mini-AI totale: 0,0% · 0/10 completati", 14, true);
        goalCard.addView(miniAiSummary);
        root.addView(goalCard);

        LinearLayout graphCard = card(Color.WHITE);
        graphCard.addView(compact("📈 Evoluzione MotorAI", 20, true));
        evolutionSummary = compact("", 1, false);
        evolutionSummary.setVisibility(View.GONE);
        evolutionView = new EvolutionView(this);
        graphCard.addView(evolutionView, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(240)));

        evolutionRangeLabel = compact("Intervallo grafico: Ultima ora", 13, true);
        graphCard.addView(evolutionRangeLabel);

        LinearLayout rangeRow1 = new LinearLayout(this);
        rangeRow1.setOrientation(LinearLayout.HORIZONTAL);
        Button rangeHour = button("1 ora");
        Button rangeDay = button("1 giorno");
        Button rangeWeek = button("1 settimana");
        rangeRow1.addView(rangeHour, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        rangeRow1.addView(rangeDay, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        rangeRow1.addView(rangeWeek, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        graphCard.addView(rangeRow1);

        LinearLayout rangeRow2 = new LinearLayout(this);
        rangeRow2.setOrientation(LinearLayout.HORIZONTAL);
        Button rangeMonth = button("1 mese");
        Button rangeYear = button("1 anno");
        Button rangeAll = button("Tutto");
        rangeRow2.addView(rangeMonth, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        rangeRow2.addView(rangeYear, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        rangeRow2.addView(rangeAll, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        graphCard.addView(rangeRow2);
        root.addView(graphCard);

        rangeHour.setOnClickListener(v -> setEvolutionRange(RANGE_HOUR_MS, "Ultima ora"));
        rangeDay.setOnClickListener(v -> setEvolutionRange(RANGE_DAY_MS, "Ultimo giorno"));
        rangeWeek.setOnClickListener(v -> setEvolutionRange(RANGE_WEEK_MS, "Ultima settimana"));
        rangeMonth.setOnClickListener(v -> setEvolutionRange(RANGE_MONTH_MS, "Ultimo mese"));
        rangeYear.setOnClickListener(v -> setEvolutionRange(RANGE_YEAR_MS, "Ultimo anno"));
        rangeAll.setOnClickListener(v -> setEvolutionRange(-1L, "Tutto"));
        evolutionRangeMs = RANGE_HOUR_MS;

        capabilityNow = compact("🏆 Dove siamo arrivati: —", 15, true);
        learningNow = compact("🔄 Cosa sta facendo adesso: —", 15, true);
        latestProgress = compact("", 1, false);
        latestProgress.setVisibility(View.GONE);
        nextGoal = compact("🎯 Prossimo passo: —", 15, true);

        LinearLayout reachedCard = card(Color.WHITE);
        reachedCard.addView(capabilityNow);
        root.addView(reachedCard);
        LinearLayout doingCard = card(Color.WHITE);
        doingCard.addView(learningNow);
        root.addView(doingCard);
        LinearLayout nextCard = card(Color.WHITE);
        nextCard.addView(nextGoal);
        root.addView(nextCard);

        Button miniAiGoals = button("🎯 Apri i 10 obiettivi");
        miniAiGoals.setTextSize(17);
        root.addView(miniAiGoals, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(58)));

        // Controlli tecnici restano disponibili dal menu, ma non ingombrano la home.
        Button diagnostics = button("Diagnostica");
        autoTrain = button("Avvia/Riprendi training");
        learn = button("Impara manualmente");
        pause = button("Pausa");
        Button save = button("Checkpoint");
        Button test = button("Aggiorna");
        prompt = new EditText(this);
        prompt.setText("abc>");
        Button talk = button("Genera");
        answer = compact("Risposta MotorAI: —", 14, false);
        Button reset = button("Riparti da pesi casuali");

        menu.setOnClickListener(v -> {
            PopupMenu p = new PopupMenu(this, menu);
            p.getMenu().add("Diagnostica");
            p.getMenu().add("Aggiorna stato");
            p.getMenu().add("Salva checkpoint");
            p.getMenu().add("Avvia/Riprendi training");
            p.setOnMenuItemClickListener(item -> {
                String title = item.getTitle().toString();
                if ("Diagnostica".equals(title)) diagnostics.performClick();
                else if ("Aggiorna stato".equals(title)) test.performClick();
                else if ("Salva checkpoint".equals(title)) save.performClick();
                else if ("Avvia/Riprendi training".equals(title)) autoTrain.performClick();
                return true;
            });
            p.show();
        });

        SharedPreferences uiPrefs = getSharedPreferences("motorai_ui", MODE_PRIVATE);
        String savedPrompt = uiPrefs.getString("last_prompt", "abc>");
        String savedAnswer = uiPrefs.getString("last_answer", "");
        prompt.setText(savedPrompt);
        if (!savedAnswer.isEmpty()) {
            answer.setText("Risposta MotorAI:\n" + savedAnswer.replace("\n", "↵\n"));
        }

        autoTrain.setOnClickListener(v -> startAutoTraining());
        miniAiGoals.setOnClickListener(v ->
                startActivity(new Intent(this, MiniAiGoalsActivity.class)));
        diagnostics.setOnClickListener(v -> openDiagnostics());
        learn.setOnClickListener(v -> startTraining());
        pause.setOnClickListener(v -> stopTraining("Pausa richiesta"));
        save.setOnClickListener(v -> runAsync(() -> {
            boolean ok = rotateAndSaveCheckpoint();
            ui(() -> state.setText(ok ? "Stato: checkpoint salvato" : "Stato: errore checkpoint"));
        }));
        test.setOnClickListener(v -> refreshMetrics());
        talk.setOnClickListener(v -> {
            // Leggere la UI sul main thread; il core nativo lavora poi in background.
            final String input = prompt.getText().toString();
            uiPrefs.edit().putString("last_prompt", input).apply();
            talk.setEnabled(false);
            answer.setText("Risposta MotorAI: generazione…");
            runAsync(() -> {
                try {
                    String out = nativeGenerate(input);
                    if (out == null || out.isEmpty()) out = "[nessun output]";
                    final String result = out;
                    uiPrefs.edit().putString("last_answer", result).apply();
                    ui(() -> {
                        answer.setText("Risposta MotorAI:\n" + result.replace("\n", "↵\n"));
                        state.setText("Stato: generazione completata");
                        talk.setEnabled(true);
                    });
                } catch (Throwable e) {
                    final String err = e.getClass().getSimpleName() + ": " +
                            (e.getMessage() == null ? "errore senza dettaglio" : e.getMessage());
                    uiPrefs.edit().putString("last_answer", "[errore] " + err).apply();
                    ui(() -> {
                        answer.setText("Risposta MotorAI:\n[errore] " + err);
                        state.setText("Stato: errore durante Genera");
                        talk.setEnabled(true);
                    });
                }
            });
        });
        reset.setOnClickListener(v -> {
            stopTraining("Reset");
            runAsync(() -> {
                nativeReset();
                deleteTree(currentCheckpoint());
                deleteTree(previousCheckpoint());
                getSharedPreferences("motorai_ui", MODE_PRIVATE).edit().remove("last_answer").apply();
                getSharedPreferences("motorai_evolution", MODE_PRIVATE).edit()
                        .putBoolean("l5_accepted", false)
                        .putString("last_progress", "Reset manuale: nuova traiettoria da pesi casuali.").apply();
                ui(() -> {
                    answer.setText("Risposta MotorAI: —");
                    state.setText("Stato: nuovi pesi casuali inizializzati");
                    refreshMetrics();
                });
            });
        });

        setContentView(scroll);

        runAsync(() -> {
            boolean resumed = currentCheckpoint().exists() && nativeLoadCheckpoint(currentCheckpoint().getAbsolutePath());
            try {
                int level = nativeCurriculum();
                JSONObject trainState = new JSONObject(nativeTrainingEvaluate());
                int step = trainState.optInt("step", 0);

                if (resumed && level == 0 && step >= 220) {
                    nativeSetCurriculum(1);
                    rotateAndSaveCheckpoint();
                    ui(() -> state.setText("Stato: checkpoint L0 ripreso · Livello 1 pronto"));
                } else if (resumed && level == 1 && step >= 620) {
                    nativeSetCurriculum(2);
                    rotateAndSaveCheckpoint();
                    ui(() -> state.setText("Stato: checkpoint L1 ripreso · Livello 2 pronto"));
                } else if (resumed && level == 2) {
                    double va = trainState.optDouble("val_accuracy", 0.0);
                    double r0 = trainState.optDouble("retention_l0_accuracy", 0.0);
                    double r1 = trainState.optDouble("retention_l1_accuracy", 0.0);
                    if (va >= 0.90 && r0 >= 0.90 && r1 >= 0.90) {
                        nativeSetCurriculum(3);
                        rotateAndSaveCheckpoint();
                        ui(() -> state.setText("Stato: checkpoint L2 ripreso · Livello 3 pronto"));
                    } else {
                        ui(() -> state.setText("Stato: checkpoint L2 ripreso · verifica soglia"));
                    }
                } else if (resumed && level == 3) {
                    double va = trainState.optDouble("val_accuracy", 0.0);
                    double r0 = trainState.optDouble("retention_l0_accuracy", 0.0);
                    double r1 = trainState.optDouble("retention_l1_accuracy", 0.0);
                    double r2 = trainState.optDouble("retention_l2_accuracy", 0.0);
                    if (va >= 0.90 && r0 >= 0.90 && r1 >= 0.90 && r2 >= 0.90) {
                        nativeSetCurriculum(4);
                        rotateAndSaveCheckpoint();
                        ui(() -> state.setText("Stato: checkpoint Seed 009 ripreso · Livello 4 pronto"));
                    } else {
                        ui(() -> state.setText("Stato: checkpoint L3 ripreso · verifica soglia"));
                    }
                } else if (resumed && level == 4) {
                    double va = trainState.optDouble("val_accuracy", 0.0);
                    double r0 = trainState.optDouble("retention_l0_accuracy", 0.0);
                    double r1 = trainState.optDouble("retention_l1_accuracy", 0.0);
                    double r2 = trainState.optDouble("retention_l2_accuracy", 0.0);
                    double r3 = trainState.optDouble("retention_l3_accuracy", 0.0);
                    if (va >= 0.95 && r0 >= 0.90 && r1 >= 0.90 && r2 >= 0.90 && r3 >= 0.90) {
                        ui(() -> state.setText("Stato: Seed 010 ripresa · Auto-Training V1 pronto"));
                    } else {
                        ui(() -> state.setText("Stato: checkpoint L4 ripreso · verifica necessaria"));
                    }
                } else if (resumed && level == 5) {
                    boolean accepted = getSharedPreferences("motorai_evolution", MODE_PRIVATE)
                            .getBoolean("l5_accepted", false);
                    ui(() -> state.setText(accepted
                            ? "Stato: L5 già consolidato · Auto-Training V1 completato"
                            : "Stato: Auto-Training L5 ripreso · premi Auto-Training per continuare"));
                } else if (resumed) {
                    ui(() -> state.setText("Stato: checkpoint ripreso automaticamente"));
                } else {
                    ui(() -> state.setText("Stato: Seed 012 pronta da pesi casuali"));
                }
            } catch (Exception e) {
                ui(() -> state.setText(resumed ? "Stato: checkpoint ripreso" : "Stato: Seed 010 pronta"));
            }
            refreshMetrics();
            boolean l5AcceptedNow = getSharedPreferences("motorai_evolution", MODE_PRIVATE)
                    .getBoolean("l5_accepted", false);
            if (resumed && nativeCurriculum() >= 5 && l5AcceptedNow
                    && MiniAiGoals.percent(this, 0) < 100) {
                ui(() -> {
                    if (!training.get()) startAutoTraining();
                });
            }
        });
    }

    private void startTraining() {
        if (!training.compareAndSet(false, true)) return;
        state.setText("Stato: training adattivo in corso…");
        learn.setEnabled(false);

        runAsync(() -> {
            int stablePasses = 0;
            double bestVal = -1.0;
            try {
                while (training.get()) {
                    Guard g = readGuard();
                    ui(() -> device.setText(g.description));
                    if (!g.allowed) {
                        appendDiagnosticFailure("Guardia dispositivo: " + g.reason);
                        training.set(false);
                        ui(() -> state.setText("Stato: training fermato automaticamente — " + g.reason));
                        break;
                    }

                    JSONObject before = new JSONObject(nativeTrainingEvaluate());
                    double beforeValLoss = before.optDouble("val_loss", Double.POSITIVE_INFINITY);
                    double beforeValAcc = before.optDouble("val_accuracy", 0.0);
                    double beforeRetentionL0 = before.optDouble("retention_l0_accuracy", 1.0);
                    double beforeRetentionL1 = before.optDouble("retention_l1_accuracy", 1.0);
                    double beforeRetentionL2 = before.optDouble("retention_l2_accuracy", 1.0);
                    double beforeRetentionL3 = before.optDouble("retention_l3_accuracy", 1.0);
                    double beforeRetentionL4 = before.optDouble("retention_l4_accuracy", 1.0);

                    if (!rotateAndSaveCheckpoint()) {
                        appendDiagnosticFailure("Checkpoint di sicurezza non creato durante training manuale");
                        training.set(false);
                        ui(() -> state.setText("Stato: impossibile creare checkpoint di sicurezza"));
                        break;
                    }

                    JSONObject j = new JSONObject(nativeTrainChunk(20));
                    maybeDiagnosticSnapshot(j, "Training manuale");
                    double afterValLoss = j.optDouble("val_loss", Double.POSITIVE_INFINITY);
                    double afterValAcc = j.optDouble("val_accuracy", 0.0);
                    double afterRetentionL0 = j.optDouble("retention_l0_accuracy", 1.0);
                    double afterRetentionL1 = j.optDouble("retention_l1_accuracy", 1.0);
                    double afterRetentionL2 = j.optDouble("retention_l2_accuracy", 1.0);
                    double afterRetentionL3 = j.optDouble("retention_l3_accuracy", 1.0);
                    double afterRetentionL4 = j.optDouble("retention_l4_accuracy", 1.0);
                    int level = j.optInt("curriculum", nativeCurriculum());
                    int step = j.optInt("step", 0);

                    boolean numericFailure = !Double.isFinite(afterValLoss);
                    boolean clearRegression = afterValLoss > (beforeValLoss * 1.50 + 0.10)
                            && afterValAcc <= beforeValAcc;
                    boolean forgetL0 = level >= 1
                            && afterRetentionL0 < 0.80
                            && afterRetentionL0 + 0.05 < beforeRetentionL0;
                    boolean forgetL1 = level >= 2
                            && afterRetentionL1 < 0.80
                            && afterRetentionL1 + 0.05 < beforeRetentionL1;
                    boolean forgetL2 = level >= 3
                            && afterRetentionL2 < 0.80
                            && afterRetentionL2 + 0.05 < beforeRetentionL2;
                    boolean forgetL3 = level >= 4
                            && afterRetentionL3 < 0.80
                            && afterRetentionL3 + 0.05 < beforeRetentionL3;
                    boolean forgetL4 = level >= 5
                            && afterRetentionL4 < 0.80
                            && afterRetentionL4 + 0.05 < beforeRetentionL4;

                    if (numericFailure || clearRegression || forgetL0 || forgetL1 || forgetL2 || forgetL3 || forgetL4) {
                        appendDiagnosticFailure("Regressione/dimenticanza nel training manuale · rollback automatico");
                        nativeLoadCheckpoint(currentCheckpoint().getAbsolutePath());
                        training.set(false);
                        ui(() -> {
                            state.setText("Stato: regressione/dimenticanza rilevata · rollback automatico");
                            refreshMetrics();
                        });
                        break;
                    }

                    rotateAndSaveCheckpoint();

                    String manualProgress = String.format(Locale.ITALY,
                            "Training manuale L%d · step %d · validation %.1f%%",
                            level, step, afterValAcc * 100.0);
                    recordEvolution(j, "", manualProgress, false);

                    if (afterValAcc > bestVal) bestVal = afterValAcc;
                    String line = formatTrainingMetrics(j);
                    ui(() -> metrics.setText(line));

                    double requiredValidation = level >= 4 ? 0.95 : 0.90;
                    boolean accepted = afterValAcc >= requiredValidation
                            && (level < 1 || afterRetentionL0 >= 0.90)
                            && (level < 2 || afterRetentionL1 >= 0.90)
                            && (level < 3 || afterRetentionL2 >= 0.90)
                            && (level < 4 || afterRetentionL3 >= 0.90)
                            && (level < 5 || afterRetentionL4 >= 0.90);
                    stablePasses = accepted ? stablePasses + 1 : 0;

                    if (level == 0 && step >= 220 && stablePasses >= 2) {
                        nativeSetCurriculum(1);
                        rotateAndSaveCheckpoint();
                        stablePasses = 0;
                        ui(() -> state.setText("Stato: Livello 0 superato · Livello 1 pronto"));
                    } else if (level == 1 && step >= 620 && stablePasses >= 2) {
                        nativeSetCurriculum(2);
                        rotateAndSaveCheckpoint();
                        stablePasses = 0;
                        ui(() -> state.setText("Stato: Livello 1 superato · Livello 2 pronto"));
                    } else if (level == 2 && stablePasses >= 2) {
                        nativeSetCurriculum(3);
                        rotateAndSaveCheckpoint();
                        stablePasses = 0;
                        ui(() -> state.setText("Stato: Livello 2 superato · Livello 3 pronto"));
                    } else if (level == 3 && stablePasses >= 2) {
                        nativeSetCurriculum(4);
                        rotateAndSaveCheckpoint();
                        stablePasses = 0;
                        ui(() -> state.setText("Stato: Livello 3 superato · Livello 4 pronto"));
                    } else if (level >= 4 && stablePasses >= 4) {
                        training.set(false);
                        ui(() -> {
                            state.setText("Stato: Livello 4 superato su validation · TEST finale disponibile");
                            refreshMetrics();
                        });
                    } else if (level >= 4 && step - j.optInt("curriculum_start_step", step) >= 6000) {
                        training.set(false);
                        ui(() -> {
                            state.setText("Stato: Livello 4 non ancora superato · nessuna promozione");
                            refreshMetrics();
                        });
                    }
                }
            } catch (Exception e) {
                appendDiagnosticFailure("Errore training manuale: " + safeMessage(e));
                training.set(false);
                ui(() -> state.setText("Stato: errore training — " + e.getMessage()));
            } finally {
                ui(() -> learn.setEnabled(true));
                if (!UI_ACTIVE.get()) {
                    MotorAIBackgroundJobService.scheduleKick(getApplicationContext());
                }
            }
        });
    }

    private String formatTrainingMetrics(JSONObject j) {
        int step = j.optInt("step", 0);
        latestUiStep = step;
        int level = j.optInt("curriculum", nativeCurriculum());
        int startStep = j.optInt("curriculum_start_step", step);
        ui(() -> curriculum.setText("Stato\nL" + level + " in training"));
        return "Step totali\n" + step + "\nLivello attivo: " + Math.max(0, step - startStep) + " passi";
    }

    private void startAutoTraining() {
        if (!training.compareAndSet(false, true)) return;
        autoTrain.setEnabled(false);
        learn.setEnabled(false);
        state.setText("Stato: Auto-Training V1 · preparazione…");

        runAsync(() -> {
            try {
                int level = nativeCurriculum();
                SharedPreferences evoPrefs = getSharedPreferences("motorai_evolution", MODE_PRIVATE);

                if (level == 5 && evoPrefs.getBoolean("l5_accepted", false)) {
                    runMiniAiGoal1Training();
                    return;
                }

                if (level < 4) {
                    training.set(false);
                    ui(() -> state.setText("Stato: Auto-Training richiede prima la baseline L4"));
                    return;
                }

                if (level == 4) {
                    JSONObject base = new JSONObject(nativeTrainingEvaluate());
                    boolean baseOk = base.optDouble("val_accuracy", 0.0) >= 0.95
                            && base.optDouble("retention_l0_accuracy", 0.0) >= 0.90
                            && base.optDouble("retention_l1_accuracy", 0.0) >= 0.90
                            && base.optDouble("retention_l2_accuracy", 0.0) >= 0.90
                            && base.optDouble("retention_l3_accuracy", 0.0) >= 0.90;
                    if (!baseOk) {
                        training.set(false);
                        ui(() -> state.setText("Stato: baseline L4 non abbastanza stabile per Auto-Training"));
                        return;
                    }

                    deleteTree(autoBaselineCheckpoint());
                    if (!nativeSaveCheckpoint(autoBaselineCheckpoint().getAbsolutePath())) {
                        training.set(false);
                        ui(() -> state.setText("Stato: impossibile creare baseline Auto-Training"));
                        return;
                    }

                    nativeSetCurriculum(5);
                    rotateAndSaveCheckpoint();
                    JSONObject startState = new JSONObject(nativeTrainingEvaluate());
                    recordEvolution(startState, "Inizio L5",
                            "Auto-Training ha scelto e avviato L5: uguaglianza tra primo e ultimo simbolo.", false);
                    ui(() -> state.setText("Stato: Auto-Training V1 · Livello 5 avviato automaticamente"));
                }

                int stablePasses = 0;
                while (training.get() && nativeCurriculum() == 5) {
                    Guard g = readGuard();
                    ui(() -> device.setText(g.description));
                    if (!g.allowed) {
                        appendDiagnosticFailure("Auto-Training in pausa sicura: " + g.reason);
                        rotateAndSaveCheckpoint();
                        training.set(false);
                        ui(() -> state.setText("Stato: Auto-Training in pausa sicura — " + g.reason));
                        break;
                    }

                    JSONObject before = new JSONObject(nativeTrainingEvaluate());
                    double beforeLoss = before.optDouble("val_loss", Double.POSITIVE_INFINITY);
                    double beforeAcc = before.optDouble("val_accuracy", 0.0);
                    double[] beforeR = new double[] {
                            before.optDouble("retention_l0_accuracy", 1.0),
                            before.optDouble("retention_l1_accuracy", 1.0),
                            before.optDouble("retention_l2_accuracy", 1.0),
                            before.optDouble("retention_l3_accuracy", 1.0),
                            before.optDouble("retention_l4_accuracy", 1.0)
                    };

                    if (!rotateAndSaveCheckpoint()) {
                        appendDiagnosticFailure("Auto-Training: checkpoint non disponibile");
                        training.set(false);
                        ui(() -> state.setText("Stato: Auto-Training fermato · checkpoint non disponibile"));
                        break;
                    }

                    JSONObject j = new JSONObject(nativeTrainChunk(20));
                    maybeDiagnosticSnapshot(j, "Auto-Training L5");
                    double loss = j.optDouble("val_loss", Double.POSITIVE_INFINITY);
                    double acc = j.optDouble("val_accuracy", 0.0);
                    double[] r = new double[] {
                            j.optDouble("retention_l0_accuracy", 1.0),
                            j.optDouble("retention_l1_accuracy", 1.0),
                            j.optDouble("retention_l2_accuracy", 1.0),
                            j.optDouble("retention_l3_accuracy", 1.0),
                            j.optDouble("retention_l4_accuracy", 1.0)
                    };
                    int step = j.optInt("step", 0);
                    int start = j.optInt("curriculum_start_step", step);

                    boolean regression = !Double.isFinite(loss)
                            || (loss > beforeLoss * 1.50 + 0.10 && acc <= beforeAcc);
                    boolean forgetting = false;
                    for (int i = 0; i < r.length; i++) {
                        if (r[i] < 0.80 && r[i] + 0.05 < beforeR[i]) {
                            forgetting = true;
                            break;
                        }
                    }

                    if (regression || forgetting) {
                        appendDiagnosticFailure("Auto-Training: regressione o dimenticanza rilevata · rollback");
                        nativeLoadCheckpoint(currentCheckpoint().getAbsolutePath());
                        JSONObject rolled = new JSONObject(nativeTrainingEvaluate());
                        recordEvolution(rolled, "Rollback",
                                "Regressione rilevata: MotorAI è tornata automaticamente all'ultimo checkpoint sicuro.", false);
                        training.set(false);
                        ui(() -> {
                            state.setText("Stato: Auto-Training · regressione rilevata, rollback automatico");
                            refreshMetrics();
                        });
                        break;
                    }

                    rotateAndSaveCheckpoint();

                    boolean accepted = acc >= 0.95;
                    for (double x : r) accepted = accepted && x >= 0.90;
                    stablePasses = accepted ? stablePasses + 1 : 0;

                    final String progress = String.format(Locale.ITALY,
                            "Auto-Training V1 · L5 passi %d · Val. %.1f%% · Memorie min. %.1f%%",
                            Math.max(0, step - start), acc * 100.0,
                            Math.min(Math.min(Math.min(r[0], r[1]), Math.min(r[2], r[3])), r[4]) * 100.0);
                    int levelSteps = Math.max(0, step - start);
                    String pointLabel = levelSteps % 100 == 0
                            ? "L5 · " + levelSteps + " passi" : "";
                    recordEvolution(j, pointLabel, progress, false);
                    ui(() -> {
                        state.setText("Stato: " + progress);
                        metrics.setText(formatTrainingMetrics(j));
                        updateEvolutionTexts(j, null, false);
                    });

                    if (stablePasses >= 4) {
                        JSONObject fin = new JSONObject(nativeEvaluate());
                        double test = fin.optDouble("test_accuracy", 0.0);
                        boolean finalOk = test >= 0.90
                                && fin.optDouble("retention_l0_accuracy", 0.0) >= 0.90
                                && fin.optDouble("retention_l1_accuracy", 0.0) >= 0.90
                                && fin.optDouble("retention_l2_accuracy", 0.0) >= 0.90
                                && fin.optDouble("retention_l3_accuracy", 0.0) >= 0.90
                                && fin.optDouble("retention_l4_accuracy", 0.0) >= 0.90;

                        if (finalOk) {
                            rotateAndSaveCheckpoint();
                            getSharedPreferences("motorai_evolution", MODE_PRIVATE)
                                    .edit().putBoolean("l5_accepted", true).apply();
                            recordEvolution(fin, "L5 consolidato",
                                    "Ha imparato a confrontare i primi due simboli a/b ignorando distrattori nuovi da a a z.", true);
                            training.set(false);
                            ui(() -> {
                                state.setText(String.format(Locale.ITALY,
                                        "Stato: Auto-Training V1 completato · L5 TEST %.1f%% · checkpoint salvato",
                                        test * 100.0));
                                refreshMetrics();
                            });
                        } else {
                            if (autoBaselineCheckpoint().exists()) {
                                nativeLoadCheckpoint(autoBaselineCheckpoint().getAbsolutePath());
                                rotateAndSaveCheckpoint();
                            }
                            appendDiagnosticFailure("Auto-Training: TEST finale non superato · rollback alla Seed 010");
                            getSharedPreferences("motorai_evolution", MODE_PRIVATE)
                                    .edit().putBoolean("l5_accepted", false).apply();
                            JSONObject rolled = new JSONObject(nativeTrainingEvaluate());
                            recordEvolution(rolled, "Rollback TEST",
                                    "Il TEST finale non ha confermato L5: rollback automatico alla Seed 010.", false);
                            training.set(false);
                            ui(() -> {
                                state.setText("Stato: Auto-Training · TEST finale non superato, rollback alla Seed 010");
                                refreshMetrics();
                            });
                        }
                        break;
                    }

                    if (step - start >= 5000) {
                        if (autoBaselineCheckpoint().exists()) {
                            nativeLoadCheckpoint(autoBaselineCheckpoint().getAbsolutePath());
                            rotateAndSaveCheckpoint();
                        }
                        appendDiagnosticFailure("Auto-Training: limite di sicurezza raggiunto · rollback alla Seed 010");
                        getSharedPreferences("motorai_evolution", MODE_PRIVATE)
                                .edit().putBoolean("l5_accepted", false).apply();
                        JSONObject rolled = new JSONObject(nativeTrainingEvaluate());
                        recordEvolution(rolled, "Rollback limite",
                                "Limite di sicurezza raggiunto: rollback automatico alla Seed 010.", false);
                        training.set(false);
                        ui(() -> {
                            state.setText("Stato: Auto-Training · limite di sicurezza raggiunto, rollback alla Seed 010");
                            refreshMetrics();
                        });
                        break;
                    }
                }
            } catch (Exception e) {
                appendDiagnosticFailure("Errore Auto-Training: " + safeMessage(e));
                training.set(false);
                final String err = e.getMessage() == null ? e.getClass().getSimpleName() : e.getMessage();
                ui(() -> state.setText("Stato: errore Auto-Training — " + err));
            } finally {
                ui(() -> {
                    autoTrain.setEnabled(true);
                    learn.setEnabled(true);
                });
                if (!UI_ACTIVE.get()) {
                    MotorAIBackgroundJobService.scheduleKick(getApplicationContext());
                }
            }
        });
    }

    private int goal1PercentFromValidation(double accuracy) {
        final double chance = 0.20; // 5 classi di intento.
        if (!Double.isFinite(accuracy) || accuracy <= chance) return 0;
        double normalized = (accuracy - chance) / (1.0 - chance);
        return Math.max(0, Math.min(99, (int)Math.round(normalized * 99.0)));
    }

    private void updateGoal1Ui(int percent, int goalStep, String status) {
        int safePercent = Math.max(0, Math.min(100, percent));
        if (goal1ProgressBar != null) goal1ProgressBar.setProgress(safePercent);
        if (goal1ProgressText != null) {
            goal1ProgressText.setText("Capire una richiesta normale · " + safePercent
                    + "%\n" + status + " · step obiettivo " + goalStep);
        }
        if (miniAiSummary != null) {
            miniAiSummary.setText("Percorso Mini-AI totale: "
                    + String.format(Locale.ITALY, "%.1f%%", MiniAiGoals.totalPercent(this))
                    + " · " + MiniAiGoals.completedCount(this) + "/10 completati");
        }
    }

    private void runMiniAiGoal1Training() throws Exception {
        int already = MiniAiGoals.percent(this, 0);
        if (already >= 100) {
            training.set(false);
            ui(() -> {
                state.setText("Stato: Obiettivo Mini-AI 1/10 già completato");
                updateGoal1Ui(100, 0, "completato");
                refreshMetrics();
            });
            return;
        }

        JSONObject foundation = new JSONObject(nativeEvaluate());
        final int foundationStep = foundation.optInt("step", 3100);
        final double memoryPercent = minRetention(foundation, 5) * 100.0;

        int stablePasses = 0;
        int bestPercent = already;
        state.post(() -> state.setText("Stato: Obiettivo Mini-AI 1/10 · training reale avviato"));

        while (training.get()) {
            Guard g = readGuard();
            ui(() -> device.setText(g.description));
            if (!g.allowed) {
                rotateAndSaveCheckpoint();
                training.set(false);
                final String reason = g.reason;
                ui(() -> state.setText("Stato: training Mini-AI in pausa sicura — " + reason));
                break;
            }

            JSONObject before = new JSONObject(nativeGoal1Evaluate());
            double beforeAcc = before.optDouble("validation_accuracy", 0.0);

            if (!rotateAndSaveCheckpoint()) {
                training.set(false);
                appendDiagnosticFailure("Goal1: checkpoint pre-chunk non disponibile");
                ui(() -> state.setText("Stato: Goal1 fermato · checkpoint non disponibile"));
                break;
            }

            JSONObject after = new JSONObject(nativeGoal1TrainChunk(20));
            double validation = after.optDouble("validation_accuracy", 0.0);
            int goalStep = after.optInt("goal1_step", 0);
            int measured = goal1PercentFromValidation(validation);

            if (validation + 0.20 < beforeAcc) {
                nativeLoadCheckpoint(currentCheckpoint().getAbsolutePath());
                appendDiagnosticFailure("Goal1: regressione forte rilevata · rollback");
                training.set(false);
                ui(() -> state.setText("Stato: Goal1 · regressione rilevata, rollback automatico"));
                break;
            }

            if (!rotateAndSaveCheckpoint()) {
                nativeLoadCheckpoint(currentCheckpoint().getAbsolutePath());
                appendDiagnosticFailure("Goal1: salvataggio post-chunk fallito");
                training.set(false);
                ui(() -> state.setText("Stato: Goal1 fermato · salvataggio non riuscito"));
                break;
            }

            bestPercent = Math.max(bestPercent, measured);
            String evidence = String.format(Locale.ITALY,
                    "Validation %.1f%% · step obiettivo %d", validation * 100.0, goalStep);
            MiniAiGoals.updateGoal(this, 0, bestPercent, evidence);

            double total = MiniAiGoals.totalPercent(this);
            EvolutionHistory.recordMiniAi(this, foundationStep, goalStep,
                    bestPercent, memoryPercent, total,
                    "Mini-AI 1 · " + bestPercent + "%",
                    "Comprensione di richieste semplici in italiano");

            getSharedPreferences("motorai_evolution", MODE_PRIVATE).edit()
                    .putString("last_progress", "Mini-AI 1/10 · " + bestPercent
                            + "% · step obiettivo " + goalStep).apply();

            final int shownPercent = bestPercent;
            final int shownStep = goalStep;
            ui(() -> {
                state.setText("Stato: TRAINING REALE · Obiettivo 1/10 · " + shownPercent + "%");
                updateGoal1Ui(shownPercent, shownStep, "training in corso");
                refreshEvolutionChart();
            });

            stablePasses = validation >= 0.90 ? stablePasses + 1 : 0;
            if (stablePasses >= 4) {
                JSONObject fin = new JSONObject(nativeGoal1FinalTest());
                double test = fin.optDouble("test_accuracy", 0.0);
                if (test >= 0.90) {
                    MiniAiGoals.updateGoal(this, 0, 100,
                            String.format(Locale.ITALY,
                                    "TEST separato superato: %.1f%% · step obiettivo %d",
                                    test * 100.0, goalStep));
                    rotateAndSaveCheckpoint();
                    EvolutionHistory.recordMiniAi(this, foundationStep, goalStep,
                            100.0, memoryPercent, MiniAiGoals.totalPercent(this),
                            "Mini-AI 1 completato",
                            "TEST separato superato: comprende l'intento di richieste semplici.");
                    getSharedPreferences("motorai_evolution", MODE_PRIVATE).edit()
                            .putString("last_progress", String.format(Locale.ITALY,
                                    "Mini-AI 1/10 completato · TEST %.1f%%", test * 100.0)).apply();
                    training.set(false);
                    ui(() -> {
                        state.setText(String.format(Locale.ITALY,
                                "Stato: ✅ Obiettivo Mini-AI 1/10 completato · TEST %.1f%%",
                                test * 100.0));
                        updateGoal1Ui(100, shownStep, "completato");
                        refreshEvolutionChart();
                    });
                    JSONObject remote = new JSONObject(nativeEvaluate());
                    forceDiagnosticSnapshot(remote, "miniai_goal1_completed");
                    break;
                }
                stablePasses = 0;
            }

            if (goalStep >= 2500) {
                training.set(false);
                appendDiagnosticFailure("Goal1: limite di sicurezza 2500 step raggiunto senza accettazione");
                ui(() -> state.setText("Stato: Goal1 in pausa · limite di sicurezza raggiunto"));
                break;
            }
        }
    }

    private void stopTraining(String why) {
        training.set(false);
        nativeRequestPause();
        state.setText("Stato: " + why + " · salvo al primo punto sicuro");
    }

    private String safeMessage(Throwable e) {
        if (e == null) return "errore sconosciuto";
        String m = e.getMessage();
        return (m == null || m.trim().isEmpty()) ? e.getClass().getSimpleName() : m;
    }

    private String diagnosticsTimestamp() {
        return new java.text.SimpleDateFormat("dd/MM/yyyy HH:mm:ss", Locale.ITALY)
                .format(new java.util.Date());
    }

    private synchronized void appendDiagnosticFailure(String message) {
        try {
            SharedPreferences p = getSharedPreferences("motorai_diagnostics", MODE_PRIVATE);
            String old = p.getString("failures", "");
            String entry = "• " + diagnosticsTimestamp() + " — " + message.replace("\n", " ");
            String combined = old.isEmpty() ? entry : old + "\n" + entry;
            String[] lines = combined.split("\\n");
            int from = Math.max(0, lines.length - 12);
            StringBuilder kept = new StringBuilder();
            for (int i = from; i < lines.length; i++) {
                if (kept.length() > 0) kept.append("\n");
                kept.append(lines[i]);
            }
            p.edit().putString("failures", kept.toString()).apply();
        } catch (Exception ignored) {
        }
    }

    private synchronized void maybeDiagnosticSnapshot(JSONObject j, String reason) {
        SharedPreferences p = getSharedPreferences("motorai_diagnostics", MODE_PRIVATE);
        long now = System.currentTimeMillis();
        long last = p.getLong("last_snapshot_ms", 0L);
        if (last == 0L || now - last >= DIAGNOSTIC_INTERVAL_MS) {
            saveDiagnosticSnapshot(j, reason, now);
        }
    }

    private synchronized void forceDiagnosticSnapshot(JSONObject j, String reason) {
        saveDiagnosticSnapshot(j, reason, System.currentTimeMillis());
    }

    private void saveDiagnosticSnapshot(JSONObject j, String reason, long now) {
        try {
            int level = j.optInt("curriculum", nativeCurriculum());
            int step = j.optInt("step", 0);
            int params = j.optInt("parameters", 0);
            int start = j.optInt("curriculum_start_step", step);
            double val = j.has("val_accuracy") ? j.optDouble("val_accuracy", Double.NaN) : Double.NaN;
            double valLoss = j.has("val_loss") ? j.optDouble("val_loss", Double.NaN) : Double.NaN;
            double test = j.has("test_accuracy") ? j.optDouble("test_accuracy", Double.NaN) : Double.NaN;
            double r0 = j.optDouble("retention_l0_accuracy", Double.NaN);
            double r1 = j.optDouble("retention_l1_accuracy", Double.NaN);
            double r2 = j.optDouble("retention_l2_accuracy", Double.NaN);
            double r3 = j.optDouble("retention_l3_accuracy", Double.NaN);
            double r4 = j.optDouble("retention_l4_accuracy", Double.NaN);
            Guard g = readGuard();

            StringBuilder b = new StringBuilder();
            b.append("MotorAI Seed 012\n");
            b.append("Snapshot: ").append(diagnosticsTimestamp()).append("\n");
            b.append("Motivo: ").append(reason).append("\n");
            b.append("Livello: L").append(level)
                    .append(" · Passi totali: ").append(step)
                    .append(" · Passi livello: ").append(Math.max(0, step - start)).append("\n");
            b.append("Parametri: ").append(String.format(Locale.ITALY, "%,d", params)).append("\n");

            if (Double.isFinite(val)) {
                b.append(String.format(Locale.ITALY, "Validation: %.1f%% · loss %.4f\n", val * 100.0, valLoss));
            }
            if (Double.isFinite(test)) {
                b.append(String.format(Locale.ITALY, "TEST disponibile: %.1f%%\n", test * 100.0));
            } else {
                b.append("TEST finale: nascosto/non consultato durante il training\n");
            }

            b.append("Memoria: ");
            if (Double.isFinite(r4)) b.append(String.format(Locale.ITALY, "L4 %.1f%% · ", r4 * 100.0));
            if (Double.isFinite(r3)) b.append(String.format(Locale.ITALY, "L3 %.1f%% · ", r3 * 100.0));
            if (Double.isFinite(r2)) b.append(String.format(Locale.ITALY, "L2 %.1f%% · ", r2 * 100.0));
            if (Double.isFinite(r1)) b.append(String.format(Locale.ITALY, "L1 %.1f%% · ", r1 * 100.0));
            if (Double.isFinite(r0)) b.append(String.format(Locale.ITALY, "L0 %.1f%%", r0 * 100.0));
            b.append("\n");

            b.append(g.description).append("\n");
            b.append("Checkpoint: current=").append(currentCheckpoint().exists() ? "OK" : "NO")
                    .append(" · previous=").append(previousCheckpoint().exists() ? "OK" : "NO")
                    .append(" · auto-baseline=").append(autoBaselineCheckpoint().exists() ? "OK" : "NO")
                    .append("\n");

            SharedPreferences evo = getSharedPreferences("motorai_evolution", MODE_PRIVATE);
            b.append("L5 consolidato: ").append(evo.getBoolean("l5_accepted", false) ? "SÌ" : "NO").append("\n");
            b.append("Ultimo progresso: ")
                    .append(evo.getString("last_progress", "nessuno")).append("\n");
            b.append("\nSuggerimento screenshot: includere anche la sezione “Problemi rilevati” qui sotto.");

            getSharedPreferences("motorai_diagnostics", MODE_PRIVATE).edit()
                    .putLong("last_snapshot_ms", now)
                    .putString("last_report", b.toString())
                    .apply();

            JSONObject remote = MotorAIBridgeClient.buildSnapshot(this, j, reason);
            MotorAIBridgeClient.sendSnapshot(this, remote);
        } catch (Exception e) {
            appendDiagnosticFailure("Creazione snapshot diagnostico fallita: " + safeMessage(e));
        }
    }

    private void openDiagnostics() {
        runAsync(() -> {
            try {
                int level = nativeCurriculum();
                boolean accepted = getSharedPreferences("motorai_evolution", MODE_PRIVATE)
                        .getBoolean("l5_accepted", false);
                JSONObject j = (level >= 5 && !accepted)
                        ? new JSONObject(nativeTrainingEvaluate())
                        : new JSONObject(nativeEvaluate());
                forceDiagnosticSnapshot(j, "Apertura pagina diagnostica");
            } catch (Exception e) {
                appendDiagnosticFailure("Aggiornamento diagnosi manuale fallito: " + safeMessage(e));
            }
            ui(() -> startActivity(new Intent(this, DiagnosticsActivity.class)));
        });
    }

    private void refreshMetrics() {
        runAsync(() -> {
            try {
                int level = nativeCurriculum();
                boolean l5Accepted = getSharedPreferences("motorai_evolution", MODE_PRIVATE)
                        .getBoolean("l5_accepted", false);
                JSONObject j;
                String line;
                boolean finalMetric = level < 5 || l5Accepted;
                if (level >= 5 && !l5Accepted) {
                    j = new JSONObject(nativeTrainingEvaluate());
                    line = formatTrainingMetrics(j);
                } else {
                    j = new JSONObject(nativeEvaluate());
                    line = formatMetrics(j);
                }
                seedEvolutionHistoryIfNeeded();
                EvolutionHistory.recordCurrentIfChanged(this, j, finalMetric,
                        "Stato corrente", "Sincronizzazione automatica della schermata");
                maybeDiagnosticSnapshot(j, "Controllo metriche");
                final List<EvolutionView.Point> pts = loadEvolutionPoints();
                Guard g = readGuard();
                final JSONObject snapshot = j;
                latestUiStep = j.optInt("step", latestUiStep);
                ui(() -> {
                    metrics.setText(line);
                    device.setText(g.description);
                    evolutionView.setTimeAxis(evolutionRangeMs > 0L);
                    evolutionView.setPoints(pts);
                    updateEvolutionTexts(snapshot, null, finalMetric);
                });
            } catch (Exception e) {
                ui(() -> metrics.setText("Metriche non disponibili: " + e.getMessage()));
            }
        });
    }

    private String formatMetrics(JSONObject j) {
        int step = j.optInt("step", 0);
        latestUiStep = step;
        int level = j.optInt("curriculum", nativeCurriculum());
        int startStep = j.optInt("curriculum_start_step", step);
        boolean l5Accepted = getSharedPreferences("motorai_evolution", MODE_PRIVATE)
                .getBoolean("l5_accepted", false);

        final String levelText = level >= 5 && l5Accepted
                ? "Stato\nL5 completato"
                : "Stato\nL" + level + (level >= 5 ? " in corso" : "");
        ui(() -> curriculum.setText(levelText));

        int levelSteps = Math.max(0, step - startStep);
        return "Step totali\n" + step + "\nUltimo livello: " + levelSteps + " passi";
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private double minRetention(JSONObject j, int level) {
        double min = 1.0;
        if (level >= 1) min = Math.min(min, j.optDouble("retention_l0_accuracy", 1.0));
        if (level >= 2) min = Math.min(min, j.optDouble("retention_l1_accuracy", 1.0));
        if (level >= 3) min = Math.min(min, j.optDouble("retention_l2_accuracy", 1.0));
        if (level >= 4) min = Math.min(min, j.optDouble("retention_l3_accuracy", 1.0));
        if (level >= 5) min = Math.min(min, j.optDouble("retention_l4_accuracy", 1.0));
        return min;
    }

    private double evolutionIndex(int level, double learning, double memory) {
        double curriculum = Math.min(1.0, (Math.max(0, level) + 1) / 6.0);
        return 100.0 * (0.45 * learning + 0.35 * memory + 0.20 * curriculum);
    }

    private synchronized void seedEvolutionHistoryIfNeeded() {
        EvolutionHistory.seedIfNeeded(this);
    }

    private synchronized void recordEvolution(JSONObject j, String label, String progress, boolean useTest) {
        try {
            EvolutionHistory.record(this, j, useTest, label, progress);
            getSharedPreferences("motorai_evolution", MODE_PRIVATE).edit()
                    .putString("last_progress", progress).apply();

            final List<EvolutionView.Point> pts = loadEvolutionPoints();
            final JSONObject snapshot = j;
            ui(() -> {
                evolutionView.setTimeAxis(evolutionRangeMs > 0L);
                evolutionView.setPoints(pts);
                updateEvolutionTexts(snapshot, progress, useTest);
            });
        } catch (Exception ignored) {
        }
    }

    private synchronized List<EvolutionView.Point> loadEvolutionPoints() {
        return EvolutionHistory.load(this, evolutionRangeMs);
    }

    private void setEvolutionRange(long rangeMs, String label) {
        evolutionRangeMs = rangeMs;
        evolutionRangeLabel.setText("Intervallo grafico: " + label);
        refreshEvolutionChart();
    }

    private void refreshEvolutionChart() {
        runAsync(() -> {
            final List<EvolutionView.Point> pts = loadEvolutionPoints();
            ui(() -> {
                evolutionView.setTimeAxis(evolutionRangeMs > 0L);
                evolutionView.setPoints(pts);
            });
        });
    }

    private void startHeartbeat() {
        heartbeatHandler.removeCallbacks(heartbeat);
        heartbeatTick = 0;
        heartbeatHandler.post(heartbeat);
    }

    private void stopHeartbeat() {
        heartbeatHandler.removeCallbacks(heartbeat);
    }

    private String capabilityList(int acceptedLevel) {
        StringBuilder b = new StringBuilder();
        if (acceptedLevel >= 0) b.append("✅ Copiare una sequenza breve");
        if (acceptedLevel >= 1) b.append("\n✅ Mettere due simboli al contrario");
        if (acceptedLevel >= 2) b.append("\n✅ Scegliere e ripetere il primo simbolo");
        if (acceptedLevel >= 3) b.append("\n✅ Scegliere e ripetere il secondo simbolo");
        if (acceptedLevel >= 4) b.append("\n✅ Capire se due parti corrispondono anche quando sono lontane");
        if (acceptedLevel >= 5) b.append("\n✅ Confrontare due simboli anche quando c'è in mezzo qualcosa che non serve");
        return b.toString();
    }

    private void updateEvolutionTexts(JSONObject j, String explicitProgress, boolean finalMetric) {
        int level = j.optInt("curriculum", nativeCurriculum());
        boolean l5Accepted = getSharedPreferences("motorai_evolution", MODE_PRIVATE)
                .getBoolean("l5_accepted", false);
        int acceptedLevel = level >= 5 && !l5Accepted ? 4 : Math.min(level, 5);
        double learning = finalMetric && j.has("test_accuracy")
                ? j.optDouble("test_accuracy", 0.0)
                : j.optDouble("val_accuracy", 0.0);
        double memory = minRetention(j, level);
        double evo = evolutionIndex(level, learning, memory);
        int params = j.optInt("parameters", 0);

        evolutionSummary.setText("");
        int step = j.optInt("step", 0);
        int startStep = j.optInt("curriculum_start_step", step);
        int levelSteps = Math.max(0, step - startStep);

        String reached;
        if (level >= 5 && l5Accepted) {
            reached = "🏆 Dove siamo arrivati\nL0-L5 completati. Le fondamenta sono pronte.";
        } else {
            reached = "🏆 Dove siamo arrivati\nFondamenta in costruzione: livello L" + level + ".";
        }
        capabilityNow.setText(reached);

        int currentGoal = MiniAiGoals.percent(this, 0);
        if (training.get() && l5Accepted) {
            learningNow.setText("🔄 Cosa sta facendo adesso\nTraining reale sull'Obiettivo 1/10 · "
                    + currentGoal + "%.");
        } else if (l5Accepted && currentGoal < 100) {
            learningNow.setText("🔄 Cosa sta facendo adesso\nIn attesa di continuare l'Obiettivo 1/10.");
        } else if (currentGoal >= 100) {
            learningNow.setText("🔄 Cosa sta facendo adesso\nObiettivo 1 completato. Pronta per il successivo.");
        } else {
            learningNow.setText("🔄 Cosa sta facendo adesso\nSta consolidando le fondamenta.");
        }

        latestProgress.setText("");

        if (currentGoal >= 100) {
            nextGoal.setText("🎯 Prossimo passo\nPreparare l'Obiettivo Mini-AI 2/10.");
        } else if (l5Accepted) {
            nextGoal.setText("🎯 Prossimo passo\nCompletare l'Obiettivo 1/10: capire una richiesta normale.");
        } else {
            nextGoal.setText("🎯 Prossimo passo\nCompletare le fondamenta L0-L5.");
        }

        MiniAiGoals.seedIfNeeded(this);
        int goal1 = MiniAiGoals.percent(this, 0);
        int goal1Step = 0;
        try {
            JSONObject g1 = new JSONObject(nativeGoal1Evaluate());
            goal1Step = g1.optInt("goal1_step", 0);
        } catch (Exception ignored) {
        }
        updateGoal1Ui(goal1, goal1Step,
                goal1 >= 100 ? "completato" : (training.get() ? "training in corso" : "in attesa"));
    }

    private boolean rotateAndSaveCheckpoint() {
        File root = checkpointRoot();
        if (!root.exists() && !root.mkdirs()) return false;
        deleteTree(tempCheckpoint());
        if (!nativeSaveCheckpoint(tempCheckpoint().getAbsolutePath())) return false;
        deleteTree(previousCheckpoint());
        if (currentCheckpoint().exists() && !currentCheckpoint().renameTo(previousCheckpoint())) return false;
        return tempCheckpoint().renameTo(currentCheckpoint());
    }

    private static void deleteTree(File f) {
        if (f == null || !f.exists()) return;
        if (f.isDirectory()) {
            File[] children = f.listFiles();
            if (children != null) for (File c : children) deleteTree(c);
        }
        //noinspection ResultOfMethodCallIgnored
        f.delete();
    }

    private Guard readGuard() {
        Intent battery = registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
        int temp10 = battery != null ? battery.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, 0) : 0;
        float temp = temp10 / 10f;
        int level = battery != null ? battery.getIntExtra(BatteryManager.EXTRA_LEVEL, -1) : -1;
        int status = battery != null ? battery.getIntExtra(BatteryManager.EXTRA_STATUS, -1) : -1;
        boolean charging = status == BatteryManager.BATTERY_STATUS_CHARGING || status == BatteryManager.BATTERY_STATUS_FULL;

        ActivityManager am = (ActivityManager) getSystemService(Context.ACTIVITY_SERVICE);
        ActivityManager.MemoryInfo mi = new ActivityManager.MemoryInfo();
        am.getMemoryInfo(mi);
        long freeMb = mi.availMem / (1024 * 1024);

        if (temp >= 42.0f) return new Guard(false, "temperatura " + temp + " °C", describe(temp, level, charging, freeMb));
        if (!charging && level >= 0 && level <= 20) return new Guard(false, "batteria bassa", describe(temp, level, charging, freeMb));
        if (mi.lowMemory || freeMb < 256) return new Guard(false, "memoria disponibile troppo bassa", describe(temp, level, charging, freeMb));
        return new Guard(true, "", describe(temp, level, charging, freeMb));
    }

    private String describe(float temp, int level, boolean charging, long freeMb) {
        return String.format(Locale.ITALY, "Dispositivo\n%.1f °C · Batteria %d%%%s", temp, level, charging ? " · carica" : "");
    }

    private static class Guard {
        final boolean allowed; final String reason; final String description;
        Guard(boolean a, String r, String d) { allowed=a; reason=r; description=d; }
    }

    private void runAsync(Runnable r) { new Thread(r, "MotorAI-Worker").start(); }
    private void ui(Runnable r) { runOnUiThread(r); }

    @Override protected void onStart() {
        super.onStart();
        UI_ACTIVE.set(true);
        MotorAIBackgroundJobService.schedule(this);
        startHeartbeat();
        refreshMetrics();
    }

    @Override protected void onStop() {
        UI_ACTIVE.set(false);
        stopHeartbeat();
        if (training.get()) {
            stopTraining("App in background");
        }
        // Registra il passaggio prima che Android possa sospendere il processo.
        MotorAIBackgroundJobService.scheduleKick(getApplicationContext());
        super.onStop();
    }
}
