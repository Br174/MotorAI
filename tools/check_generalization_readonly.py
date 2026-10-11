#!/usr/bin/env python3
"""R3R9: prohibit contaminated 20-probe scoring and any training/persistent mutation."""
import re
from pathlib import Path
j=Path("android/app/src/main/java/it/motorai/seed/MotorAIGeneralizationProbe.java").read_text()
ui=Path("android/app/src/main/java/it/motorai/seed/DiagnosticsActivity.java").read_text()
core=Path("android/app/src/main/cpp/motorai_core.cpp").read_text()
old=Path("android/app/src/main/java/it/motorai/seed/MiniAiGoal10Inspector.java").read_text()
build=Path("android/app/build.gradle.kts").read_text()
main=Path("android/app/src/main/java/it/motorai/seed/MainActivity.java").read_text()
workflow=Path(".github/workflows/build-android-debug.yml").read_text()

cases=re.findall(r'new Case\("([^"]+)","([^"]+)","([^"]*)","([^"]*)",(\d+)\)',j)
assert len(cases)==20, f"Expected 20 cases, found {len(cases)}"
questions=[x[1] for x in cases]
assert len(set(questions))==20, "repeated cases invalidate scoring"
assert {t:sum(x[0]==t for x in cases) for t in {c[0] for c in cases}} == {
    "intent":4, "memory":4, "reason":4, "uncertainty":4, "source":2, "tool":2
}
for q in questions:
    assert ('"'+q+'"') not in core, f"Prompt already in native training/validation/test: {q}"
    assert q not in old, f"Goal 10 prompt duplicated: {q}"
for forbidden in (
    "nativeTrainChunk(", "nativeSaveCheckpoint(", "nativeLoadCheckpoint(",
    "trainGoal", "rotateAndSaveCheckpoint(", "MiniAiGoals.updateGoal(",
    "putString(", "putInt(", "putBoolean(", "allowTraining(", "scheduleKick(",
    "getSharedPreferences(", "nativeReset(", "System.exit(", "deleteTree("
):
    assert forbidden not in j, f"Forbidden mutation in read-only probe: {forbidden}"
for required in (
    "synchronized(MotorAICheckpointStore.recoveryMutex())",
    "MotorAIRecoveryMode.paused(context)",
    "MiniAiGoals.completedCount(context)!=10",
    "guardedSnapshots(context)",
    'new String[]{"current","previous","autotrain-baseline",',
    '"goal10-original-r3r8","goal10-current-transit-r3r8"',
    "MainActivity.nativeStatus()",
    "before.equals(after)&&nativeBefore.equals(nativeAfter)",
    "20 richieste alternative",
):
    assert required in j, required
assert 'MotorAIGeneralizationProbe.run(this)' in ui
assert 'generalizationReport.getText()' in ui
assert 'Esegui 20 prove nuove (senza allenamento)' in ui
assert "versionCode = 36" in build
assert 'versionName = "0.21.13-seed021r3r10"' in build
assert "MotorAI Seed 021R3R10" in main
assert "Verify R3R9 twenty generalization probes are read only" in workflow
print("PASS R3R9: 20 unique held-out-by-literal-string probes; safety guard, read-only UI and provenance")
