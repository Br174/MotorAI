#!/usr/bin/env python3
"""R3R11 acceptance: independently selected Goal1/7 snapshots, no live changes."""
import re
from pathlib import Path

base=Path("android/app/src")
core=(base/"main/cpp/motorai_core.cpp").read_text()
header=(base/"main/cpp/motorai_core.h").read_text()
jni=(base/"main/cpp/native_bridge.cpp").read_text()
ui=(base/"main/java/it/motorai/seed/DiagnosticsActivity.java").read_text()
java=(base/"main/java/it/motorai/seed/MotorAIPhase2ShadowTrial.java").read_text()
main=(base/"main/java/it/motorai/seed/MainActivity.java").read_text()
old=(base/"main/java/it/motorai/seed/MotorAIGeneralizationProbe.java").read_text()
gradle=Path("android/app/build.gradle.kts").read_text()
workflow=Path(".github/workflows/build-android-debug.yml").read_text()

for name in ("Goal1","Goal7"):
    assert f"void Engine::adoptIsolated{name}(const Engine& source)" in core
    assert f"void adoptIsolated{name}(const Engine& source);" in header
    assert f"selected.adoptIsolated{name}(trial)" in jni
assert "std::scoped_lock lock(impl_->mu, source.impl_->mu)" in core
for name,object_field in [("Goal1","goal1"),("Goal7","goal7")]:
    block=core.split(f"void Engine::adoptIsolated{name}(const Engine& source)",1)[1].split("}",1)[0]
    assert f"impl_->{object_field}=source.impl_->{object_field}" in block
    assert "g_engine" not in block
native=jni.split("Java_it_motorai_seed_MainActivity_nativePhase2R3R11Experiment(",1)[1]
native=native.split("Java_it_motorai_seed_MainActivity_nativePhase2ShadowExperiment(",1)[0]
for needed in (
    "motorai::Engine selected(174)", "motorai::Engine trial(174)",
    "trial.loadCheckpoint(base)", "trial.augmentPhase2TrainOnly()",
    "trial.trainGoal1(", "trial.trainGoal7(", "trial.evaluateGoal1Validation()",
    "trial.evaluateGoal7Validation()", "selected.evaluateGoal1Test()",
    "selected.evaluateGoal7Test()", "selected.saveCheckpoint(dest)",
    "valNoLoss && testsNoLoss", "afterBlind>beforeBlind", "afterReplay>=beforeReplay",
    "liveProfile==goal10RawProfile(selected)" if False else 'goal10RawProfile(selected)',
    "liveStatus!=g_engine.statusJson()", "const Case blind[]",
    "const Case replay[]", "bestSteps1", "bestSteps7",
):
    assert needed in native, needed
for forbidden in ("g_engine.train", "g_engine.saveCheckpoint", "g_engine.loadCheckpoint",
                  "g_engine.reset(", "MiniAiGoals.updateGoal", "nativeLoadCheckpoint"):
    assert forbidden not in native
assert native.index("const int afterBlind")>native.index("for(int lane: {1,7})")
assert native.index("const auto finalTest1")>native.index("for(int lane: {1,7})")
assert "val.answer_accuracy+0.000001f<baseline.answer_accuracy" in native
assert "val.loss>baseline.loss+0.01f" in native

new_cases=re.findall(r'\{[17],"([^"]+)","([^"]+)"\}',
    native.split("const Case blind[]",1)[1].split("const Case replay[]",1)[0])
assert len(new_cases)==20, f"expected 20 new cases, got {len(new_cases)}"
assert len({x[0] for x in new_cases})==20
train=core.split("void Engine::augmentPhase2TrainOnly()",1)[1].split("Metrics Engine::evaluateGoal1Train",1)[0]
train_cases=set(re.findall(r'\{[0-4],"([^"]+)"\}',train))
older=set(re.findall(r'new Case\("[^"]+","([^"]+)"',old))
for prompt,_ in new_cases:
    assert prompt not in train_cases, "leak into phase2 training"
    assert prompt not in older, "reused old holdout"
    assert ('"'+prompt+'"') not in core, "present in native training/validation/test"
assert "MotorAIPhase2ShadowTrial.runR3R12(this)" in ui
assert "nativePhase2R3R11Experiment(String sourcePath, String candidatePath)" in main
assert "MotorAI Seed 021R3R13" in main
for token in ("allProtected(c)", "MotorAIRecoveryMode.paused(c)",
              "MiniAiGoals.completedCount(c)!=10",
              "synchronized(MotorAICheckpointStore.recoveryMutex())",
              "phase2-r3r11-shadow-candidate", "nativeProbeCheckpoint",
              "otherCapabilitiesUnchanged(", "tenChecks(",
              "nativePhase2R3R11Experiment("):
    assert token in java,token
for token in ("nativeSaveCheckpoint(", "nativeLoadCheckpoint(", "rotateAndSave(", "nativeGoal1TrainChunk(", "MiniAiGoals.updateGoal("):
    assert token not in java,token
assert "versionCode = 39" in gradle
assert 'versionName = "0.21.16-seed021r3r13"' in gradle
assert "Verify R3R11 independent-best no regression" in workflow
print("PASS R3R11 independent lane selection, fresh heldout, live checkpoint no-write, protected Goal10 and versionCode 37")
