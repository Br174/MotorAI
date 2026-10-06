# MotorAI Seed 007 — Curriculum Level 2 Report

Date: 2026-10-07

## Starting baseline
Seed 006 is the accepted real-device baseline:
- step 620
- Level 1 generalization: 100%
- Level 0 retention: 100%
- restart/resume verified
- Level 1 generation ef> -> ef>fe verified

## Level 2 goal
Add a third capability while preserving both prior levels.

Final accepted Level-2 rule:
- input example: ef>>
- expected output: ef>>ee
- operation: select the first symbol and duplicate it
- train/validation/test use unseen ordered input pairs
- L0 and L1 replay remain active

The harder pair-sorting task was tested first and deferred to a future level because it did not pass the pre-APK gate with the current 9,536-parameter brain.

## Adaptive experiments
1. Full unseen-pair sort, 400 L2 steps
   - L2: 66.67%
   - L1 retention: 100%
   - L0 retention: 88.89%
   - REJECTED

2. Rebalanced replay + gentler L2 learning rate, 600 steps
   - L2: 79.17%
   - L1 retention: 100%
   - L0 retention: 97.78%
   - REJECTED

3. Same sort curriculum, 1,000 steps
   - L2: 79.17%
   - L1 retention: 100%
   - L0 retention: 97.78%
   - REJECTED; more steps did not help

4. Orientation-generalization sort stage
   - L2: 20.83%
   - L1 retention: 100%
   - L0 retention: 95.56%
   - REJECTED

5. Adaptive Level 2: first-symbol selection + duplication, 600 steps
   - L2: 91.67%
   - L1 retention: 100%
   - L0 retention: 100%
   - total benchmark step: 1,220
   - ACCEPTED BY PRE-APK GATE

## Technical changes
- curriculum levels now support 0, 1 and 2;
- checkpoint V6 is backward-compatible with Seed006 checkpoint V5;
- Seed006 step-620 Level-1 checkpoint auto-transitions to Level 2 without resetting learned weights;
- separate Level-0 and Level-1 retention metrics;
- rollback watches both prior capabilities at Level 2;
- Level-2 learning rate is 0.75x base LR;
- replay is weighted to preserve earlier levels;
- stable LAB signing unchanged.

## Build
GitHub Actions run: 37541630320
Build source head: 4dd7c57048bc8160cce8af8142d21d499491a749
Artifact ID: 11448741123
Artifact SHA-256: afae089bf71840ed59e0fe31bef2e067c7562f09f4bbe048bb8829a3239a8c9e
APK SHA-256: 24af190b674348c916a22c2ed82fa2ba89f4801908a316c13a67118064274514
APK size: 4,728,295 bytes

## Acceptance
Host curriculum gate: PASSED.
Android build: PASSED.
Real-device Level-2 acceptance: PENDING.
Seed 006 remains the accepted restore baseline until Seed 007 passes device training, generation and restart/resume checks.
