package it.motorai.seed;

import android.content.Context;
import android.content.SharedPreferences;

/** Persistent single-owner recovery policy for Goal 3, shared by UI and JobService.
 * A rollback must not replay the same deterministic learning step at the same rate.
 * This is not an acceptance shortcut; all existing validation and test gates remain.
 */
public final class Goal3Recovery {
    private static final String PREFS = "motorai_runtime";
    private static final String KEY = "goal3_recovery_attempts";
    private static final float[] RATES = {0.04f, 0.02f, 0.01f, 0.005f, 0.0025f};
    public static final int MAX_RECOVERIES = RATES.length;
    private Goal3Recovery() {}

    private static SharedPreferences prefs(Context c) {
        return c.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    public static boolean canTrain(Context c) {
        return prefs(c).getInt(KEY, 0) < MAX_RECOVERIES;
    }

    public static float learningRate(Context c) {
        int retries = Math.max(0, Math.min(MAX_RECOVERIES - 1, prefs(c).getInt(KEY, 0)));
        return RATES[retries];
    }

    public static int recordRegression(Context c) {
        SharedPreferences p = prefs(c);
        int next = Math.min(MAX_RECOVERIES, Math.max(0, p.getInt(KEY, 0)) + 1);
        p.edit().putInt(KEY, next).apply();
        return next;
    }

    public static void clear(Context c) {
        prefs(c).edit().remove(KEY).apply();
    }
}
