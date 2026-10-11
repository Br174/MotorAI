#!/usr/bin/env python3
"""R3R10: strict no-live-training, no-leakage, no-regression and copy-only wiring."""
from pathlib import Path
import re

core=Path("android/app/src/main/cpp/motorai_core.cpp").read_text()
header=Path("android/app/src/main/cpp/motorai_core.h").read_text()
bridge=Path("android/app/src/main/cpp/native_bridge.cpp").read_text()
java=Path("android/app/src/main/java/it/motorai/seed/MotorAIPhase2ShadowTrial.java").read_text()
ui=Path("android/app/src/main/java/it/motorai/seed/DiagnosticsActivity.java").read_text()
main=Path("android/app/src/main/java/it/motorai/seed/MainActivity.java").read_text()
r3r9=Path("android/app/src/main/java/it/motorai/seed/MotorAIGeneralizationProbe.java").read_text()
gradle=Path("android/app/build.gradle.kts").read_text()
workflow=Path(".github/workflows/build-android-debug.yml").read_text()

assert "void Engine::augmentPhase2TrainOnly()" in core
assert "void augmentPhase2TrainOnly();" in header
augment=core.split("void Engine::augmentPhase2TrainOnly()",1)[1].split("Metrics Engine::evaluateGoal1Train",1)[0]
assert "impl_->goal1data.train.push_back" in augment
assert "impl_->goal7data.train.push_back" in augment
for illegal in [".test.push_back", ".val.push_back", "g_engine", "saveCheckpoint", "loadCheckpoint"]:
    assert illegal not in augment, illegal
train_prompts=re.findall(r'\{[0-4],\"([^\"]+)\"\}',augment)
r3r9_prompts=re.findall(r'new Case\(\"[^\"]+\",\"([^\"]+)\"',r3r9)
assert len(train_prompts)>=40
assert not (set(train_prompts) & set(r3r9_prompts)), "Leakage from known R3R9 prompts"
native=bridge.split("Java_it_motorai_seed_MainActivity_nativePhase2ShadowExperiment",1)[1]
native=native.split("Java_it_motorai_seed_MainActivity_nativeGoal1Evaluate",1)[0]
for token in [
    "motorai::Engine shadow(174)", "shadow.loadCheckpoint(base)",
    "shadow.augmentPhase2TrainOnly()", "shadow.trainGoal1(", "shadow.trainGoal7(",
    "shadow.evaluateGoal1Validation()", "shadow.evaluateGoal1Test()",
    "shadow.evaluateGoal7Validation()", "shadow.evaluateGoal7Test()",
    "shadow.saveCheckpoint(dest)", "g_engine.statusJson()",
    "goal10RawProfile(g_engine)", "const Probe blind[]", "const Probe rerun[]",
    "blindAfter>blindBefore", "oldAfter>=oldBefore", "source_out_of_sync"
]:
    assert token in native, token
for illegal in ["g_engine.train", "g_engine.saveCheckpoint", "g_engine.loadCheckpoint", "g_engine.reset("]:
    assert illegal not in native, "Live change forbidden: "+illegal
blind=re.findall(r'\{[17],\"([^\"]+)\",\"[^\"]+\"\}',native.split("const Probe blind[]",1)[1].split("const Probe rerun[]",1)[0])
assert len(blind)==16 and len(set(blind))==16
assert not(set(blind)&set(train_prompts))
assert not(set(blind)&set(r3r9_prompts))
assert "synchronized(MotorAICheckpointStore.recoveryMutex())" in java
assert "MotorAIRecoveryMode.paused(c)" in java
assert "MiniAiGoals.completedCount(c)!=10" in java
assert "allProtected(c)" in java
assert "nativeBefore.equals(nativeAfter)" in java
assert 'motorai/phase2-r3r10-shadow-candidate' in java
assert "otherCapabilitiesUnchanged(" in java
assert "tenChecks(" in java
assert "nativeGoal10CandidateCompare" in java
assert "nativeProbeCheckpoint" in java
assert "phase2ShadowReport.getText()" in ui
assert "MotorAIPhase2ShadowTrial.runR3R11(this)" in ui
assert "public static String run(Context c)" in java
assert "nativePhase2ShadowExperiment(String sourcePath, String candidatePath)" in main
assert "versionCode = 37" in gradle and "0.21.14-seed021r3r11" in gradle
assert "Phase2 isolated Goal1 Goal7 safety contract R3R10" in workflow
print("PASS R3R10: shadow train-only augmentation, 16 new holdout, locked SOS/current, no native live training/promotion")
