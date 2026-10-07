package it.motorai.seed;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.graphics.Typeface;
import android.os.BatteryManager;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
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
    private static final long DIAGNOSTIC_INTERVAL_MS = 20L * 60L * 1000L;
    private static final AtomicBoolean UI_ACTIVE = new AtomicBoolean(false);
    private final AtomicBoolean training = new AtomicBoolean(false);

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

    @Override protected void onCreate(Bundle stateBundle) {
        super.onCreate(stateBundle);
        UI_ACTIVE.set(true);
        MotorAIBackgroundJobService.schedule(this);
        setTitle("MotorAI Seed 011R1");

        ScrollView scroll = new ScrollView(this);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(20, 34, 20, 30);
        scroll.addView(root);

        root.addView(text("MotorAI Seed 011R1", 28, true));
        root.addView(text("Cervello: Transformer causale nativo C++ · pesi iniziali casuali · nessun modello preaddestrato", 15, false));
        root.addView(text("Curriculum: L0–L4 consolidati → Auto-Training V1 → L5 uguaglianza adiacente", 14, false));
        root.addView(text("Auto-Training V1: genera gli esercizi, addestra, valida, controlla le memorie, salva checkpoint e fa rollback automaticamente. L5 impara una nuova posizione relazionale: nel primo gradino autonomo confronta i primi due simboli a/b e impara a ignorare un terzo simbolo distrattore da a a z, separato fra TRAIN, VALIDATION e TEST. Il canale neurale L5 è separato da L4.", 14, false));

        state = text("Stato: inizializzazione…", 16, true);
        curriculum = text("Livello: —", 15, true);
        metrics = text("Metriche: —", 15, false);
        device = text("Dispositivo: —", 14, false);
        root.addView(state);
        root.addView(curriculum);
        root.addView(metrics);
        root.addView(device);
        root.addView(text("🌙 Auto-Training persistente: attivo quando Android lo risveglia e il telefono è in carica", 13, false));

        root.addView(text("📈 Evoluzione MotorAI", 18, true));
        evolutionSummary = text("Indice Evoluzione: —", 14, true);
        root.addView(evolutionSummary);
        evolutionView = new EvolutionView(this);
        root.addView(evolutionView, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(250)));

        capabilityNow = text("🧠 Cosa sa fare adesso: —", 15, true);
        learningNow = text("🔄 Cosa sta imparando: —", 14, false);
        latestProgress = text("🎆 Ultimo progresso: —", 14, false);
        root.addView(capabilityNow);
        root.addView(learningNow);
        root.addView(latestProgress);

        Button diagnostics = button("🩺 Diagnostica");
        root.addView(diagnostics);

        autoTrain = button("🤖 Auto-Training");
        root.addView(autoTrain);

        LinearLayout actions = new LinearLayout(this);
        actions.setOrientation(LinearLayout.HORIZONTAL);
        learn = button("▶ Impara (manuale)");
        pause = button("⏸ Pausa");
        actions.addView(learn, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        actions.addView(pause, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        root.addView(actions);

        LinearLayout actions2 = new LinearLayout(this);
        actions2.setOrientation(LinearLayout.HORIZONTAL);
        Button save = button("💾 Checkpoint");
        Button test = button("🧪 Test");
        actions2.addView(save, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        actions2.addView(test, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        root.addView(actions2);

        root.addView(text("Prova MotorAI", 18, true));
        prompt = new EditText(this);
        prompt.setHint("Esempi: abc> · ef> · ef>> · ef>>> · ?efe>");
        prompt.setText("abc>");
        root.addView(prompt);
        Button talk = button("💬 Genera");
        root.addView(talk);

        answer = text("Risposta MotorAI: —", 16, true);
        answer.setPadding(22, 18, 22, 24);
        root.addView(answer);

        SharedPreferences uiPrefs = getSharedPreferences("motorai_ui", MODE_PRIVATE);
        String savedPrompt = uiPrefs.getString("last_prompt", "abc>");
        String savedAnswer = uiPrefs.getString("last_answer", "");
        prompt.setText(savedPrompt);
        if (!savedAnswer.isEmpty()) {
            answer.setText("Risposta MotorAI:\n" + savedAnswer.replace("\n", "↵\n"));
        }

        Button reset = button("↺ Riparti da pesi casuali");
        root.addView(reset);

        autoTrain.setOnClickListener(v -> startAutoTraining());
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
                    ui(() -> state.setText("Stato: Auto-Training L5 ripreso · premi Auto-Training per continuare"));
                } else if (resumed) {
                    ui(() -> state.setText("Stato: checkpoint ripreso automaticamente"));
                } else {
                    ui(() -> state.setText("Stato: Seed 011 pronta da pesi casuali"));
                }
            } catch (Exception e) {
                ui(() -> state.setText(resumed ? "Stato: checkpoint ripreso" : "Stato: Seed 010 pronta"));
            }
            refreshMetrics();
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
        int params = j.optInt("parameters", 9536);
        int level = j.optInt("curriculum", nativeCurriculum());
        double loss = j.optDouble("val_loss", Double.NaN);
        double acc = j.optDouble("val_accuracy", Double.NaN);
        double r0 = j.optDouble("retention_l0_accuracy", 1.0);
        double r1 = j.optDouble("retention_l1_accuracy", 1.0);
        double r2 = j.optDouble("retention_l2_accuracy", 1.0);
        double r3 = j.optDouble("retention_l3_accuracy", 1.0);
        double r4 = j.optDouble("retention_l4_accuracy", 1.0);
        int startStep = j.optInt("curriculum_start_step", step);
        final String levelText = level <= 0 ? "Livello 0 · copy3" :
                (level == 1 ? "Livello 1 · inversione" :
                (level == 2 ? "Livello 2 · duplica primo" :
                (level == 3 ? "Livello 3 · duplica secondo" :
                (level == 4 ? "Livello 4 · uguaglianza" : "Livello 5 · Auto-Training"))));
        ui(() -> curriculum.setText("Livello: " + levelText));

        if (level <= 0) {
            return String.format(Locale.ITALY,
                    "Passi: %d · Parametri: %,d · Validation loss: %.4f · Val.: %.1f%%",
                    step, params, loss, acc * 100.0);
        }
        if (level == 1) {
            return String.format(Locale.ITALY,
                    "Passi totali: %d · L1 passi: %d · Val.: %.1f%% · Memoria L0: %.1f%%",
                    step, Math.max(0, step - 220), acc * 100.0, r0 * 100.0);
        }
        if (level == 2) {
            return String.format(Locale.ITALY,
                    "Passi totali: %d · L2 passi: %d · Val.: %.1f%% · Memoria L1: %.1f%% · L0: %.1f%%",
                    step, Math.max(0, step - startStep), acc * 100.0, r1 * 100.0, r0 * 100.0);
        }
        if (level == 3) {
            return String.format(Locale.ITALY,
                    "Passi totali: %d · L3 passi: %d · Val.: %.1f%% · Memoria L2: %.1f%% · L1: %.1f%% · L0: %.1f%%",
                    step, Math.max(0, step - startStep), acc * 100.0, r2 * 100.0, r1 * 100.0, r0 * 100.0);
        }
        if (level == 4) {
            return String.format(Locale.ITALY,
                    "Passi totali: %d · L4 passi: %d · Val.: %.1f%% · Memoria L3: %.1f%% · L2: %.1f%% · L1: %.1f%% · L0: %.1f%%",
                    step, Math.max(0, step - startStep), acc * 100.0, r3 * 100.0, r2 * 100.0, r1 * 100.0, r0 * 100.0);
        }
        return String.format(Locale.ITALY,
                "Passi totali: %d · L5 auto: %d · Val.: %.1f%% · Memoria L4: %.1f%% · L3: %.1f%% · L2: %.1f%% · L1: %.1f%% · L0: %.1f%%",
                step, Math.max(0, step - startStep), acc * 100.0, r4 * 100.0, r3 * 100.0, r2 * 100.0, r1 * 100.0, r0 * 100.0);
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
                    training.set(false);
                    ui(() -> state.setText("Stato: L5 già consolidato · Auto-Training V1 completato"));
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
                    if (Math.max(0, step - start) % 100 == 0) {
                        recordEvolution(j, "L5 · " + Math.max(0, step - start) + " passi",
                                "Sta imparando L5 in autonomia.", false);
                    }
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
            b.append("MotorAI Seed 011R1\n");
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
                maybeDiagnosticSnapshot(j, "Controllo metriche");
                final List<EvolutionView.Point> pts = loadEvolutionPoints();
                Guard g = readGuard();
                final JSONObject snapshot = j;
                ui(() -> {
                    metrics.setText(line);
                    device.setText(g.description);
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
        int params = j.optInt("parameters", 9536);
        int level = j.optInt("curriculum", nativeCurriculum());
        double loss = j.has("test_loss") ? j.optDouble("test_loss") : Double.NaN;
        double acc = j.has("test_accuracy") ? j.optDouble("test_accuracy") : Double.NaN;
        double retentionL0 = j.has("retention_l0_accuracy") ? j.optDouble("retention_l0_accuracy") : acc;
        double retentionL1 = j.has("retention_l1_accuracy") ? j.optDouble("retention_l1_accuracy") : acc;
        double retentionL2 = j.has("retention_l2_accuracy") ? j.optDouble("retention_l2_accuracy") : acc;
        double retentionL3 = j.has("retention_l3_accuracy") ? j.optDouble("retention_l3_accuracy") : acc;
        double retentionL4 = j.has("retention_l4_accuracy") ? j.optDouble("retention_l4_accuracy") : acc;
        int startStep = j.optInt("curriculum_start_step", step);
        final String levelText = level <= 0 ? "Livello 0 · copy3" :
                (level == 1 ? "Livello 1 · inversione" :
                (level == 2 ? "Livello 2 · duplica primo" :
                (level == 3 ? "Livello 3 · duplica secondo" :
                (level == 4 ? "Livello 4 · uguaglianza" : "Livello 5 · Auto-Training"))));
        ui(() -> curriculum.setText("Livello: " + levelText));
        if (level <= 0) {
            return String.format(Locale.ITALY, "Passi: %d · Parametri: %,d · Test loss: %.4f · Generalizzazione: %.1f%%", step, params, loss, acc * 100.0);
        }
        if (level == 1) {
            return String.format(Locale.ITALY, "Passi totali: %d · L1: %d/400 · Parametri: %,d · L1 loss: %.4f · L1 gen.: %.1f%% · Memoria L0: %.1f%%",
                    step, Math.max(0, step - 220), params, loss, acc * 100.0, retentionL0 * 100.0);
        }
        if (level == 2) {
            return String.format(Locale.ITALY, "Passi totali: %d · L2 passi: %d · Parametri: %,d · L2 test loss: %.4f · L2 test: %.1f%% · Memoria L1: %.1f%% · L0: %.1f%%",
                    step, Math.max(0, step - startStep), params, loss, acc * 100.0, retentionL1 * 100.0, retentionL0 * 100.0);
        }
        if (level == 3) {
            return String.format(Locale.ITALY, "Passi totali: %d · L3 passi: %d · Parametri: %,d · L3 test loss: %.4f · L3 test: %.1f%% · Memoria L2: %.1f%% · L1: %.1f%% · L0: %.1f%%",
                    step, Math.max(0, step - startStep), params, loss, acc * 100.0, retentionL2 * 100.0, retentionL1 * 100.0, retentionL0 * 100.0);
        }
        if (level == 4) {
            return String.format(Locale.ITALY, "Passi totali: %d · L4 passi: %d · Parametri: %,d · L4 test loss: %.4f · L4 test: %.1f%% · Memoria L3: %.1f%% · L2: %.1f%% · L1: %.1f%% · L0: %.1f%%",
                    step, Math.max(0, step - startStep), params, loss, acc * 100.0, retentionL3 * 100.0, retentionL2 * 100.0, retentionL1 * 100.0, retentionL0 * 100.0);
        }
        return String.format(Locale.ITALY, "Passi totali: %d · L5 auto: %d · Parametri: %,d · L5 test loss: %.4f · L5 test: %.1f%% · Memoria L4: %.1f%% · L3: %.1f%% · L2: %.1f%% · L1: %.1f%% · L0: %.1f%%",
                step, Math.max(0, step - startStep), params, loss, acc * 100.0, retentionL4 * 100.0, retentionL3 * 100.0, retentionL2 * 100.0, retentionL1 * 100.0, retentionL0 * 100.0);
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
        File f = evolutionHistoryFile();
        if (f.exists() && f.length() > 0) return;
        File parent = f.getParentFile();
        if (parent != null && !parent.exists()) parent.mkdirs();
        try (FileWriter w = new FileWriter(f, false)) {
            // Solo milestone già verificati sul dispositivo.
            writeEvolutionLine(w, 1700, 95.8, 100.0,
                    evolutionIndex(2, 0.958, 1.0), "Seed 008", "Ha consolidato L2");
            writeEvolutionLine(w, 1760, 100.0, 95.6,
                    evolutionIndex(3, 1.0, 0.956), "Seed 009", "Ha consolidato L3");
            writeEvolutionLine(w, 2960, 98.5, 95.6,
                    evolutionIndex(4, 0.985, 0.956), "Seed 010", "Ha consolidato L4");
        } catch (Exception ignored) {
        }
    }

    private void writeEvolutionLine(FileWriter w, int step, double learning, double memory,
                                    double evolution, String label, String capability) throws Exception {
        String safeLabel = label.replace("\t", " ").replace("\n", " ");
        String safeCap = capability.replace("\t", " ").replace("\n", " ");
        w.write(step + "\t" + String.format(Locale.US, "%.4f", learning) + "\t"
                + String.format(Locale.US, "%.4f", memory) + "\t"
                + String.format(Locale.US, "%.4f", evolution) + "\t"
                + safeLabel + "\t" + safeCap + "\n");
    }

    private synchronized void recordEvolution(JSONObject j, String label, String progress, boolean useTest) {
        try {
            seedEvolutionHistoryIfNeeded();
            int step = j.optInt("step", 0);
            int level = j.optInt("curriculum", nativeCurriculum());
            double learning;
            if (useTest && j.has("test_accuracy")) learning = j.optDouble("test_accuracy", 0.0);
            else learning = j.optDouble("val_accuracy", 0.0);
            double memory = minRetention(j, level);
            double evo = evolutionIndex(level, learning, memory);

            File f = evolutionHistoryFile();
            try (FileWriter w = new FileWriter(f, true)) {
                writeEvolutionLine(w, step, learning * 100.0, memory * 100.0,
                        evo, label, progress);
            }
            getSharedPreferences("motorai_evolution", MODE_PRIVATE).edit()
                    .putString("last_progress", progress).apply();

            final List<EvolutionView.Point> pts = loadEvolutionPoints();
            final JSONObject snapshot = j;
            ui(() -> {
                evolutionView.setPoints(pts);
                updateEvolutionTexts(snapshot, progress, useTest);
            });
        } catch (Exception ignored) {
        }
    }

    private synchronized List<EvolutionView.Point> loadEvolutionPoints() {
        ArrayList<EvolutionView.Point> out = new ArrayList<>();
        File f = evolutionHistoryFile();
        if (!f.exists()) return out;
        try (BufferedReader r = new BufferedReader(new FileReader(f))) {
            String line;
            while ((line = r.readLine()) != null) {
                String[] p = line.split("\\t", 6);
                if (p.length < 5) continue;
                out.add(new EvolutionView.Point(
                        Integer.parseInt(p[0]),
                        Float.parseFloat(p[1]),
                        Float.parseFloat(p[2]),
                        Float.parseFloat(p[3]),
                        p[4]));
            }
        } catch (Exception ignored) {
        }
        return out;
    }

    private String capabilityList(int acceptedLevel) {
        StringBuilder b = new StringBuilder("🧠 Cosa sa fare adesso");
        if (acceptedLevel >= 0) b.append("\n✅ Copiare una sequenza breve");
        if (acceptedLevel >= 1) b.append("\n✅ Invertire una coppia di simboli");
        if (acceptedLevel >= 2) b.append("\n✅ Selezionare e duplicare il primo simbolo");
        if (acceptedLevel >= 3) b.append("\n✅ Selezionare e duplicare il secondo simbolo");
        if (acceptedLevel >= 4) b.append("\n✅ Riconoscere un'uguaglianza strutturale a distanza");
        if (acceptedLevel >= 5) b.append("\n✅ Confrontare i primi due simboli a/b ignorando distrattori a–z nuovi");
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

        evolutionSummary.setText(String.format(Locale.ITALY,
                "Indice Evoluzione: %.1f/100 · Memoria min.: %.1f%% · Parametri: %,d\n(indice interno, non è un QI)",
                evo, memory * 100.0, params));
        capabilityNow.setText(capabilityList(acceptedLevel));

        if (level < 5) {
            learningNow.setText("🔄 Cosa sta imparando: Auto-Training pronto per scegliere il prossimo livello.");
        } else if (!l5Accepted) {
            learningNow.setText("🔄 Cosa sta imparando: riconoscere se i primi due simboli a/b coincidono ignorando distrattori a–z mai visti.");
        } else {
            learningNow.setText("🔄 Cosa sta imparando: L5 consolidato; prossimo curriculum in preparazione.");
        }

        String p = explicitProgress;
        if (p == null || p.isEmpty()) {
            p = getSharedPreferences("motorai_evolution", MODE_PRIVATE)
                    .getString("last_progress", "Seed 010 consolidata: Auto-Training V1 pronto.");
        }
        latestProgress.setText("🎆 Ultimo progresso: " + p);
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
        return String.format(Locale.ITALY, "Dispositivo: %.1f °C · batteria %d%% %s · RAM libera ~%,d MB", temp, level, charging ? "(carica)" : "", freeMb);
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
    }

    @Override protected void onStop() {
        UI_ACTIVE.set(false);
        super.onStop();
        if (training.get()) {
            stopTraining("App in background");
        } else {
            runAsync(() -> {
                rotateAndSaveCheckpoint();
                MotorAIBackgroundJobService.scheduleKick(getApplicationContext());
            });
        }
    }
}
