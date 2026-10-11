package it.motorai.seed;

import android.app.job.JobScheduler;
import android.content.Context;
import android.content.SharedPreferences;

/** Emergency LAB is fail-closed: all training stays off until the owner confirms. */
public final class MotorAIRecoveryMode {
    private static final String PREFS="motorai_recovery_safety";
    private MotorAIRecoveryMode() {}
    public static boolean paused(Context c) {
        return c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getBoolean("paused",true);
    }
    public static void pause(Context c) {
        c.getSharedPreferences(PREFS,Context.MODE_PRIVATE).edit()
                .putBoolean("paused",true).commit();
        JobScheduler jobs=(JobScheduler)c.getSystemService(Context.JOB_SCHEDULER_SERVICE);
        if(jobs!=null) { jobs.cancel(11011); jobs.cancel(11012); }
    }
    public static void allowTraining(Context c) {
        c.getSharedPreferences(PREFS,Context.MODE_PRIVATE).edit()
                .putBoolean("paused",false).commit();
    }
}
