# MotorAI Seed 008 — Adaptive acceptance / device-gap fix

Date: 2026-10-07

## Trigger
Seed 007 correctly resumed the accepted Seed 006 checkpoint at 620 total steps, but after its nominal 600-step Level-2 run on the real Android device it reached only:
- L2 test generalization: 75.0%
- L1 retention: 100.0%
- L0 retention: 95.6%
- total step: 1220

Seed 007 is rejected and Seed 006 remains the last accepted restore baseline.

## Root causes closed
1. Fixed-step completion defect: the Android app declared a level completed when the step budget ended even when benchmark acceptance was below 90%.
2. Transition mismatch: on device, a checkpoint already at the end of Level 1 could execute one extra 20-step Level-1 chunk before switching to Level 2. The host gate switched at the exact boundary.
3. Random mixed replay: Level-2 batches sampled a combined dataset randomly. Seed 008 uses explicit deterministic proportions:
   - 50% Level 2
   - 25% Level 1 replay
   - 25% Level 0 replay
4. Test leakage in control loop: autosave and training chunks previously evaluated the test set. Seed 008 uses training/validation/retention only during learning; the test set is reserved for final evaluation.
5. Single-trajectory host gate: Seed 008 validates three independent seeds and simulates checkpoint save -> new Engine -> load -> curriculum transition before accepting the build.

## Acceptance-driven training
A level is accepted only when validation >= 90% and all required retention metrics >= 90% for two consecutive checks. Level 2 may continue adaptively up to a hard safety cap; reaching a step count alone can no longer mark success.

## Multi-seed checkpoint-resume gate
- seed 174: L0 97.78%, L1 100%, L2 100%, L1 retention 100%, L0 retention 100%, stopped at step 920
- seed 175: L0 100%, L1 100%, L2 100%, L1 retention 100%, L0 retention 100%, stopped at step 840
- seed 176: L0 100%, L1 100%, L2 91.67%, L1 retention 100%, L0 retention 100%, stopped at step 820

All three passed >=90% for the new task and both retained tasks.

## Build
GitHub Actions run: 37542609961
Source head: c2872fe85defc7e16ecd567ce541fa037a4b3517
Artifact ID: 11448872458
Artifact SHA-256: 081edbcdfe2af12dc0cf94e4a2e367d458820240772c009a3bc6b5efc9273499
APK SHA-256: 25c9bf8fc0c6c059ddfa9d4b5a4a3cf7c6189da95a52a034a4bb8b9fad04ba56
APK size: 4,732,407 bytes

## Device path
Install Seed 008 over Seed 007 using the stable LAB signing identity. Do not uninstall. Existing step-1220 Level-2 checkpoint is preserved; pressing Impara continues adaptively from that state. Promotion remains blocked until the real device reaches the acceptance thresholds and passes generation/restart-resume validation.
