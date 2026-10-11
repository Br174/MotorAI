#!/usr/bin/env python3
"""R3R12: independent, balanced shadow training and per-class validation safety."""
from pathlib import Path
import re
base=Path("android/app/src/main")
core=(base/"cpp/motorai_core.cpp").read_text()
header=(base/"cpp/motorai_core.h").read_text()
jni=(base/"cpp/native_bridge.cpp").read_text()
java=(base/"java/it/motorai/seed/MotorAIPhase2ShadowTrial.java").read_text()
main=(base/"java/it/motorai/seed/MainActivity.java").read_text()
ui=(base/"java/it/motorai/seed/DiagnosticsActivity.java").read_text()
gradle=Path("android/app/build.gradle.kts").read_text()
workflow=Path(".github/workflows/build-android-debug.yml").read_text()
old=(base/"java/it/motorai/seed/MotorAIGeneralizationProbe.java").read_text()

assert "void augmentPhase2R3R12Goal7BalancedTrainOnly();" in header
train=core.split("void Engine::augmentPhase2R3R12Goal7BalancedTrainOnly()",1)[1].split(
    "/** R3R11 Pollicino",1)[0]
assert "impl_->goal7data.train.push_back" in train
for illegal in ["impl_->goal7data.val", "impl_->goal7data.test","impl_->goal1data", "g_engine","saveCheckpoint"]:
    assert illegal not in train
added=re.findall(r'\{[0-3],"([^"]+)"\}',train)
assert len(added)==26 and len(set(added))==26
native=jni.split("Java_it_motorai_seed_MainActivity_nativePhase2R3R12Experiment(",1)[1].split(
    "Java_it_motorai_seed_MainActivity_nativePhase2R3R11Experiment(",1)[0]
for marker in (
    "motorai::Engine selected(174)", "motorai::Engine trial(174)",
    "trial.augmentPhase2TrainOnly()", "trial.augmentPhase2R3R12Goal7BalancedTrainOnly()",
    "for(int lane: {1,7})", "selected.adoptIsolatedGoal1(trial)", "selected.adoptIsolatedGoal7(trial)",
    "const Case guard[]", "const Case blind[]", "const Case replay[]",
    "guardNoWorse(candidateGuard,baseGuard)", "currentGuard.total>bestGuard.total",
    "bestGuard=currentGuard", "guardRetained && valNoLoss && testsNoLoss",
    "finalTest7.answer_accuracy", "finalTest1.answer_accuracy",
    "afterBlind>beforeBlind", "afterReplay>=beforeReplay",
    "liveStatus!=g_engine.statusJson()","goal10RawProfile(selected)",
):
    assert marker in native,marker
for forbidden in ["g_engine.train", "g_engine.saveCheckpoint", "g_engine.loadCheckpoint",
                  "g_engine.reset(", "MiniAiGoals.updateGoal("]:
    assert forbidden not in native, forbidden
guard=native.split("const Case guard[]",1)[1].split("const Case replay[]",1)[0]
blind=native.split("const Case blind[]",1)[1].split("const Case guard[]",1)[0]
guard_prompts=re.findall(r'\{7,"([^"]+)","(chat|memory|calculator|search)"\}',guard)
blind_prompts=re.findall(r'\{[17],"([^"]+)","([^"]+)"\}',blind)
assert len(guard_prompts)==24 and len(blind_prompts)==20
assert len({p for p,_ in guard_prompts})==24
assert len({p for p,_ in blind_prompts})==20
assert sorted([sum(x[1]==group for x in guard_prompts) for group in ["chat","memory","calculator","search"]])==[6,6,6,6]
original_aug=core.split("void Engine::augmentPhase2TrainOnly()",1)[1].split(
    "Metrics Engine::evaluateGoal1Train",1)[0]
training=set(added)|set(re.findall(r'\{[0-4],"([^"]+)"\}',original_aug))
r3r9=set(re.findall(r'new Case\("[^"]+","([^"]+)"',old))
for prompt,_ in guard_prompts+blind_prompts:
    assert prompt not in training,"train leakage"
    assert prompt not in r3r9,"repeated R3R9 holdout"
    assert '"'+prompt+'"' not in core,"part of native corpus"
assert not set(p for p,_ in blind_prompts)&set(p for p,_ in guard_prompts)
# The final blind and original TEST are ONLY post-selection measurements.
assert native.index("const auto finalTest1")>native.index("for(int lane: {1,7})")
assert native.index("const int afterBlind")>native.index("for(int lane: {1,7})")
assert native.index("const GuardStats finalGuard")>native.index("for(int lane: {1,7})")
for required in (
    "runR3R12(Context c)", "MotorAIRecoveryMode.paused(c)",
    "MiniAiGoals.completedCount(c)!=10", "allProtected(c)",
    "synchronized(MotorAICheckpointStore.recoveryMutex())",
    'motorai/phase2-r3r12-shadow-candidate', "otherCapabilitiesUnchanged(",
    "tenChecks(", "nativeProbeCheckpoint(", "guard_no_regression",
):
    assert required in java,required
assert "MotorAIPhase2ShadowTrial.runR3R12(this)" in ui
assert "nativePhase2R3R12Experiment(String sourcePath, String candidatePath)" in main
assert "MotorAI Seed 021R3R13" in main
assert "versionCode = 39" in gradle
assert 'versionName = "0.21.16-seed021r3r13"' in gradle
assert "Verify R3R12 Goal7 category guard and independent holdout" in workflow
print("PASS R3R12: balanced shadow Goal7, no old TEST selection, 24 supplemental val, 20 untouched final blind, no live writes")
