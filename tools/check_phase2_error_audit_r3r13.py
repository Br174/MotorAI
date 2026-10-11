#!/usr/bin/env python3
"""R3R13: real per-prompt inspection of current model; zero writes or training."""
from pathlib import Path
import re

java=Path("android/app/src/main/java/it/motorai/seed/MotorAIPhase2ErrorAudit.java").read_text()
ui=Path("android/app/src/main/java/it/motorai/seed/DiagnosticsActivity.java").read_text()
jni=Path("android/app/src/main/cpp/native_bridge.cpp").read_text()
main=Path("android/app/src/main/java/it/motorai/seed/MainActivity.java").read_text()
gradle=Path("android/app/build.gradle.kts").read_text()
workflow=Path(".github/workflows/build-android-debug.yml").read_text()
assert "MOTORAI R3R13" not in java or "R3R13" in java
cases=re.findall(r'new Case\("(BLIND20|GUARD24|REPLAY6)",([17]),"([^"]+)","([^"]+)"\)',java)
assert len(cases)==50,len(cases)
assert len({x[2] for x in cases})==50
assert sum(x[0]=="BLIND20" for x in cases)==20
assert sum(x[0]=="GUARD24" for x in cases)==24
assert sum(x[0]=="REPLAY6" for x in cases)==6
old=jni.split("Java_it_motorai_seed_MainActivity_nativePhase2R3R12Experiment(",1)[1].split(
    "Java_it_motorai_seed_MainActivity_nativePhase2R3R11Experiment(",1)[0]
for key,section,end in [
    ("BLIND20","blind","const Case guard[]"),
    ("GUARD24","guard","const Case replay[]"),
    ("REPLAY6","replay","const auto score=")
]:
    found=old.split("const Case "+section+"[]={",1)[1].split(end,1)[0]
    actual=set(re.findall(r'\{([17]),"([^"]+)","([^"]+)"\}',found))
    mapped={(lane,prompt,expected) for set_name,lane,prompt,expected in cases if set_name==key}
    assert mapped==actual,(key,len(mapped),len(actual))
for forbidden in (
    "trainGoal", "nativePhase2R3R12Experiment(", "saveCheckpoint(", "loadCheckpoint(",
    "nativeGoal10Promotion", "putInt(", "putString(", "putBoolean(",
    "MiniAiGoals.updateGoal(", "nativeReset(", "deleteTree(", "Runtime.getRuntime("
):
    assert forbidden not in java,forbidden
for required in (
    "synchronized(MotorAICheckpointStore.recoveryMutex())",
    "MotorAIRecoveryMode.paused(c)", "MiniAiGoals.completedCount(c)!=10",
    "MessageDigest.getInstance(", "nativeGoal1Classify(", "nativeGoal7Route(",
    "MainActivity.nativeStatus()", "before.equals(snapshots(c))",
    "phase2-r3r12" if False else "R3R12 era stata respinta",
    "RIEPILOGO PER CATEGORIA", "TIPI DI CONFUSIONE", "Confidenza:",
):
    assert required in java,required
assert 'MotorAIPhase2ErrorAudit.run(this)' in ui
assert 'phase2ErrorAuditReport.getText()' in ui
assert 'phase2Shadow.setEnabled(false);' in ui
assert 'R3R13 · Analizza le 50 risposte' in ui
assert 'MotorAI Seed 021R3R14' in main
assert "versionCode = 40" in gradle
assert 'versionName = "0.21.17-seed021r3r14"' in gradle
assert 'Verify R3R13 read-only per-question audit' in workflow
print("PASS R3R13: 50 exact historical questions, per-case expected/output/confidence, read-only invariant, no repeat training")
