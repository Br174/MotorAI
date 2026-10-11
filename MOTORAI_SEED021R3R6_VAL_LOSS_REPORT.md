# MotorAI Seed021R3R6 — Goal1 shadow continuation by validation loss

Owner-supplied real device report 11 Oct 2026 09:11:53:
Goal10 stays at 9/10 integrated checks; comprehension intent 'calcolo' is
correct but confidence 0.3706 (<0.50).
R3R5 shadow-only test succeeded in preserving active CURRENT SHA-256 and
Goal1 Validation/Test accuracy at 95%, improving confidence 0.3706 to
0.4074 after 320 steps. No candidate was committed or promoted.
Root cause: R3R5 early stopped after 16 20-step chunks with identical
validation accuracy (95%). The discrete accuracy plateau does not imply
validation loss or classifier confidence have stopped improving.

R3R6 minimal fix:
- Continue until validation cross-entropy loss stops improving, without
  looking at held-out final TEST or the Goal10 challenge while training.
- Stop safely on accuracy regression, non-finite loss, persistent loss
  worsening, bounded validation-loss plateau, 12 seconds or 3000 steps.
- Run final TEST and challenge only once after training stops; ensure
  original validation/test accuracy don't decline, validation loss stays
  acceptable, unseen paraphrase checks pass and comprehension confidence
  meets original >= 0.50 threshold.
- Return validation loss before/after and stop reason, not just accuracy.
- Keep Goal10 training on hold, current weights/checkpoint read-only;
  only verified separate candidate may be saved, never auto-promoted.
- Existing LAB identity, signing and install-in-place, versionCode32.
- No lower thresholds; do not claim 100% or code CI equals real-device proof.

Pending: full CI and Android trial of new loss-based algorithm; backup first.
