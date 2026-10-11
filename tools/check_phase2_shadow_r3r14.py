#!/usr/bin/env python3
"""R3R14: shadow-only paired hard-negative training and two-stage preview contract."""
import re
from pathlib import Path
base=Path("android/app/src/main")
core=(base/"cpp/motorai_core.cpp").read_text()
header=(base/"cpp/motorai_core.h").read_text()
native=(base/"cpp/native_bridge.cpp").read_text()
trial=(base/"java/it/motorai/seed/MotorAIPhase2ShadowTrial.java").read_text()
ui=(base/"java/it/motorai/seed/DiagnosticsActivity.java").read_text()
main=(base/"java/it/motorai/seed/MainActivity.java").read_text()
audit=(base/"java/it/motorai/seed/MotorAIPhase2ErrorAudit.java").read_text()
r3r9=(base/"java/it/motorai/seed/MotorAIGeneralizationProbe.java").read_text()
workflow=Path(".github/workflows/build-android-debug.yml").read_text()
gradle=Path("android/app/build.gradle.kts").read_text()

assert "void augmentPhase2R3R14ContrastPairsTrainOnly();" in header
augment=core.split("void Engine::augmentPhase2R3R14ContrastPairsTrainOnly()",1)[1].split(
    "/** R3R12: restore",1)[0]
assert "impl_->goal1data.train.push_back" in augment
assert "impl_->goal7data.train.push_back" in augment
for forbidden in (
    "impl_->goal1data.val", "impl_->goal1data.test",
    "impl_->goal7data.val", "impl_->goal7data.test",
    "g_engine", "saveCheckpoint(", "loadCheckpoint("
):
    assert forbidden not in augment,forbidden

goal1=augment.split("const std::pair<int,const char*> intentPairs[]={",1)[1].split(
    "const std::pair<int,const char*> routingPairs[]={",1)[0]
goal7=augment.split("const std::pair<int,const char*> routingPairs[]={",1)[1].split(
    "for(const auto& q:intentPairs)",1)[0]
train1=re.findall(r'\{([0-4]),"([^"]+)"\}',goal1)
train7=re.findall(r'\{([0-3]),"([^"]+)"\}',goal7)
assert len(train1)>=40 and len(train7)>=40,(len(train1),len(train7))
assert len({s for _,s in train1})==len(train1)
assert len({s for _,s in train7})==len(train7)
assert len({a for a,_ in train1})==5
assert len({a for a,_ in train7})==4

section=native.split("Java_it_motorai_seed_MainActivity_nativePhase2R3R14Experiment(",1)[1].split(
    "Java_it_motorai_seed_MainActivity_nativePhase2R3R12Experiment(",1)[0]
assert "const Case blind[]" in section
assert "const Case guard[]" in section
assert "const Case intentGuard[]" in section
def cases(name,stop):
    part=section.split("const Case "+name+"[]={",1)[1].split(stop,1)[0]
    return re.findall(r'\{([17]),"([^"]+)","([^"]+)"\}',part)
blind=cases("blind","const Case guard[]")
guard=cases("guard","const Case intentGuard[]")
intent_guard=cases("intentGuard","// Retest six known")
assert len(blind)==24 and len(guard)==24 and len(intent_guard)==15
assert len({s for _,s,_ in blind+guard+intent_guard})==63
assert set(x[2] for x in blind if x[0]=="1")=={
    "saluto","ricerca","calcolo","informazione","azione"
}
assert set(x[2] for x in guard)=={"chat","memory","calculator","search"}
assert all(x[0]=="1" for x in intent_guard) and all(x[0]=="7" for x in guard)
train_sentences={s for _,s in train1+train7}
old_cases=set(re.findall(r'new Case\("[^"]+",[17],"([^"]+)"',audit))
old_cases|=set(re.findall(r'new Case\("[^"]+","([^"]+)"',r3r9))
for lane,prompt,label in blind+guard+intent_guard:
    assert prompt not in train_sentences
    assert prompt not in old_cases
    assert '"'+prompt+'"' not in core, "R3R14 prompt present in original native train/val/test"
for forbidden in ("g_engine.train","g_engine.saveCheckpoint","g_engine.loadCheckpoint",
                  "g_engine.reset(","MiniAiGoals.updateGoal"):
    assert forbidden not in section
for required in (
    "motorai::Engine selected(174)", "motorai::Engine trial(174)",
    "selected.loadCheckpoint(base)", "trial.loadCheckpoint(base)",
    "trial.augmentPhase2R3R14ContrastPairsTrainOnly()",
    "trial.augmentPhase2TrainOnly()", "trial.augmentPhase2R3R12Goal7BalancedTrainOnly()",
    "selected.adoptIsolatedGoal1(trial)", "selected.adoptIsolatedGoal7(trial)",
    "intentsNoWorse(candidateIntent,baseIntent)",
    "guardNoWorse(candidateGuard,baseGuard)",
    "currentIntent.total>bestIntent.total",
    "const auto staged=", "stageBefore", "stageAfter",
    "intentGuardRetained && guardRetained && valNoLoss && testsNoLoss && improvement",
    "afterBlind>beforeBlind", "afterReplay>=beforeReplay",
    "stageAfter>=stageBefore", "selected.saveCheckpoint(dest)",
    "goal10RawProfile(selected)", "g_engine.statusJson()",
):
    assert required in section,required
assert section.index("const auto finalTest1") > section.index("for(int lane: {1,7})")
assert section.index("const int afterBlind") > section.index("for(int lane: {1,7})")
assert section.index("const int stageAfter") > section.index("for(int lane: {1,7})")
for required in (
    "runR3R14(Context c)", "phoneTooWarm(c)", "temp>=400",
    "MotorAIRecoveryMode.paused(c)", "MiniAiGoals.completedCount(c)!=10",
    "synchronized(MotorAICheckpointStore.recoveryMutex())",
    "allProtected(c)", "MainActivity.nativeStatus()",
    "nativePhase2R3R14Experiment(", "intent_guard_no_regression",
    "otherCapabilitiesUnchanged(", "tenChecks(",
    "nativeProbeCheckpoint(", "motorai/phase2-r3r14-shadow-candidate",
):
    assert required in trial,required
assert "MotorAIPhase2ShadowTrial.runR3R14(this)" in ui
assert "MotorAIPhase2ErrorAudit.run(this)" in ui
assert "nativePhase2R3R14Experiment(String sourcePath, String candidatePath)" in main
assert "MotorAI Seed 021R3R14" in main
assert "versionCode = 40" in gradle
assert 'versionName = "0.21.17-seed021r3r14"' in gradle
assert "Verify R3R14 contrasting shadow and staged router safety" in workflow
print("PASS R3R14: paired contrastive shadow train, 39 dev guard, 24 fresh blind, staged readout only, SOS and thermal guard")
