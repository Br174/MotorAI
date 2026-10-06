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

import java.io.File;
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
    private final AtomicBoolean training = new AtomicBoolean(false);

    private File checkpointRoot() { return new File(getFilesDir(), "motorai/checkpoints"); }
    private File currentCheckpoint() { return new File(checkpointRoot(), "current"); }
    private File previousCheckpoint() { return new File(checkpointRoot(), "previous"); }
    private File tempCheckpoint() { return new File(checkpointRoot(), "tmp"); }

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
        setTitle("MotorAI Seed 010");

        ScrollView scroll = new ScrollView(this);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(20, 34, 20, 30);
        scroll.addView(root);

        root.addView(text("MotorAI Seed 010", 28, true));
        root.addView(text("Cervello: Transformer causale nativo C++ · pesi iniziali casuali · nessun modello preaddestrato", 15, false));
        root.addView(text("Curriculum: L0 copia3 → L1 inversione → L2 duplica primo → L3 duplica secondo → L4 uguaglianza", 14, false));
        root.addView(text("L4: ? + tre simboli + > = uguaglianza strutturale (+ se primo=terzo, - se diversi). Esempio: ?efe> → ?efe>+. Adapter residuale locale.", 14, false));

        state = text("Stato: inizializzazione…", 16, true);
        curriculum = text("Livello: —", 15, true);
        metrics = text("Metriche: —", 15, false);
        device = text("Dispositivo: —", 14, false);
        root.addView(state);
        root.addView(curriculum);
        root.addView(metrics);
        root.addView(device);

        LinearLayout actions = new LinearLayout(this);
        actions.setOrientation(LinearLayout.HORIZONTAL);
        learn = button("▶ Impara");
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
                } else if (resumed) {
                    ui(() -> state.setText("Stato: checkpoint ripreso automaticamente"));
                } else {
                    ui(() -> state.setText("Stato: Seed 010 pronta da pesi casuali"));
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

                    if (!rotateAndSaveCheckpoint()) {
                        training.set(false);
                        ui(() -> state.setText("Stato: impossibile creare checkpoint di sicurezza"));
                        break;
                    }

                    JSONObject j = new JSONObject(nativeTrainChunk(20));
                    double afterValLoss = j.optDouble("val_loss", Double.POSITIVE_INFINITY);
                    double afterValAcc = j.optDouble("val_accuracy", 0.0);
                    double afterRetentionL0 = j.optDouble("retention_l0_accuracy", 1.0);
                    double afterRetentionL1 = j.optDouble("retention_l1_accuracy", 1.0);
                    double afterRetentionL2 = j.optDouble("retention_l2_accuracy", 1.0);
                    double afterRetentionL3 = j.optDouble("retention_l3_accuracy", 1.0);
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

                    if (numericFailure || clearRegression || forgetL0 || forgetL1 || forgetL2 || forgetL3) {
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
                            && (level < 4 || afterRetentionL3 >= 0.90);
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
                training.set(false);
                ui(() -> state.setText("Stato: errore training — " + e.getMessage()));
            } finally {
                ui(() -> learn.setEnabled(true));
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
        int startStep = j.optInt("curriculum_start_step", step);
        final String levelText = level <= 0 ? "Livello 0 · copy3" :
                (level == 1 ? "Livello 1 · inversione" :
                (level == 2 ? "Livello 2 · duplica primo" :
                (level == 3 ? "Livello 3 · duplica secondo" : "Livello 4 · uguaglianza")));
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
        return String.format(Locale.ITALY,
                "Passi totali: %d · L4 passi: %d · Val.: %.1f%% · Memoria L3: %.1f%% · L2: %.1f%% · L1: %.1f%% · L0: %.1f%%",
                step, Math.max(0, step - startStep), acc * 100.0, r3 * 100.0, r2 * 100.0, r1 * 100.0, r0 * 100.0);
    }

    private void stopTraining(String why) {
        training.set(false);
        nativeRequestPause();
        state.setText("Stato: " + why + " · salvo al primo punto sicuro");
    }

    private void refreshMetrics() {
        runAsync(() -> {
            try {
                JSONObject j = new JSONObject(nativeEvaluate());
                String line = formatMetrics(j);
                Guard g = readGuard();
                ui(() -> { metrics.setText(line); device.setText(g.description); });
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
        int startStep = j.optInt("curriculum_start_step", step);
        final String levelText = level <= 0 ? "Livello 0 · copy3" :
                (level == 1 ? "Livello 1 · inversione" :
                (level == 2 ? "Livello 2 · duplica primo" :
                (level == 3 ? "Livello 3 · duplica secondo" : "Livello 4 · uguaglianza")));
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
        return String.format(Locale.ITALY, "Passi totali: %d · L4 passi: %d · Parametri: %,d · L4 test loss: %.4f · L4 test: %.1f%% · Memoria L3: %.1f%% · L2: %.1f%% · L1: %.1f%% · L0: %.1f%%",
                step, Math.max(0, step - startStep), params, loss, acc * 100.0, retentionL3 * 100.0, retentionL2 * 100.0, retentionL1 * 100.0, retentionL0 * 100.0);
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

    @Override protected void onStop() {
        super.onStop();
        if (training.get()) stopTraining("App in background");
        else runAsync(this::rotateAndSaveCheckpoint);
    }
}
