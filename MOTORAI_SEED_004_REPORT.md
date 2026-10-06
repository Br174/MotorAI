# MotorAI Seed 004 — Root Cause Fix Report

Date: 2026-10-06

## Trigger
Real-device Seed 003 training reached step 220 with test loss 27.6310 and generalization 0.0%, down from initial loss 9.3653 and 2.2%.

## Confirmed causes / weaknesses
1. Autograd backward closures captured their own shared_ptr node, creating reference cycles and graph-memory leaks.
2. Random normal initialization, shuffle and batch sampling depended on standard-library implementation details, so host and Android were not guaranteed to execute the same seeded experiment.
3. Learning rate 0.01 was unnecessarily aggressive for cross-platform robustness.
4. There was no validation-based automatic rollback when a training block regressed sharply.
5. Seed 003 used an ephemeral GitHub debug signing identity, unsuitable for update-family continuity.

## Seed 004 fixes
- remove autograd self-ownership cycles;
- deterministic RNG-derived initialization, shuffle and sampling;
- learning rate 0.001;
- remove -ffast-math;
- validation-based regression rollback;
- checkpoint magic V4, rejecting Seed003 bad checkpoint format;
- stable public LAB-only signing identity for future laboratory updates;
- versionCode 4 / versionName 0.4.0-seed004.

## Verification
- 30/30 host seeds improved after 220 steps at lr 0.001;
- corrected repeated test peak RSS ~1.9 MB;
- seed 174: test loss 2.419345 -> 0.917470, accuracy 11.1% -> 97.8%;
- GitHub Actions final run 37537239081: SUCCESS;
- artifact 11447405080;
- artifact SHA-256: b448f245bea21afceea21a75d4a500533451b44c76991d76f8aad620a98d60d8;
- extracted APK SHA-256: 0e941a8cc1b92b926727a05ce8de393fa5755d68509e588ea2eca94c1a802031.

## Acceptance
Pending real-device Seed 004 training benchmark. Seed 003 remains rejected and must not be promoted.
