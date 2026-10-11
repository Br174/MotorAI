# MotorAI Seed 021R3R8 — Goal10 guarded live promotion

Date: 2026-10-11
Operation: MOTORAI-GOAL10-R3R8-GUARDED-PROMOTION-20261011
Source: R3R7 isolated on-device candidate certification at 09:46:34.
Model main: 9/10 (comprehension 37.06% < 50%).
Candidate isolated: 10/10 (comprehension 70.28%).
Goal1 TEST unchanged 95.00%; validation 95.00% -> 97.50%.
Other nine integrated answers identical; CURRENT and candidate checkpoint SHA-256 unchanged.
These on-device facts are user-provided, not yet replicated on R3R8.

## Change boundary

- Reuses R3R7 candidate from motorai/goal10-shadow-candidate-r3r5.
- Adds R3R8 promotion button to existing diagnostics (no UI redesign).
- Requires existing SOS pause and nine goals completed.
- Re-runs strict independent candidate certification before any active change.
- Creates *new* immutable separate original backup from CURRENT; NEVER touches PREVIOUS.
- Loads candidate only after backup has been verified; no further training.
- Compares native L0-L5 foundation accuracy/retention to baseline.
- Re-runs all 10 Goal10 checks on candidate *as active native model*.
- Saves candidate to new staging directory, probes its readability, and checks all SHA-256 fingerprints.
- Moves original CURRENT into a permanent transit backup and promotes staged checkpoint to CURRENT.
- Certifies Goal10=100% only LAST, after further live 10/10.
- On rejection, restores original engine and filesystem checkpoint with explicit status.
- On interrupted update, the next app launch attempts safe original recovery BEFORE usual checkpoint resume.
- Keeps current external SOS ZIP as user-managed secondary backup; never overwrites it.
- Maintains paused training until separately authorized.
- Version 0.21.11 / versionCode 34, package it.motorai.seed, stable LAB debug signing.

## Limitations and acceptance criteria

On-device certification is required. GitHub CI only validates source safety and synthetic/native smoke tests;
it does not prove the user's real checkpoint transitions.

After installing update-in-place over R3R7, retain SOS paused and backup ZIP.
Open Diagnostics, tap 'Promuovi candidata Goal 10 (protetto)', confirm, wait for result,
copy diagnostics. Do not use old recovery or train functions while testing.
ACCEPT only if real live Goal10 10/10, checkpoint CURRENT remains readable, L0-L5
remain >= baseline (and >= 90%), prior nine capabilities unchanged,
PREVIOUS preserved, and restart/resume reproduces 10/10.
If stopped, do not uninstall; send the exact text.
