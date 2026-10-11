#!/usr/bin/env python3
from pathlib import Path
native=Path("android/app/src/main/cpp/native_bridge.cpp").read_text()
probe=Path("android/app/src/main/java/it/motorai/seed/MotorAIGoal10ShadowTrial.java").read_text()
ui=Path("android/app/src/main/java/it/motorai/seed/DiagnosticsActivity.java").read_text()
fn=native.split("Java_it_motorai_seed_MainActivity_nativeGoal1ShadowTrial",1)[1].split('extern "C" JNIEXPORT',1)[0]
assert "motorai::Engine shadow(174)" in fn
assert "shadow.loadCheckpoint(base)" in fn
assert "shadow.trainGoal1(20,24,0.02f)" in fn
assert "metrics.loss" in fn
assert "lossStagnant>=36" in fn
assert "stopped_by" in fn
assert "validationLoss<=initialLoss+0.01" in fn
assert "shadow.saveCheckpoint(dest)" in fn
assert "shadow.evaluateGoal1Validation()" in fn
assert "shadow.evaluateGoal1Test()" in fn
assert "source_out_of_sync" in fn
assert "liveStep==g_engine.globalStep()" in fn
assert "liveGoal1==g_engine.goal1Step()" in fn
assert "after>=.50" in fn
assert "g_engine.trainGoal1(" not in fn
assert "g_engine.loadCheckpoint(" not in fn
block=fn.split("for(int n=0;n<150;++n)",1)[1].split("// Only now perform",1)[0]
assert "quanto fa sette piu otto" not in block
assert "evaluateGoal1Test()" not in block
assert "MotorAIGoal10DiagnosticHold.shouldHold(c)" in probe
assert "MotorAIRecoveryMode.paused(c)" in probe
assert "String before=fingerprint(current)" in probe
assert "if(!before.equals(after))" in probe
assert "MotorAIGoal10ShadowTrial.preview(this)" in ui
print("PASS shadow training isolated, validation early-stop, no live writes")
