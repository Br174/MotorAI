package it.motorai.seed;

import android.content.Context;

/** R3R4 forensic-only safety: do not train an incomplete Goal10
 * until the measured failure is diagnosed and a tested repair is installed.
 * No stored preferences are altered and prior accepted Goals stay untouched.
 */
public final class MotorAIGoal10DiagnosticHold {
    private MotorAIGoal10DiagnosticHold() {}
    public static boolean shouldHold(Context c) {
        return MiniAiGoals.completedCount(c) == 9
            && MiniAiGoals.percent(c, 9) < 100;
    }
}
