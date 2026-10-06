package it.motorai.seed;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
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
    public static native void nativeRequestPause();
    public static native boolean nativeSaveCheckpoint(String path);
    public static native boolean nativeLoadCheckpoint(String path);
    public static native void nativeReset();
    public static native String nativeGenerate(String prefix);

    private TextView metrics;
    private TextView device;
    private TextView state;
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
        setTitle("MotorAI Seed 003");

        ScrollView scroll = new ScrollView(this);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(20, 34, 20, 30);
        scroll.addView(root);

        root.addView(text("MotorAI Seed 003", 28, true));
        root.addView(text("Cervello: Transformer causale nativo C++ · pesi iniziali casuali · nessun modello preaddestrato", 15, false));
        root.addView(text("Benchmark corrente: copy3 con TRAIN / VALIDATION / TEST separati", 14, false));

        state = text("Stato: inizializzazione…", 16, true);
        metrics = text("Metriche: —", 15, false);
        device = text("Dispositivo: —", 14, false);
        root.addView(state);
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
        prompt.setHint("Esempio: abc>");
        prompt.setText("abc>");
        root.addView(prompt);
        Button talk = button("💬 Genera");
        root.addView(talk);

        Button reset = button("↺ Riparti da pesi casuali");
        root.addView(reset);

        learn.setOnClickListener(v -> startTraining());
        pause.setOnClickListener(v -> stopTraining("Pausa richiesta"));
        save.setOnClickListener(v -> runAsync(() -> {
            boolean ok = rotateAndSaveCheckpoint();
            ui(() -> state.setText(ok ? "Stato: checkpoint salvato" : "Stato: errore checkpoint"));
        }));
        test.setOnClickListener(v -> refreshMetrics());
        talk.setOnClickListener(v -> runAsync(() -> {
            String out = nativeGenerate(prompt.getText().toString());
            ui(() -> state.setText("MotorAI: " + out.replace("\n", "↵")));
        }));
        reset.setOnClickListener(v -> {
            stopTraining("Reset");
            runAsync(() -> {
                nativeReset();
                deleteTree(currentCheckpoint());
                deleteTree(previousCheckpoint());
                ui(() -> { state.setText("Stato: nuovi pesi casuali inizializzati"); refreshMetrics(); });
            });
        });

        setContentView(scroll);

        runAsync(() -> {
            boolean resumed = currentCheckpoint().exists() && nativeLoadCheckpoint(currentCheckpoint().getAbsolutePath());
            ui(() -> state.setText(resumed ? "Stato: checkpoint ripreso automaticamente" : "Stato: Seed 003 pronta da pesi casuali"));
            refreshMetrics();
        });
    }

    private void startTraining() {
        if (!training.compareAndSet(false, true)) return;
        state.setText("Stato: training in corso…");
        learn.setEnabled(false);
        runAsync(() -> {
            try {
                while (training.get()) {
                    Guard g = readGuard();
                    ui(() -> device.setText(g.description));
                    if (!g.allowed) {
                        training.set(false);
                        ui(() -> state.setText("Stato: training fermato automaticamente — " + g.reason));
                        break;
                    }
                    String result = nativeTrainChunk(20);
                    rotateAndSaveCheckpoint();
                    JSONObject j = new JSONObject(result);
                    String line = formatMetrics(j);
                    ui(() -> metrics.setText(line));
                    if (j.optInt("step", 0) >= 220) {
                        training.set(false);
                        ui(() -> state.setText("Stato: benchmark Seed 003 completato · checkpoint salvato"));
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
        double loss = j.has("test_loss") ? j.optDouble("test_loss") : Double.NaN;
        double acc = j.has("test_accuracy") ? j.optDouble("test_accuracy") : Double.NaN;
        return String.format(Locale.ITALY, "Passi: %d · Parametri: %,d · Test loss: %.4f · Generalizzazione: %.1f%%", step, params, loss, acc * 100.0);
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
