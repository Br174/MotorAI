package it.motorai.seed;

import android.content.Context;

import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Persistent, timestamped evolution history.
 *
 * V2 rows:
 * timestamp_ms<TAB>step<TAB>learning%<TAB>memory%<TAB>evolution<TAB>label<TAB>capability
 *
 * Legacy V1 rows without timestamp remain readable and are shown only in "Tutto".
 */
public final class EvolutionHistory {
    private EvolutionHistory() {}

    private static File file(Context context) {
        return new File(context.getFilesDir(), "motorai/evolution.tsv");
    }

    public static synchronized void seedIfNeeded(Context context) {
        File f = file(context);
        if (f.exists() && f.length() > 0) return;
        File parent = f.getParentFile();
        if (parent != null && !parent.exists()) parent.mkdirs();
        try (FileWriter w = new FileWriter(f, false)) {
            write(w, 0L, 1700, 95.8, 100.0,
                    evolutionIndex(2, 0.958, 1.0), "Seed 008", "Ha consolidato L2");
            write(w, 0L, 1760, 100.0, 95.6,
                    evolutionIndex(3, 1.0, 0.956), "Seed 009", "Ha consolidato L3");
            write(w, 0L, 2960, 98.5, 95.6,
                    evolutionIndex(4, 0.985, 0.956), "Seed 010", "Ha consolidato L4");
        } catch (Exception ignored) {
        }
    }

    public static synchronized void record(Context context, JSONObject j, boolean useTest,
                                           String label, String capability) {
        try {
            seedIfNeeded(context);
            int step = j.optInt("step", 0);
            int level = j.optInt("curriculum", MainActivity.nativeCurriculum());
            double learning = useTest && j.has("test_accuracy")
                    ? j.optDouble("test_accuracy", 0.0)
                    : j.optDouble("val_accuracy", 0.0);
            double memory = minRetention(j, level);
            double evo = evolutionIndex(level, learning, memory);
            try (FileWriter w = new FileWriter(file(context), true)) {
                write(w, System.currentTimeMillis(), step, learning * 100.0,
                        memory * 100.0, evo, label, capability);
            }
        } catch (Exception ignored) {
        }
    }

    public static synchronized void recordMiniAi(Context context, int foundationStep, int goalStep,
                                                     double goalPercent, double memoryPercent,
                                                     double totalMiniAiPercent, String label, String capability) {
        try {
            seedIfNeeded(context);
            int syntheticStep = foundationStep + Math.max(0, goalStep);
            try (FileWriter w = new FileWriter(file(context), true)) {
                write(w, System.currentTimeMillis(), syntheticStep,
                        Math.max(0.0, Math.min(100.0, goalPercent)),
                        Math.max(0.0, Math.min(100.0, memoryPercent)),
                        Math.max(0.0, Math.min(100.0, totalMiniAiPercent)),
                        label, capability);
            }
        } catch (Exception ignored) {
        }
    }

    public static synchronized void recordCurrentIfChanged(Context context, JSONObject j, boolean useTest,
                                                           String label, String capability) {
        int step = j.optInt("step", 0);
        if (step <= 0 || lastStep(context) == step) return;
        record(context, j, useTest, label, capability);
    }

    public static synchronized List<EvolutionView.Point> load(Context context, long rangeMs) {
        ArrayList<EvolutionView.Point> out = new ArrayList<>();
        File f = file(context);
        if (!f.exists()) return out;
        long since = rangeMs > 0 ? System.currentTimeMillis() - rangeMs : Long.MIN_VALUE;
        try (BufferedReader r = new BufferedReader(new FileReader(f))) {
            String line;
            while ((line = r.readLine()) != null) {
                String[] p = line.split("\\t", 7);
                if (p.length < 5) continue;

                long timestamp;
                int step;
                float learning;
                float memory;
                float evolution;
                String label;

                if (p.length >= 7) {
                    timestamp = Long.parseLong(p[0]);
                    step = Integer.parseInt(p[1]);
                    learning = Float.parseFloat(p[2]);
                    memory = Float.parseFloat(p[3]);
                    evolution = Float.parseFloat(p[4]);
                    label = p[5];
                } else {
                    // Legacy V1: step, learning, memory, evolution, label, capability.
                    timestamp = 0L;
                    step = Integer.parseInt(p[0]);
                    learning = Float.parseFloat(p[1]);
                    memory = Float.parseFloat(p[2]);
                    evolution = Float.parseFloat(p[3]);
                    label = p[4];
                }

                if (rangeMs > 0 && (timestamp <= 0L || timestamp < since)) continue;
                out.add(new EvolutionView.Point(timestamp, step, learning, memory, evolution, label));
            }
        } catch (Exception ignored) {
        }
        return out;
    }

    public static synchronized int lastStep(Context context) {
        int last = -1;
        File f = file(context);
        if (!f.exists()) return last;
        try (BufferedReader r = new BufferedReader(new FileReader(f))) {
            String line;
            while ((line = r.readLine()) != null) {
                String[] p = line.split("\\t", 7);
                if (p.length >= 7) last = Integer.parseInt(p[1]);
                else if (p.length >= 5) last = Integer.parseInt(p[0]);
            }
        } catch (Exception ignored) {
        }
        return last;
    }

    private static void write(FileWriter w, long timestamp, int step, double learning, double memory,
                              double evolution, String label, String capability) throws Exception {
        String safeLabel = clean(label);
        String safeCapability = clean(capability);
        w.write(timestamp + "\t" + step + "\t"
                + String.format(Locale.US, "%.4f", learning) + "\t"
                + String.format(Locale.US, "%.4f", memory) + "\t"
                + String.format(Locale.US, "%.4f", evolution) + "\t"
                + safeLabel + "\t" + safeCapability + "\n");
    }

    private static String clean(String value) {
        if (value == null) return "";
        return value.replace("\t", " ").replace("\n", " ");
    }

    private static double minRetention(JSONObject j, int level) {
        double min = 1.0;
        if (level >= 1) min = Math.min(min, j.optDouble("retention_l0_accuracy", 1.0));
        if (level >= 2) min = Math.min(min, j.optDouble("retention_l1_accuracy", 1.0));
        if (level >= 3) min = Math.min(min, j.optDouble("retention_l2_accuracy", 1.0));
        if (level >= 4) min = Math.min(min, j.optDouble("retention_l3_accuracy", 1.0));
        if (level >= 5) min = Math.min(min, j.optDouble("retention_l4_accuracy", 1.0));
        return min;
    }

    private static double evolutionIndex(int level, double learning, double memory) {
        double curriculum = Math.min(1.0, (Math.max(0, level) + 1) / 6.0);
        return 100.0 * (0.45 * learning + 0.35 * memory + 0.20 * curriculum);
    }
}
