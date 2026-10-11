#!/usr/bin/env python3
"""R3R8 fail-closed promotion wiring and crash recovery static CI contract."""
from pathlib import Path

root=Path("android/app/src/main/java/it/motorai/seed")
promotion=(root/"MotorAIGoal10Promotion.java").read_text()
ui=(root/"DiagnosticsActivity.java").read_text()
main=(root/"MainActivity.java").read_text()
recovery=(root/"MotorAIRecoveryMode.java").read_text()
gradle=Path("android/app/build.gradle.kts").read_text()
wf=Path(".github/workflows/build-android-debug.yml").read_text()

for requirement in (
    "synchronized(MotorAICheckpointStore.recoveryMutex())",
    "MotorAIRecoveryMode.paused(c)",
    "MiniAiGoal10CandidateInspector.inspect(c)",
    "CANDIDATA VERIFICATA IN ISOLAMENTO",
    "fingerprint(current)",
    "fingerprint(candidate)",
    "fingerprint(previous)",
    "sameFoundation(foundationBefore,foundationAfter)",
    "MiniAiAssistantCycle.evaluate(c)",
    "nativeLoadCheckpoint(candidate.getAbsolutePath())",
    "nativeSaveCheckpoint(stage.getAbsolutePath())",
    "nativeProbeCheckpoint(stage.getAbsolutePath())",
    "nativeProbeCheckpoint(current.getAbsolutePath())",
    "current.renameTo(folder(c,\"goal10-current-transit-r3r8\"))",
    "stage.renameTo(current)",
    "transit.renameTo(current)",
    "MiniAiGoals.updateGoal(c,9,100",
    "recoverInterrupted(Context c)",
    "MiniAiGoals.percent(c,9)>=100",
    "MotorAIRecoveryMode.pause(c)",
    "Goal10 resta non certificato",
):
    assert requirement in promotion, requirement

# Acceptance update must occur after the FINAL live integration test and stage commit.
assert promotion.index("stage.renameTo(current)") < promotion.index("MiniAiGoals.updateGoal(c,9,100")
assert promotion.index("MiniAiAssistantCycle.Result finalTest") < promotion.index("MiniAiGoals.updateGoal(c,9,100")
assert promotion.index("nativeSaveCheckpoint(stage.getAbsolutePath())") < promotion.index("stage.renameTo(current)")
assert "deleteTree(original)" not in promotion
assert "deleteTree(previous)" not in promotion
assert "MotorAIRecoveryMode.allowTraining(" not in promotion
assert "nativeGoal1TrainChunk(" not in promotion
assert "setPositiveButton(\"Verifica e applica\"" in ui
assert "MotorAIGoal10Promotion.promote(this)" in ui
assert "MotorAIGoal10Promotion.recoverInterrupted(this)" in main
assert main.index("MotorAIGoal10Promotion.recoverInterrupted(this)") < main.index("UI_ACTIVE.set(true)")
assert "paused\",true" in recovery
assert "versionCode = 34" in gradle and 'versionName = "0.21.11-seed021r3r8"' in gradle
assert "Verify guarded Goal10 promotion contract" in wf
print("PASS R3R8 guarded promotion, anti-regression, SOS pause, rollback, crash recovery and in-place update")
