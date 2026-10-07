package it.motorai.seed;

import android.app.ActivityManager;
import android.app.job.JobInfo;
import android.app.job.JobParameters;
import android.app.job.JobScheduler;
import android.app.job.JobService;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.os.BatteryManager;

import org.json.JSONObject;

import java.io.File;
import java.util.Locale;

public class MotorAIBackgroundJobService extends JobService {
    private static final int PERIODIC_JOB_ID = 11011;
    private static final int KICK_JOB_ID = 11012;
    private static final long PERIOD_MS = 20L * 60L * 1000L;
    private static final long KICK_DELAY_MS = 5_000L;
    private static final long MAX_WAKE_MS = 120_000L;
    private static final int MAX_CHUNKS_PER_WAKE = 8;
    private volatile Thread worker;

    public static void schedule(Context context) {
        try {
            JobScheduler scheduler = (JobScheduler) context.getSystemService(Context.JOB_SCHEDULER_SERVICE);
            if (scheduler == null) return;
            JobInfo info = new JobInfo.Builder(PERIODIC_JOB_ID,
                    new ComponentName(context, MotorAIBackgroundJobService.class))
                    .setPeriodic(PERIOD_MS)
                    .setPersisted(true)
                    .setRequiresCharging(true)
                    .build();
            scheduler.schedule(info);
            context.getSharedPreferences("motorai_background", Context.MODE_PRIVATE)
                    .edit().putBoolean("enabled", true).apply();
        } catch (Exception ignored) {
        }
    }

    public static void scheduleKick(Context context) {
        try {
            JobScheduler scheduler = (JobScheduler) context.getSystemService(Context.JOB_SCHEDULER_SERVICE);
            if (scheduler == null) return;
            JobInfo info = new JobInfo.Builder(KICK_JOB_ID,
                    new ComponentName(context, MotorAIBackgroundJobService.class))
                    .setMinimumLatency(KICK_DELAY_MS)
                    .setPersisted(true)
                    .setRequiresCharging(true)
                    .build();
            scheduler.schedule(info);
            context.getSharedPreferences("motorai_background", Context.MODE_PRIVATE)
                    .edit().putLong("last_kick_scheduled_ms", System.currentTimeMillis()).apply();
        } catch (Exception ignored) {
        }
    }

    @Override public boolean onStartJob(JobParameters params) {
        worker = new Thread(() -> {
            boolean reschedule = false;
            boolean continueSoon = false;
            try {
                continueSoon = runOneCycle();
            } catch (Throwable e) {
                appendFailure("Background Auto-Training: " + safe(e.getMessage()));
                reschedule = true;
            } finally {
                jobFinished(params, reschedule);
                if (!reschedule && continueSoon && !MainActivity.isUiActive()) {
                    scheduleKick(getApplicationContext());
                }
            }
        }, "MotorAI-Background");
        worker.start();
        return true;
    }

    @Override public boolean onStopJob(JobParameters params) {
        try { MainActivity.nativeRequestPause(); } catch (Throwable ignored) {}
        return true;
    }

    private boolean runOneCycle() throws Exception {
        SharedPreferences runtime = getSharedPreferences("motorai_runtime", MODE_PRIVATE);
        if (MainActivity.isUiActive()) {
            sendCurrentTelemetry("background_skip_ui_active");
            return false;
        }

        File current = checkpoint("current");
        if (!current.exists() || !MainActivity.nativeLoadCheckpoint(current.getAbsolutePath())) {
            appendFailure("Background Auto-Training: checkpoint current non disponibile");
            return false;
        }

        Guard guard = readGuard();
        if (!guard.allowed) {
            sendCurrentTelemetry("background_pause_" + guard.reason);
            return false;
        }

        SharedPreferences evo = getSharedPreferences("motorai_evolution", MODE_PRIVATE);
        boolean l5Accepted = evo.getBoolean("l5_accepted", false);
        int level = MainActivity.nativeCurriculum();

        if (level < 4) {
            sendCurrentTelemetry("background_wait_baseline_L4");
            return false;
        }

        if (level == 4 && !l5Accepted) {
            JSONObject base = new JSONObject(MainActivity.nativeTrainingEvaluate());
            boolean baseOk = base.optDouble("val_accuracy", 0.0) >= 0.95
                    && base.optDouble("retention_l0_accuracy", 0.0) >= 0.90
                    && base.optDouble("retention_l1_accuracy", 0.0) >= 0.90
                    && base.optDouble("retention_l2_accuracy", 0.0) >= 0.90
                    && base.optDouble("retention_l3_accuracy", 0.0) >= 0.90;
            if (!baseOk) {
                appendFailure("Background Auto-Training: baseline L4 sotto soglia");
                MotorAIBridgeClient.sendSnapshot(this,
                        MotorAIBridgeClient.buildSnapshot(this, base, "baseline_L4_sotto_soglia"));
                return false;
            }

            File baseline = checkpoint("autotrain-baseline");
            deleteTree(baseline);
            if (!MainActivity.nativeSaveCheckpoint(baseline.getAbsolutePath())) {
                appendFailure("Background Auto-Training: impossibile salvare autotrain-baseline");
                return false;
            }

            MainActivity.nativeSetCurriculum(5);
            if (!rotateAndSave()) {
                appendFailure("Background Auto-Training: checkpoint iniziale L5 fallito");
                MainActivity.nativeLoadCheckpoint(baseline.getAbsolutePath());
                rotateAndSave();
                return false;
            }
            runtime.edit().putInt("l5_stable_passes", 0).apply();
            appendProgress("Auto-Training background: L5 avviato automaticamente");
            level = 5;
        }

        if (level >= 5 && !l5Accepted) {
            long wakeStarted = System.currentTimeMillis();
            for (int chunk = 0; chunk < MAX_CHUNKS_PER_WAKE; chunk++) {
                if (MainActivity.isUiActive()) {
                    sendCurrentTelemetry("background_pause_ui_active");
                    return false;
                }

                Guard chunkGuard = readGuard();
                if (!chunkGuard.allowed) {
                    sendCurrentTelemetry("background_pause_" + chunkGuard.reason);
                    return false;
                }

                if (!runLevel5Chunk(runtime, evo)) {
                    return false;
                }

                if (evo.getBoolean("l5_accepted", false)) {
                    return false;
                }
                if (MainActivity.nativeCurriculum() < 5) {
                    return false;
                }
                if (System.currentTimeMillis() - wakeStarted >= MAX_WAKE_MS) {
                    break;
                }
            }

            sendCurrentTelemetry("background_burst_checkpoint");
            return !MainActivity.isUiActive()
                    && !evo.getBoolean("l5_accepted", false)
                    && MainActivity.nativeCurriculum() >= 5
                    && readGuard().allowed;
        }

        if (level >= 5 && l5Accepted && MiniAiGoals.completedCount(this) < 10) {
            long wakeStarted = System.currentTimeMillis();
            for (int chunk = 0; chunk < MAX_CHUNKS_PER_WAKE; chunk++) {
                if (MainActivity.isUiActive()) {
                    sendCurrentTelemetry("background_miniai_pause_lab_ui_active");
                    return false;
                }
                Guard chunkGuard = readGuard();
                if (!chunkGuard.allowed) {
                    int active = MiniAiGoals.activeGoalIndex(this);
                    MiniAiTrainingCoordinator.paused(this, active,
                            MiniAiGoals.percent(this, active), 0, 0.0, chunkGuard.reason);
                    sendCurrentTelemetry("background_miniai_pause_" + chunkGuard.reason);
                    return false;
                }

                int active = MiniAiGoals.activeGoalIndex(this);
                boolean continuePath;
                if (active == 0) {
                    continuePath = runGoal1Chunk(runtime);
                } else if (active == 1) {
                    continuePath = runGoal2Chunk(runtime);
                } else if (active == 2) {
                    continuePath = runGoal3Chunk(runtime);
                } else {
                    MiniAiTrainingCoordinator.paused(this, active,
                            MiniAiGoals.percent(this, active), 0, 0.0,
                            "Coordinatore pronto: modulo reale Obiettivo "
                                    + (active + 1) + "/10 non ancora installato.");
                    sendCurrentTelemetry("background_miniai_wait_goal_" + (active + 1));
                    return false;
                }

                if (!continuePath) return false;
                if (MiniAiGoals.completedCount(this) >= 10) return false;
                if (System.currentTimeMillis() - wakeStarted >= MAX_WAKE_MS) break;
            }

            int active = MiniAiGoals.activeGoalIndex(this);
            sendCurrentTelemetry("background_miniai_burst_goal_" + (active + 1));
            return !MainActivity.isUiActive()
                    && MiniAiGoals.completedCount(this) < 10
                    && readGuard().allowed;
        }

        sendCurrentTelemetry("background_idle_consolidated");
        return false;
    }

    private int goal1Percent(double accuracy) {
        final double chance = 0.20;
        if (!Double.isFinite(accuracy) || accuracy <= chance) return 0;
        return Math.max(0, Math.min(99,
                (int)Math.round(((accuracy - chance) / (1.0 - chance)) * 99.0)));
    }

    private boolean runGoal1Chunk(SharedPreferences runtime) throws Exception {
        JSONObject before = new JSONObject(MainActivity.nativeGoal1Evaluate());
        double beforeAcc = before.optDouble("validation_accuracy", 0.0);

        if (!rotateAndSave()) {
            appendFailure("Background Goal1: checkpoint pre-chunk non disponibile");
            return false;
        }

        JSONObject after = new JSONObject(MainActivity.nativeGoal1TrainChunk(20));
        double validation = after.optDouble("validation_accuracy", 0.0);
        int goalStep = after.optInt("goal1_step", 0);

        if (validation + 0.20 < beforeAcc) {
            MainActivity.nativeLoadCheckpoint(checkpoint("current").getAbsolutePath());
            runtime.edit().putInt("goal1_stable_passes", 0).apply();
            appendFailure("Background Goal1: regressione forte · rollback automatico");
            sendCurrentTelemetry("background_goal1_rollback_regression");
            return false;
        }

        if (!rotateAndSave()) {
            MainActivity.nativeLoadCheckpoint(checkpoint("current").getAbsolutePath());
            appendFailure("Background Goal1: salvataggio post-chunk fallito");
            return false;
        }

        int measured = goal1Percent(validation);
        int best = Math.max(MiniAiGoals.percent(this, 0), measured);
        String evidence = String.format(Locale.ITALY,
                "Validation %.1f%% · step obiettivo %d", validation * 100.0, goalStep);
        MiniAiTrainingCoordinator.progress(this, 0, best, goalStep, validation,
                evidence, "Sta imparando a capire il tipo di richiesta.");

        JSONObject foundation = new JSONObject(MainActivity.nativeEvaluate());
        double memory = min(retentions(foundation)) * 100.0;
        int foundationStep = foundation.optInt("step", 3100);
        EvolutionHistory.recordMiniAi(this, foundationStep, goalStep,
                best, memory, MiniAiGoals.totalPercent(this),
                "Mini-AI 1 · " + best + "%",
                "Comprensione di richieste semplici in italiano");

        appendProgress("Mini-AI 1/10 · " + best + "% · step obiettivo " + goalStep);

        int stable = validation >= 0.90
                ? runtime.getInt("goal1_stable_passes", 0) + 1 : 0;
        runtime.edit().putInt("goal1_stable_passes", stable).apply();

        if (stable >= 4) {
            JSONObject fin = new JSONObject(MainActivity.nativeGoal1FinalTest());
            double test = fin.optDouble("test_accuracy", 0.0);
            if (test >= 0.90) {
                String finalEvidence = String.format(Locale.ITALY,
                        "TEST separato superato: %.1f%% · step obiettivo %d",
                        test * 100.0, goalStep);
                MiniAiTrainingCoordinator.completed(this, 0, goalStep, test, finalEvidence);
                rotateAndSave();
                EvolutionHistory.recordMiniAi(this, foundationStep, goalStep,
                        100.0, memory, MiniAiGoals.totalPercent(this),
                        "Mini-AI 1 completato",
                        "TEST separato superato: comprende l'intento di richieste semplici.");
                runtime.edit().putInt("goal1_stable_passes", 0).apply();
                appendProgress(String.format(Locale.ITALY,
                        "Mini-AI 1/10 completato · TEST %.1f%% · passo automatico al 2/10",
                        test * 100.0));
                sendCurrentTelemetry("background_miniai_goal1_completed");
                return true;
            }
            runtime.edit().putInt("goal1_stable_passes", 0).apply();
        }

        if (goalStep >= 2500) {
            appendFailure("Background Goal1: limite di sicurezza 2500 step raggiunto");
            sendCurrentTelemetry("background_goal1_safety_limit");
            return false;
        }

        sendCurrentTelemetry("background_training_miniai_goal1");
        return true;
    }

    private int goal2Percent(double accuracy) {
        final double chance = 1.0 / 32.0;
        final double target = 0.60;
        if (!Double.isFinite(accuracy) || accuracy <= chance) return 0;
        return Math.max(0, Math.min(99,
                (int)Math.round(((accuracy - chance) / (target - chance)) * 99.0)));
    }

    private boolean runGoal2Chunk(SharedPreferences runtime) throws Exception {
        JSONObject before = new JSONObject(MainActivity.nativeGoal2Evaluate());
        double beforeAcc = before.optDouble("validation_accuracy", 0.0);
        int currentPercent = MiniAiGoals.percent(this, 1);

        if (!rotateAndSave()) {
            MiniAiTrainingCoordinator.error(this, 1, currentPercent, 0, beforeAcc,
                    "Checkpoint pre-chunk non disponibile.");
            appendFailure("Background Goal2: checkpoint pre-chunk non disponibile");
            return false;
        }

        JSONObject after = new JSONObject(MainActivity.nativeGoal2TrainChunk(20));
        double validation = after.optDouble("validation_accuracy", 0.0);
        int goalStep = after.optInt("goal2_step", 0);

        if (validation + 0.15 < beforeAcc) {
            MainActivity.nativeLoadCheckpoint(checkpoint("current").getAbsolutePath());
            runtime.edit().putInt("goal2_stable_passes", 0).apply();
            MiniAiTrainingCoordinator.error(this, 1, currentPercent, goalStep, validation,
                    "Regressione rilevata: rollback automatico.");
            appendFailure("Background Goal2: regressione forte · rollback automatico");
            sendCurrentTelemetry("background_goal2_rollback_regression");
            return false;
        }

        if (!rotateAndSave()) {
            MainActivity.nativeLoadCheckpoint(checkpoint("current").getAbsolutePath());
            MiniAiTrainingCoordinator.error(this, 1, currentPercent, goalStep, validation,
                    "Salvataggio post-chunk fallito.");
            appendFailure("Background Goal2: salvataggio post-chunk fallito");
            return false;
        }

        int measured = goal2Percent(validation);
        int best = Math.max(currentPercent, measured);
        String evidence = String.format(Locale.ITALY,
                "Validation linguistica %.1f%% · step obiettivo %d",
                validation * 100.0, goalStep);
        MiniAiTrainingCoordinator.progress(this, 1, best, goalStep, validation,
                evidence, "Sta imparando a formulare una breve risposta naturale.");

        JSONObject foundation = new JSONObject(MainActivity.nativeEvaluate());
        double memory = min(retentions(foundation)) * 100.0;
        int foundationStep = foundation.optInt("step", 3100);
        EvolutionHistory.recordMiniAi(this, foundationStep, goalStep,
                best, memory, MiniAiGoals.totalPercent(this),
                "Mini-AI 2 · " + best + "%",
                "Generazione di brevi risposte naturali.");

        appendProgress("Mini-AI 2/10 · " + best + "% · step obiettivo " + goalStep);

        int stable = validation >= 0.60
                ? runtime.getInt("goal2_stable_passes", 0) + 1 : 0;
        runtime.edit().putInt("goal2_stable_passes", stable).apply();

        if (stable >= 4) {
            MiniAiTrainingCoordinator.testing(this, 1, best, goalStep, validation,
                    "TEST finale separato in corso.");
            JSONObject fin = new JSONObject(MainActivity.nativeGoal2FinalTest());
            double test = fin.optDouble("test_accuracy", 0.0);
            if (test >= 0.55) {
                String finalEvidence = String.format(Locale.ITALY,
                        "TEST separato superato: %.1f%% · step obiettivo %d",
                        test * 100.0, goalStep);
                MiniAiTrainingCoordinator.completed(this, 1, goalStep, test, finalEvidence);
                rotateAndSave();
                EvolutionHistory.recordMiniAi(this, foundationStep, goalStep,
                        100.0, memory, MiniAiGoals.totalPercent(this),
                        "Mini-AI 2 completato",
                        "TEST separato superato: genera una risposta breve coerente con l'intento.");
                runtime.edit().putInt("goal2_stable_passes", 0).apply();
                appendProgress(String.format(Locale.ITALY,
                        "Mini-AI 2/10 completato · TEST %.1f%% · passo automatico al 3/10",
                        test * 100.0));
                sendCurrentTelemetry("background_miniai_goal2_completed");
                return true;
            }
            runtime.edit().putInt("goal2_stable_passes", 0).apply();
        }

        if (goalStep >= 3000) {
            MiniAiTrainingCoordinator.paused(this, 1, best, goalStep, validation,
                    "Limite di sicurezza 3000 step raggiunto.");
            appendFailure("Background Goal2: limite di sicurezza 3000 step raggiunto");
            sendCurrentTelemetry("background_goal2_safety_limit");
            return false;
        }

        sendCurrentTelemetry("background_training_miniai_goal2");
        return true;
    }

    private int goal3Percent(double accuracy) {
        final double chance = 1.0 / 9.0;
        final double target = 0.90;
        if (!Double.isFinite(accuracy) || accuracy <= chance) return 0;
        return Math.max(0, Math.min(99,
                (int)Math.round(((accuracy - chance) / (target - chance)) * 99.0)));
    }

    private boolean runGoal3Chunk(SharedPreferences runtime) throws Exception {
        JSONObject before = new JSONObject(MainActivity.nativeGoal3Evaluate());
        double beforeAcc = before.optDouble("validation_accuracy", 0.0);
        int currentPercent = MiniAiGoals.percent(this, 2);

        if (!rotateAndSave()) {
            MiniAiTrainingCoordinator.error(this, 2, currentPercent, 0, beforeAcc,
                    "Checkpoint pre-chunk non disponibile.");
            appendFailure("Background Goal3: checkpoint pre-chunk non disponibile");
            return false;
        }

        JSONObject after = new JSONObject(MainActivity.nativeGoal3TrainChunk(20));
        double validation = after.optDouble("validation_accuracy", 0.0);
        int goalStep = after.optInt("goal3_step", 0);

        if (validation + 0.20 < beforeAcc) {
            MainActivity.nativeLoadCheckpoint(checkpoint("current").getAbsolutePath());
            runtime.edit().putInt("goal3_stable_passes", 0).apply();
            MiniAiTrainingCoordinator.error(this, 2, currentPercent, goalStep, validation,
                    "Regressione rilevata: rollback automatico.");
            appendFailure("Background Goal3: regressione forte · rollback automatico");
            sendCurrentTelemetry("background_goal3_rollback_regression");
            return false;
        }

        if (!rotateAndSave()) {
            MainActivity.nativeLoadCheckpoint(checkpoint("current").getAbsolutePath());
            MiniAiTrainingCoordinator.error(this, 2, currentPercent, goalStep, validation,
                    "Salvataggio post-chunk fallito.");
            appendFailure("Background Goal3: salvataggio post-chunk fallito");
            return false;
        }

        int measured = goal3Percent(validation);
        int best = Math.max(currentPercent, measured);
        String evidence = String.format(Locale.ITALY,
                "Validation memoria %.1f%% · step obiettivo %d",
                validation * 100.0, goalStep);
        MiniAiTrainingCoordinator.progress(this, 2, best, goalStep, validation,
                evidence, "Sta imparando a riconoscere cosa ricordare e cosa recuperare.");

        JSONObject foundation = new JSONObject(MainActivity.nativeEvaluate());
        double memory = min(retentions(foundation)) * 100.0;
        int foundationStep = foundation.optInt("step", 3100);
        EvolutionHistory.recordMiniAi(this, foundationStep, goalStep,
                best, memory, MiniAiGoals.totalPercent(this),
                "Mini-AI 3 · " + best + "%",
                "Memoria conversazionale persistente.");

        appendProgress("Mini-AI 3/10 · " + best + "% · step obiettivo " + goalStep);

        int stable = validation >= 0.90
                ? runtime.getInt("goal3_stable_passes", 0) + 1 : 0;
        runtime.edit().putInt("goal3_stable_passes", stable).apply();

        if (stable >= 4) {
            MiniAiTrainingCoordinator.testing(this, 2, best, goalStep, validation,
                    "TEST finale separato in corso.");
            JSONObject fin = new JSONObject(MainActivity.nativeGoal3FinalTest());
            double test = fin.optDouble("test_accuracy", 0.0);
            if (test >= 0.90) {
                String finalEvidence = String.format(Locale.ITALY,
                        "TEST memoria separato superato: %.1f%% · step obiettivo %d",
                        test * 100.0, goalStep);
                MiniAiTrainingCoordinator.completed(this, 2, goalStep, test, finalEvidence);
                rotateAndSave();
                EvolutionHistory.recordMiniAi(this, foundationStep, goalStep,
                        100.0, memory, MiniAiGoals.totalPercent(this),
                        "Mini-AI 3 completato",
                        "TEST separato superato: riconosce salvataggio e recupero del contesto.");
                runtime.edit().putInt("goal3_stable_passes", 0).apply();
                appendProgress(String.format(Locale.ITALY,
                        "Mini-AI 3/10 completato · TEST %.1f%% · passo automatico al 4/10",
                        test * 100.0));
                sendCurrentTelemetry("background_miniai_goal3_completed");
                return true;
            }
            runtime.edit().putInt("goal3_stable_passes", 0).apply();
        }

        if (goalStep >= 2500) {
            MiniAiTrainingCoordinator.paused(this, 2, best, goalStep, validation,
                    "Limite di sicurezza 2500 step raggiunto.");
            appendFailure("Background Goal3: limite di sicurezza 2500 step raggiunto");
            sendCurrentTelemetry("background_goal3_safety_limit");
            return false;
        }

        sendCurrentTelemetry("background_training_miniai_goal3");
        return true;
    }

    private boolean runLevel5Chunk(SharedPreferences runtime, SharedPreferences evo) throws Exception {
        JSONObject before = new JSONObject(MainActivity.nativeTrainingEvaluate());
        double beforeLoss = before.optDouble("val_loss", Double.POSITIVE_INFINITY);
        double beforeAcc = before.optDouble("val_accuracy", 0.0);
        double[] beforeR = retentions(before);

        if (!rotateAndSave()) {
            appendFailure("Background Auto-Training: checkpoint pre-chunk non disponibile");
            return false;
        }

        JSONObject after = new JSONObject(MainActivity.nativeTrainChunk(20));
        double loss = after.optDouble("val_loss", Double.POSITIVE_INFINITY);
        double acc = after.optDouble("val_accuracy", 0.0);
        double[] r = retentions(after);

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
            MainActivity.nativeLoadCheckpoint(checkpoint("current").getAbsolutePath());
            runtime.edit().putInt("l5_stable_passes", 0).apply();
            appendFailure("Background Auto-Training: regressione/dimenticanza · rollback automatico");
            sendTelemetry(after, "background_rollback_regression");
            return false;
        }

        if (!rotateAndSave()) {
            appendFailure("Background Auto-Training: salvataggio post-chunk fallito");
            MainActivity.nativeLoadCheckpoint(checkpoint("previous").getAbsolutePath());
            rotateAndSave();
            return false;
        }

        boolean accepted = acc >= 0.95;
        for (double x : r) accepted = accepted && x >= 0.90;

        int stable = accepted ? runtime.getInt("l5_stable_passes", 0) + 1 : 0;
        runtime.edit().putInt("l5_stable_passes", stable).apply();

        int step = after.optInt("step", 0);
        int start = after.optInt("curriculum_start_step", step);
        int levelSteps = Math.max(0, step - start);
        String progress = String.format(Locale.ITALY,
                "Background L5 · %d passi · validation %.1f%% · memoria min %.1f%%",
                levelSteps, acc * 100.0, min(r) * 100.0);
        appendProgress(progress);
        String pointLabel = levelSteps % 100 == 0 ? "L5 · " + levelSteps + " passi" : "";
        EvolutionHistory.record(this, after, false, pointLabel, progress);

        if (stable >= 4) {
            JSONObject fin = new JSONObject(MainActivity.nativeEvaluate());
            double test = fin.optDouble("test_accuracy", 0.0);
            boolean finalOk = test >= 0.90;
            for (double x : retentions(fin)) finalOk = finalOk && x >= 0.90;

            if (finalOk) {
                rotateAndSave();
                String finalProgress = String.format(Locale.ITALY,
                        "L5 consolidato in background · TEST %.1f%%", test * 100.0);
                evo.edit()
                        .putBoolean("l5_accepted", true)
                        .putString("last_progress", finalProgress)
                        .apply();
                EvolutionHistory.record(this, fin, true, "L5 consolidato", finalProgress);
                runtime.edit().putInt("l5_stable_passes", 0).apply();
                sendTelemetry(fin, "background_L5_consolidated");
                return false;
            }

            rollbackToBaseline("TEST finale L5 non superato");
            sendCurrentTelemetry("background_rollback_final_test");
            return false;
        }

        if (step - start >= 5000) {
            rollbackToBaseline("limite di sicurezza L5 raggiunto");
            sendCurrentTelemetry("background_rollback_safety_limit");
            return false;
        }

        sendTelemetry(after, "background_training_L5");
        return true;
    }

    private void rollbackToBaseline(String reason) {
        File baseline = checkpoint("autotrain-baseline");
        if (baseline.exists() && MainActivity.nativeLoadCheckpoint(baseline.getAbsolutePath())) {
            rotateAndSave();
        }
        getSharedPreferences("motorai_evolution", MODE_PRIVATE)
                .edit().putBoolean("l5_accepted", false).apply();
        getSharedPreferences("motorai_runtime", MODE_PRIVATE)
                .edit().putInt("l5_stable_passes", 0).apply();
        appendFailure("Background Auto-Training: " + reason + " · rollback Seed 010");
    }

    private void sendCurrentTelemetry(String status) {
        try {
            int level = MainActivity.nativeCurriculum();
            boolean accepted = getSharedPreferences("motorai_evolution", MODE_PRIVATE)
                    .getBoolean("l5_accepted", false);
            JSONObject j = (level >= 5 && !accepted)
                    ? new JSONObject(MainActivity.nativeTrainingEvaluate())
                    : new JSONObject(MainActivity.nativeEvaluate());
            sendTelemetry(j, status);
        } catch (Exception e) {
            appendFailure("Bridge background: " + safe(e.getMessage()));
        }
    }

    private void sendTelemetry(JSONObject metrics, String status) {
        JSONObject snapshot = MotorAIBridgeClient.buildSnapshot(this, metrics, status);
        if (!MotorAIBridgeClient.sendSnapshot(this, snapshot)) {
            appendFailure("Diagnostic Bridge: invio non riuscito; nuovo tentativo al prossimo ciclo");
        }
    }

    private double[] retentions(JSONObject j) {
        return new double[] {
                j.optDouble("retention_l0_accuracy", 1.0),
                j.optDouble("retention_l1_accuracy", 1.0),
                j.optDouble("retention_l2_accuracy", 1.0),
                j.optDouble("retention_l3_accuracy", 1.0),
                j.optDouble("retention_l4_accuracy", 1.0)
        };
    }

    private double min(double[] values) {
        double m = 1.0;
        for (double x : values) m = Math.min(m, x);
        return m;
    }

    private File checkpoint(String name) {
        return new File(new File(getFilesDir(), "motorai/checkpoints"), name);
    }

    private boolean rotateAndSave() {
        File root = new File(getFilesDir(), "motorai/checkpoints");
        if (!root.exists() && !root.mkdirs()) return false;
        File temp = checkpoint("tmp-bg");
        File current = checkpoint("current");
        File previous = checkpoint("previous");
        deleteTree(temp);
        if (!MainActivity.nativeSaveCheckpoint(temp.getAbsolutePath())) return false;
        deleteTree(previous);
        if (current.exists() && !current.renameTo(previous)) {
            deleteTree(temp);
            return false;
        }
        return temp.renameTo(current);
    }

    private static void deleteTree(File f) {
        if (f == null || !f.exists()) return;
        if (f.isDirectory()) {
            File[] children = f.listFiles();
            if (children != null) for (File child : children) deleteTree(child);
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
        long freeMb = mi.availMem / (1024L * 1024L);

        if (!charging) return new Guard(false, "non_in_carica");
        if (temp >= 40.5f) return new Guard(false, "temperatura_alta");
        if (level >= 0 && level < 30) return new Guard(false, "batteria_bassa");
        if (mi.lowMemory || freeMb < 512) return new Guard(false, "ram_bassa");
        return new Guard(true, "");
    }

    private void appendProgress(String message) {
        getSharedPreferences("motorai_evolution", MODE_PRIVATE).edit()
                .putString("last_progress", message)
                .apply();
    }

    private void appendFailure(String message) {
        try {
            SharedPreferences p = getSharedPreferences("motorai_diagnostics", MODE_PRIVATE);
            String old = p.getString("failures", "");
            String entry = "• " + new java.text.SimpleDateFormat("dd/MM/yyyy HH:mm:ss", Locale.ITALY)
                    .format(new java.util.Date()) + " — " + safe(message);
            String combined = old == null || old.isEmpty() ? entry : old + "\n" + entry;
            String[] lines = combined.split("\n");
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

    private static String safe(String s) {
        if (s == null) return "errore sconosciuto";
        String v = s.replace('\n', ' ').replace('\r', ' ');
        return v.substring(0, Math.min(300, v.length()));
    }

    private static class Guard {
        final boolean allowed;
        final String reason;
        Guard(boolean allowed, String reason) {
            this.allowed = allowed;
            this.reason = reason;
        }
    }
}
