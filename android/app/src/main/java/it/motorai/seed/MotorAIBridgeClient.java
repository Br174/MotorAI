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

public final class MotorAIBridgeClient {
    private static final String BASE = "https://motorai-diagnostic-bridge-rl5jwl.v2.appdeploy.ai";
    private static final String CHANNEL = "motorai-br174-primary-v1";
    private static final String PREFS = "motorai_bridge";

    private MotorAIBridgeClient() {}

    public static JSONObject buildSnapshot(Context context, JSONObject metrics, String status) {
        JSONObject out = new JSONObject();
        try {
            out.put("appVersion", BuildConfig.VERSION_NAME);
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

            SharedPreferences diag = context.getSharedPreferences("motorai_diagnostics", Context.MODE_PRIVATE);
            String failures = diag.getString("failures", "");
            out.put("failures", failures == null ? "" : failures);
            out.put("status", status == null ? "" : status);
            out.put("deviceTimestamp", System.currentTimeMillis());
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
        try {
            SharedPreferences p = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
            String deviceId = p.getString("device_id", "");
            String writeKey = p.getString("write_key", "");

            if (deviceId == null || deviceId.isEmpty() || writeKey == null || writeKey.isEmpty()) {
                JSONObject reg = new JSONObject();
                reg.put("channel", CHANNEL);
                JSONObject response = post("/api/register", reg);
                deviceId = response.optString("deviceId", "");
                writeKey = response.optString("writeKey", "");
                if (deviceId.isEmpty() || writeKey.isEmpty()) {
                    recordBridgeError(context, "Registrazione bridge senza credenziali");
                    return false;
                }
                p.edit().putString("device_id", deviceId).putString("write_key", writeKey).apply();
            }

            JSONObject body = new JSONObject();
            body.put("channel", CHANNEL);
            body.put("deviceId", deviceId);
            body.put("writeKey", writeKey);
            body.put("snapshot", snapshot);

            JSONObject response = post("/api/telemetry", body);
            boolean ok = response.optBoolean("ok", false);
            if (ok) {
                p.edit().putLong("last_upload_ms", System.currentTimeMillis())
                        .remove("last_error")
                        .apply();
                return true;
            }
            recordBridgeError(context, "Bridge: risposta non valida");
        } catch (Exception e) {
            recordBridgeError(context, "Bridge: " + safe(e.getMessage()));
        }
        return false;
    }

    public static long lastUploadMs(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                .getLong("last_upload_ms", 0L);
    }

    private static void recordBridgeError(Context context, String message) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
                .putString("last_error", safe(message))
                .apply();
    }

    private static String safe(String s) {
        if (s == null) return "errore sconosciuto";
        return s.replace('\n', ' ').replace('\r', ' ').substring(0, Math.min(300, s.length()));
    }

    private static JSONObject post(String path, JSONObject body) throws Exception {
        URL url = new URL(BASE + path);
        HttpsURLConnection c = (HttpsURLConnection) url.openConnection();
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
        StringBuilder text = new StringBuilder();
        if (stream != null) {
            try (BufferedReader r = new BufferedReader(new InputStreamReader(stream, StandardCharsets.UTF_8))) {
                String line;
                while ((line = r.readLine()) != null) text.append(line);
            }
        }
        c.disconnect();
        if (code < 200 || code >= 300) throw new IllegalStateException("HTTP " + code + " " + text);
        return new JSONObject(text.toString());
    }
}
