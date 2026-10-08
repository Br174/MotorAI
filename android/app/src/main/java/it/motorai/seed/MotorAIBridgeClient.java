package it.motorai.seed;

import android.app.ActivityManager;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.os.BatteryManager;

import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.File;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.URL;
import java.nio.charset.StandardCharsets;

import javax.net.ssl.HttpsURLConnection;

/**
 * Diagnostic Bridge V1.
 *
 * Transport: PostHog EU public capture API.
 * The project token is explicitly client-safe; no personal API key or GitHub token is stored here.
 * Only technical MotorAI diagnostics are sent.
 */
public final class MotorAIBridgeClient {
    private static final String CAPTURE_URL = "https://eu.i.posthog.com/i/v0/e";
    private static final String PROJECT_TOKEN = "phc_qcq4jwDmBiNJhThhMatbRpYS9XB7p9mFKC9maCJ969DW";
    private static final String DISTINCT_ID = "motorai-primary-device";
    private static final String EVENT = "motorai_diagnostic_snapshot";
    private static final String PREFS = "motorai_bridge";

    private MotorAIBridgeClient() {}

    public static JSONObject buildSnapshot(Context context, JSONObject metrics, String status) {
        JSONObject out = new JSONObject();
        try {
            String version = "Seed011";
            try {
                String installed = context.getPackageManager()
                        .getPackageInfo(context.getPackageName(), 0).versionName;
                if (installed != null && !installed.isEmpty()) version = installed;
            } catch (Exception ignored) {
            }
            out.put("appVersion", version);
            copy(metrics, out, "seed", "seed");
            copy(metrics, out, "step", "step");
            copy(metrics, out, "curriculum", "curriculum");
            copy(metrics, out, "curriculum_start_step", "curriculumStartStep");
            copy(metrics, out, "parameters", "parameters");
            copy(metrics, out, "val_accuracy", "validationAccuracy");
            copy(metrics, out, "val_loss", "validationLoss");
            copy(metrics, out, "test_accuracy", "testAccuracy");
            copy(metrics, out, "retention_l0_accuracy", "retentionL0");
            copy(metrics, out, "retention_l1_accuracy", "retentionL1");
            copy(metrics, out, "retention_l2_accuracy", "retentionL2");
            copy(metrics, out, "retention_l3_accuracy", "retentionL3");
            copy(metrics, out, "retention_l4_accuracy", "retentionL4");

            Intent battery = context.registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
            int temp10 = battery != null ? battery.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, 0) : 0;
            int pct = battery != null ? battery.getIntExtra(BatteryManager.EXTRA_LEVEL, -1) : -1;
            int bs = battery != null ? battery.getIntExtra(BatteryManager.EXTRA_STATUS, -1) : -1;
            boolean charging = bs == BatteryManager.BATTERY_STATUS_CHARGING || bs == BatteryManager.BATTERY_STATUS_FULL;

            ActivityManager am = (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
            ActivityManager.MemoryInfo mi = new ActivityManager.MemoryInfo();
            am.getMemoryInfo(mi);

            out.put("temperatureC", temp10 / 10.0);
            out.put("batteryPct", pct);
            out.put("charging", charging);
            out.put("ramFreeMb", mi.availMem / (1024L * 1024L));

            File root = new File(context.getFilesDir(), "motorai/checkpoints");
            out.put("checkpointCurrent", new File(root, "current").exists());
            out.put("checkpointPrevious", new File(root, "previous").exists());
            out.put("checkpointBaseline", new File(root, "autotrain-baseline").exists());

            SharedPreferences evo = context.getSharedPreferences("motorai_evolution", Context.MODE_PRIVATE);
            out.put("l5Accepted", evo.getBoolean("l5_accepted", false));
            out.put("lastProgress", evo.getString("last_progress", "nessuno"));

            MiniAiGoals.seedIfNeeded(context);
            int activeGoal = MiniAiGoals.activeGoalIndex(context);
            out.put("miniAiGoal1Percent", MiniAiGoals.percent(context, 0));
            out.put("miniAiGoal2Percent", MiniAiGoals.percent(context, 1));
            out.put("miniAiGoal3Percent", MiniAiGoals.percent(context, 2));
            out.put("miniAiGoal4Percent", MiniAiGoals.percent(context, 3));
            out.put("miniAiGoal5Percent", MiniAiGoals.percent(context, 4));
            out.put("miniAiGoal6Percent", MiniAiGoals.percent(context, 5));
            out.put("miniAiTotalPercent", MiniAiGoals.totalPercent(context));
            out.put("miniAiCompletedGoals", MiniAiGoals.completedCount(context));
            out.put("miniAiActiveGoal", activeGoal + 1);
            out.put("miniAiActiveGoalPercent", MiniAiGoals.percent(context, activeGoal));
            out.put("miniAiActiveGoalStatus", MiniAiGoals.status(context, activeGoal));
            out.put("miniAiActiveGoalEvidence", MiniAiGoals.evidence(context, activeGoal));
            out.put("miniAiTrainingState", MiniAiTrainingStatus.state(context));
            out.put("miniAiTrainingStep", MiniAiTrainingStatus.step(context));
            out.put("miniAiTrainingValidation", MiniAiTrainingStatus.validation(context));

            SharedPreferences diag = context.getSharedPreferences("motorai_diagnostics", Context.MODE_PRIVATE);
            String failures = diag.getString("failures", "");
            out.put("failures", failures == null ? "" : failures);
            out.put("status", status == null ? "" : status);
            out.put("deviceTimestamp", System.currentTimeMillis());
            out.put("bridgeVersion", 7);
            out.put("$process_person_profile", false);
        } catch (Exception ignored) {
        }
        return out;
    }

    private static void copy(JSONObject from, JSONObject to, String source, String target) {
        if (from.has(source) && !from.isNull(source)) {
            try { to.put(target, from.get(source)); } catch (Exception ignored) {}
        }
    }

    public static boolean sendSnapshot(Context context, JSONObject snapshot) {
        HttpsURLConnection c = null;
        try {
            JSONObject body = new JSONObject();
            body.put("api_key", PROJECT_TOKEN);
            body.put("distinct_id", DISTINCT_ID);
            body.put("event", EVENT);
            body.put("properties", snapshot);

            URL url = new URL(CAPTURE_URL);
            c = (HttpsURLConnection) url.openConnection();
            c.setRequestMethod("POST");
            c.setConnectTimeout(10000);
            c.setReadTimeout(10000);
            c.setDoOutput(true);
            c.setRequestProperty("Content-Type", "application/json; charset=utf-8");

            byte[] bytes = body.toString().getBytes(StandardCharsets.UTF_8);
            c.setFixedLengthStreamingMode(bytes.length);
            try (OutputStream os = c.getOutputStream()) {
                os.write(bytes);
            }

            int code = c.getResponseCode();
            InputStream stream = code >= 200 && code < 300 ? c.getInputStream() : c.getErrorStream();
            StringBuilder response = new StringBuilder();
            if (stream != null) {
                try (BufferedReader r = new BufferedReader(new InputStreamReader(stream, StandardCharsets.UTF_8))) {
                    String line;
                    while ((line = r.readLine()) != null) response.append(line);
                }
            }

            SharedPreferences p = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
            if (code >= 200 && code < 300) {
                p.edit()
                        .putLong("last_upload_ms", System.currentTimeMillis())
                        .remove("last_error")
                        .apply();
                return true;
            }

            recordBridgeError(context, "PostHog HTTP " + code + " " + response);
        } catch (Exception e) {
            recordBridgeError(context, "PostHog: " + safe(e.getMessage()));
        } finally {
            if (c != null) c.disconnect();
        }
        return false;
    }

    public static long lastUploadMs(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                .getLong("last_upload_ms", 0L);
    }

    public static String lastError(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                .getString("last_error", "");
    }

    private static void recordBridgeError(Context context, String message) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
                .putString("last_error", safe(message))
                .apply();
    }

    private static String safe(String s) {
        if (s == null) return "errore sconosciuto";
        String v = s.replace('\n', ' ').replace('\r', ' ');
        return v.substring(0, Math.min(300, v.length()));
    }
}
