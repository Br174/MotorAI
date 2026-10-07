package it.motorai.seed;

import android.content.Context;

/**
 * Orchestratore unico del percorso Mini-AI.
 *
 * Le UI non contengono più logica specifica di passaggio 1->2->3...
 * Ogni modulo di training registra avanzamento qui; quando un obiettivo arriva al
 * TEST finale, questo coordinatore attiva semanticamente il primo obiettivo non completato.
 */
public final class MiniAiTrainingCoordinator {
    private MiniAiTrainingCoordinator(){}

    public static int activeGoal(Context context){
        return MiniAiGoals.activeGoalIndex(context);
    }

    public static void progress(Context context, int goalIndex, int percent, int step,
                                double validation, String evidence, String message) {
        MiniAiGoals.updateGoal(context, goalIndex, percent, evidence);
        MiniAiTrainingStatus.update(context, goalIndex, "training", percent, step,
                validation, message);
    }

    public static void testing(Context context, int goalIndex, int percent, int step,
                               double validation, String message) {
        MiniAiTrainingStatus.update(context, goalIndex, "testing", percent, step,
                validation, message);
    }

    public static void completed(Context context, int goalIndex, int step,
                                 double testAccuracy, String evidence) {
        MiniAiGoals.updateGoal(context, goalIndex, 100, evidence);
        int next = MiniAiGoals.activeGoalIndex(context);
        if (next == goalIndex && goalIndex < MiniAiGoals.TITLES.length - 1) {
            next = goalIndex + 1;
        }
        if (goalIndex >= MiniAiGoals.TITLES.length - 1) {
            MiniAiTrainingStatus.update(context, goalIndex, "completed", 100, step,
                    testAccuracy, "Percorso Mini-AI completato.");
        } else {
            MiniAiTrainingStatus.update(context, next, "waiting",
                    MiniAiGoals.percent(context, next), 0, 0.0,
                    "Obiettivo " + (goalIndex + 1) + " completato. Passaggio automatico all'Obiettivo "
                            + (next + 1) + "/10.");
            MotorAIBackgroundJobService.scheduleKick(context.getApplicationContext());
        }
    }

    public static void paused(Context context, int goalIndex, int percent, int step,
                              double validation, String reason) {
        MiniAiTrainingStatus.update(context, goalIndex, "paused", percent, step,
                validation, reason);
    }

    public static void error(Context context, int goalIndex, int percent, int step,
                             double validation, String reason) {
        MiniAiTrainingStatus.update(context, goalIndex, "error", percent, step,
                validation, reason);
    }
}
