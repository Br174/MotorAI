# MotorAI Seed 006 — Curriculum Level 1 Report

Date: 2026-10-07

## Starting point
Seed 005 is the accepted Android baseline:
- real-device step: 220
- real-device test loss: 0.8275
- real-device copy3 generalization: 100%
- checkpoint resume: verified
- generation: abc> -> abc>abc
- update-in-place signing: verified

Seed 005 remains a frozen rollback point.

## Goal
Teach a genuinely new rule while preserving the already learned Level 0 capability.
Acceptance gate: new-level accuracy >= 90% AND Level-0 retention >= 90%.

## Adaptive curriculum experiments
All host gates used seed 174 and reproduced Level 0 first.

1. Multi-rule Level 1 (copy2 + reverse2 + sort2), 400 steps
   - Level 1: 61.11%
   - Level 0 retention: 75.56%
   - REJECTED

2. More replay + optimizer-moment reset, 600 steps
   - Level 1: 58.33%
   - Level 0 retention: 84.44%
   - REJECTED

3. Two-rule Level 1 (copy2 + reverse2), 400 steps
   - Level 1: 54.17%
   - Level 0 retention: 82.22%
   - REJECTED

4. Answer-targeted Level-1 loss, 400 steps
   - Level 1: 66.67%
   - Level 0 retention: 84.44%
   - REJECTED

5. Same objective, 800 steps
   - Level 1: 62.50%
   - Level 0 retention: 80.00%
   - REJECTED
   - Conclusion: simply adding more steps does not solve the curriculum conflict.

6. Adaptive single-rule Level 1: reverse an unseen ordered pair, with 1:1 Level-0 replay
   - Example: ef> -> ef>fe
   - 72 distinct ordered pairs; 48 train / 12 validation / 12 test
   - Level-0 copy3 replay: 48 examples
   - 400 Level-1 steps
   - Level 0 before switch: 97.78%
   - Level 1 test accuracy: 100.00%
   - Level 1 test loss: 3.5911
   - Level 0 retention after Level 1: 100.00%
   - total host step: 620
   - ACCEPTED BY PRE-APK GATE

## Technical changes
- checkpoint V5 / MOTORAI_CHECKPOINT_NATIVE_V2 stores curriculum level;
- backward-compatible loader accepts Seed005 V4 checkpoints;
- Seed005 checkpoint at step >=220 auto-transitions to curriculum Level 1 without resetting weights;
- optimizer moments reset only at curriculum transition; learned weights remain intact;
- Level 1 training loss focuses on answer targets plus end-of-line;
- validation regression rollback retained;
- catastrophic-forgetting rollback added;
- Level-0 retention is displayed separately in Android UI;
- stable LAB signing remains unchanged.

## Build
GitHub Actions run: 37539705963
Source head: 1a120eb1a23070aeaa3bf4d8e5a5dc207beb9d89
Artifact ID: 11447653277
Artifact digest SHA-256: 4401aa1a4209cc5c861daf505006d2af3b710d8bd4bd56878e186935f3b96c73
Extracted APK SHA-256: 2b20e5480c39b64dd4d548de54721615e2dc3bc37ce0a69cb06cc964e195ac44
APK size: 4,708,103 bytes

## Acceptance state
Host curriculum gate: PASSED.
Android build: PASSED.
Real-device Level-1 training: PENDING.
Seed 006 must not replace Seed 005 as the accepted restore baseline until real-device verification.
