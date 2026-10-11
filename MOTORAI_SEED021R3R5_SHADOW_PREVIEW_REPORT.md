# MotorAI Seed 021R3R5: Goal1 shadow-training preview (not promotion)
Date 2026-10-11. Parent: R3R4 Goal10 read-only diagnosis.
Real phone: nine Goal10 tests PASS, only Goal1 comprehension fails despite
correct intent=calcolo, confidence=0.3706 below unchanged cutoff 0.50.
Goal1 original recovery previously rolled back and exhausted retries.

User approved safe staged repair; in this release the proposed Goal1
fine-tuning is tested solely on a fresh separate Engine loaded from CURRENT.
The active native Engine is never trained, loaded, saved, or replaced.
Training data remain the original TRAIN split. Validation stops the candidate;
Goal1 TEST and the known integration prompt are evaluated only after training.
Independent generic paraphrases must also be recognized, and validation/test
cannot worsen. A successful candidate is saved to a separate diagnostic
directory and can be inspected, but NOT PROMOTED or AUTO-RESTORED.
Java checks exact SHA-256 current weights and metadata before/after trial,
verifies source sync with active weights, and requires Goal10 forensic hold.
Goal10 remains paused in both UI and background, with no changes to user
progress, accepted Goals, neural training schema, or thresholds.
Code 31, signed app update-in-place, APK + ZIP only after full CI.
Device acceptance pending. If insufficient confidence, no candidate created.
