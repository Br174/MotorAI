#!/usr/bin/env python3
"""Goal 10 candidate must be read-only, independently scored, and fail closed."""
from pathlib import Path
native=Path("android/app/src/main/cpp/native_bridge.cpp").read_text()
java=Path("android/app/src/main/java/it/motorai/seed/MiniAiGoal10CandidateInspector.java").read_text()
ui=Path("android/app/src/main/java/it/motorai/seed/DiagnosticsActivity.java").read_text()
main=Path("android/app/src/main/java/it/motorai/seed/MainActivity.java").read_text()
cert=Path("android/app/src/main/java/it/motorai/seed/MiniAiAssistantCycle.java").read_text()
fn=native.split("Java_it_motorai_seed_MainActivity_nativeGoal10CandidateCompare",1)[1].split(
    'extern "C" JNIEXPORT',1)[0]
profile=native.split("static std::string goal10RawProfile(",1)[1].split(
    'extern "C" JNIEXPORT',1)[0]
for example in ("quanto fa sette piu otto","come mi chiamo","chi e michelangelo",
                "17 + 4 = 22","Michelangelo era un artista. Fonte: Wikipedia"):
    assert example in profile and example in cert,example
for call in ("classifyGoal1","respondGoal2","classifyGoal3","solveGoal4","classifyGoal5",
             "planGoal6","routeGoal7","planGoal8","reviewGoal9"):
    assert call in profile,call
for forbidden in ("g_engine.loadCheckpoint(","g_engine.saveCheckpoint(",
                  "g_engine.trainGoal","candidate.saveCheckpoint(","candidate.trainGoal"):
    assert forbidden not in fn,forbidden
assert "candidate.loadCheckpoint(toString(env,candidatePath))" in fn
assert "goal10RawProfile(g_engine)" in fn and "goal10RawProfile(candidate)" in fn
assert "candidate.globalStep()!=liveStep" in fn
assert "candidate.goal1Step()<=goal1Step" in fn
assert "live_unchanged" in fn
assert "active_goal1_test" in fn and "candidate_goal1_test" in fn
for q in ("case \"comprensione\"","case \"risposta\"","case \"memoria\"",
          "case \"ragionamento\"","case \"incertezza\"","case \"ricerca\"",
          "case \"strumenti\"","case \"piano\"","case \"autocontrollo-calcolo\"",
          "case \"autocontrollo-fonte\""):
    assert q in java,q
assert "activeCount==9 && candidateCount==10 && othersEqual" in java
assert "newTest+0.00001>=originalTest" in java
assert "newVal+0.00001>=originalVal" in java
assert "fingerprint(current)" in java and "fingerprint(candidate)" in java
assert "MotorAIGoal10DiagnosticHold.shouldHold(context)" in java
assert "MiniAiGoals.updateGoal(" not in java
assert "MiniAiAssistantCycle.evaluate(" not in java
assert "nativeLoadCheckpoint(" not in java
assert "MotorAIGoal10CandidateInspector.inspect(this)" in ui
assert "candidateCertification.getText()" in ui
assert "nativeGoal10CandidateCompare(String candidatePath)" in main
assert "versionCode = 33" in Path("android/app/build.gradle.kts").read_text()
print("PASS Goal10 shadow-candidate 10 checks, evidence alignment and complete non-mutation")
