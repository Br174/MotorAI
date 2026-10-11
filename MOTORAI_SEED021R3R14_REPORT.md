# MotorAI Seed021R3R14 — paired hard negatives and a two-stage router prototype
Operation ID: MOTORAI-R3R14-CONTRASTIVE-SHADOW-TWO-STAGE-20261011

## Confirmed baseline
R3R13 Android 11/10/2026 12:45:17:
- 50 known probes, 37 correct (74%), 13 wrong, 0 errors.
- 20 previous final examples 15/20; 24 guard examples 19/24; six historical 3/6.
- Goal1 saluto 4 wrong; ricerca 1 wrong.
- Goal7 calculator→search 4, chat→calculator 2, memory→search 2.
- 50/50 softmax winning class confidence below 0.5: NOT calibrated certainty.
- Goal10 MAIN 10/10, CURRENT/PREVIOUS/auto-baseline OK, SOS pause enabled.
- These 50 examples have now been disclosed and MUST NOT be called novel tests.

## R3R14 authorized scope
1. Contrastively PAIR examples in TRAIN only, for Goal1 and Goal7:
   similar wording / opposing intent labels. Keep native 512-feature linear
   softmax classifier architecture, do not claim a newly implemented
   contrastive embedding loss. No pretrained weights or paid AI API.
2. Shadow copies loaded from CURRENT (native mutex + Java recovery lock),
   isolated training of Goal1 and Goal7; neither model MAIN nor Goal
   progress, checkpoint CURRENT, PREVIOUS, auto-baseline, SOS are changed.
3. Selection uses native validation plus NEW 15 Goal1 developmental
   examples (five classes ×3) and NEW 24 Goal7 developmental examples
   (four classes ×6). Acceptance requires per-category non-regression.
4. NEW 24 final held-out prompts (Goal1 12 / Goal7 12), measured once
   AFTER selecting weights. Historic six R3R9 cases checked for regressions.
   Original TEST evaluated only after selection (not used to tune weights).
5. Two-stage PROTOTYPE evaluated on Goal7 12 holdout cases: broad
   Goal1 intent then Goal7 tool route; it does NOT replace the live router.
   Both raw model and staged performance reported before/after. Eligibility
   requires new raw performance to improve, staged score not decrease,
   and validation + original TEST + Goal10 remain intact.
6. Battery temp >=40°C blocks isolated training; pause SOS mandatory.
   Qualified shadow checkpoint saved at separate path only; NO promotion.

## Limitations
Synthetic and small test sets; no proof of general intelligence or
real-world end-to-end tool use. Novel means exact strings held out,
NOT that the semantic patterns are entirely unseen.
The held-out result may remain unchanged or regress; failure must
REJECT shadow weights without retries/promotions.
Prototypical two-stage routing is deliberately non-persistent.

## Android deliverable
Build family code40 / 0.21.17-seed021r3r14, package it.motorai.seed,
same stable LAB signature. In-place install over R3R13, preserve data.
Existing UI styling unchanged, new action in Diagnostics, older
R3R13 read-only 50-case inspection retained.
APK direct and root-APK ZIP plus full source ZIP.
On-device acceptance: install above R3R13, SOS stays paused,
Diagnostics → R3R14 experiment, send full copied report.
