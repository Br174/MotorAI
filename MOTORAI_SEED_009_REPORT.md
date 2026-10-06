# MotorAI Seed 009 — Curriculum Level 3 Report

Date: 2026-10-07

## Accepted starting baseline
Seed 008 remains the current accepted real-device baseline:
- step 1700
- curriculum Level 2
- Level 2 test: 95.8%
- Level 1 retention: 100%
- Level 0 retention: 100%
- generation ef>> -> ef>>ee
- restart/resume verified

## Level 3 goal
Add a new capability without losing Levels 2, 1 or 0.

Level-3 rule:
- input example: ef>>>
- expected generation: ef>>>ff
- operation: select the second symbol and duplicate it
- Level 2 remains first-symbol duplication with >>
- Level 1 remains pair reversal with >
- Level 0 remains copy3

## Structural improvements
- new explicit Level-2 retention metric;
- acceptance requires L3 >=90% and L2/L1/L0 retention >=90%;
- checkpoint format V8 / version 3 stores curriculum_start_step;
- old Seed008 checkpoints remain backward compatible;
- on transition to L3, curriculum_start_step is set to the real current global step;
- UI can therefore report per-level steps correctly after update/restart;
- Level-3 batches: 50% L3, ~16.7% L2 replay, ~16.7% L1 replay, ~16.7% L0 replay;
- Level-3 learning rate is reduced to 0.65x base;
- test remains isolated from the training control loop.

## Gate correction
An initial diagnostic gate accidentally changed the already-verified L0/L1 trajectory and caused an L2 host failure. The gate was corrected to preserve the verified path:
- L0 fixed 220 steps;
- L1 fixed 400 steps;
- L2 adaptive as Seed008;
- checkpoint save -> new Engine -> reload;
- only then Level 3 adaptive training.

A JNI integration typo (missing r2 declaration in nativeTrainingEvaluate) was also caught after the learning gate passed and fixed before the final APK build.

## Final multi-seed gate
seed 174:
- L0 test: 97.78%
- L1 test: 100%
- L2 test: 100%
- L3 test: 100%
- L2 retention: 100%
- L1 retention: 100%
- L0 retention: 95.56%
- L3 started at step 920; accepted at step 980

seed 175:
- L0 test: 100%
- L1 test: 100%
- L2 test: 100%
- L3 test: 100%
- L2 retention: 100%
- L1 retention: 100%
- L0 retention: 100%
- L3 started at step 840; accepted at step 920

seed 176:
- L0 test: 100%
- L1 test: 100%
- L2 test: 91.67%
- L3 test: 100%
- L2 retention: 91.67%
- L1 retention: 100%
- L0 retention: 97.78%
- L3 started at step 820; accepted at step 1900

All final trajectories passed the >=90% gate for the new level and every retained level.

## Android build
GitHub Actions run: 37544418897
Source head: e316e55f59986f24d6abae04c6a108c35373c734
Artifact ID: 11450050810
Artifact ZIP SHA-256: 6de31ed8287afe0c8b0dae20befe63fde9c195c3680816d55801c82c77bb210a
APK SHA-256: 9ce7753944f452b07a4b9ced2df7bee05773e1597ac5277e38aa19092740b1f0
APK size: 4,755,415 bytes
Build result: PASSED

## Device acceptance
PENDING.
Install Seed 009 over Seed 008 without uninstalling. The existing real-device checkpoint should resume at step 1700, transition from Level 2 to Level 3, set the Level-3 start step to 1700, and preserve all learned weights.
Seed 008 remains the official accepted restore baseline until device training, generation and restart/resume verification pass.
