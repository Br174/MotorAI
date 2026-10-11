#!/usr/bin/env python3
"""Verify Goal10 diagnostic fidelity and read-only behavior, without bypassing gates."""
from pathlib import Path
inspector=Path("android/app/src/main/java/it/motorai/seed/MiniAiGoal10Inspector.java").read_text()
cert=Path("android/app/src/main/java/it/motorai/seed/MiniAiAssistantCycle.java").read_text()
ui=Path("android/app/src/main/java/it/motorai/seed/DiagnosticsActivity.java").read_text()
gradle=Path("android/app/build.gradle.kts").read_text()
methods=("nativeGoal1Classify","nativeGoal2Respond","nativeGoal3Classify","nativeGoal4Solve",
         "nativeGoal5Classify","nativeGoal6Plan","nativeGoal7Route","nativeGoal8Plan","nativeGoal9Review")
for method in methods:
    assert method in inspector and method in cert, method
for example in ("quanto fa sette piu otto","come mi chiamo","17 + 4 = 22",
                "Michelangelo era un artista. Fonte: Wikipedia","chi e michelangelo"):
    assert example in inspector and example in cert, example
for forbidden in (".edit()", ".putString(", ".putBoolean(", ".putInt(",
                  "MiniAiGoals.updateGoal(", "MiniAiTrainingCoordinator.completed(",
                  "nativeGoal1TrainChunk", "nativeGoal3TrainChunk", "nativeGoal9TrainChunk",
                  "nativeSaveCheckpoint", "nativeLoadCheckpoint", "rotateAndSave(",
                  "MotorAIGoal10Recovery.oneChunk(", "MiniAiAssistantCycle.evaluate("):
    assert forbidden not in inspector, forbidden
assert inspector.count("add(b,")==10
assert inspector.count("confidence(j)>=0.50")==4
assert "confidence(j)>=0.55" in inspector
assert inspector.count("nativeGoal9Review(")==2
assert "MiniAiGoal10Inspector.inspect()" in ui
assert "new Thread(() ->" in ui
assert "goal10Inspector.getText()" in ui
hold=Path("android/app/src/main/java/it/motorai/seed/MotorAIGoal10DiagnosticHold.java").read_text()
foreground=Path("android/app/src/main/java/it/motorai/seed/MainActivity.java").read_text()
background=Path("android/app/src/main/java/it/motorai/seed/MotorAIBackgroundJobService.java").read_text()
assert "MiniAiGoals.completedCount(c) == 9" in hold
assert "MiniAiGoals.percent(c, 9) < 100" in hold
assert "MotorAIGoal10DiagnosticHold.shouldHold(this)" in foreground
assert "MotorAIGoal10DiagnosticHold.shouldHold(this)" in background
assert "return false;" in background
assert "versionCode = 30" in gradle
assert 'versionName = "0.21.7-seed021r3r4"' in gradle
print("PASS: 10 Goal10 probes match cert criteria; no writes, no training, device UI wired.")
