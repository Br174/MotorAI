# MotorAI Seed 021R2 — Goal10 and checkpoint repair
Operation: MOTORAI-SEED021R2-GOAL10-CHECKPOINT-20261011
Approved by user: yes; protected UI, pretrained policy and previous LAB remain untouched.
Source ref: lab/seed021r2-goal10-checkpoints
Certified source commit: 4acd0467193fe03ca9d539657bed394f5d79e4f7
CI run: https://github.com/Br174/MotorAI/actions/runs/38101051125
CI result: SUCCESS, all existing Goal1–Goal10 and L0–L5 benchmarks passed.
Artifact ID: 11687453283, MotorAI-Seed021R2-debug
GitHub artifact ZIP SHA256: e3630ef319761ec678acf19b7e16f1fb572bd6493dc141bd9a4c0df5b4aca390
APK SHA256: 4ffd8e73c05c268e41f6c1d1d2a84a7a990e55a3f0fc361efa5ba5b1c6c05377
Install: Android applicationId it.motorai.seed, same stable LAB signing identity, versionCode 25, versionName 0.21.2-seed021r2.

## Root cause
Actual device screenshot: Goal10 = 6/10 (60%), overall 96%, Goal1–9 accepted, checkpoints current/previous/auto-baseline OK. Isolated backend background reports occasional pre-chunk checkpoint unavailable.

New 10-check host integration smoke reproduced exactly 6/10 after original acceptance training:
- comprehension correct intent calcolo but confidence .3433, threshold .50.
- memory correct recall/name but confidence .4676, threshold .50.
- self-check calculation correct but confidence .4830, threshold .50.
- self-check source correct but confidence .4894, threshold .50.
The other six checks passed. Root issue is inadequate confidence generalization at integration gate, not a bug in the percent calculation. The prior goal10_smoke tested router capability examples but not confidence of all 10 integrated checks.

## Fix
- Shared MotorAICheckpointStore serializes checkpoint rotation and native restore using one lock across MainActivity, background and ChatActivity, verifies files before promotion and retains previous/backup transaction path.
- Persistent MotorAIGoal10Recovery performs bounded extra training for only failed capabilities, never changes thresholds or marks Goal10 complete unless all real checks pass; saves/loads checkpoint around each attempt and rejects regressions.
- MiniAiAssistantCycle persists exact passed/failed proof and DiagnosticsActivity displays it.
- Foreground and background reuse one recovery policy; no additional training scheduler or neural engine.
- Cosmetic build version updated while preserving approved screen design.

## Verification
- Targeted recovery of Goal1=15 chunks, Goal3=46 chunks, Goal9=1 chunk in host test, independent accepted tests remained PASS.
- Integration test after saved checkpoint reload: 10/10, confidence at/above original required thresholds.
- Existing native curriculum, Goal1–Goal10, Goal3 recovery and background handoff tests all passed.
- Android debug APK built and signed using stable LAB signing key; upload and final combined gate success.
- Device acceptance pending; do not promote to Mother or declare user Goal10 achieved until updated device shows 10/10.

## Pollicino next step
Install APK over installed version without uninstall/clear data. Check Goal1–9 remain 100, Goal10 individual fail/pass text in Diagnostics, confidence progresses and Goal10 reaches 10/10; verify checkpoints current and previous show OK and reopen app to confirm persistence. Keep R1 (CI run 38099248753) and Seed010 accepted floor for rollback.
