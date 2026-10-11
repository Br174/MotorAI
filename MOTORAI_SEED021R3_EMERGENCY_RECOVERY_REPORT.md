# MotorAI Seed 021R3 — Recupero d'emergenza SOS

## Origine e autorizzazione
Bruno ha confermato la disinstallazione involontaria della vecchia app MotorAI. Dopo reinstallazione R2 è presente un checkpoint L4 a step 1760, con 0/10 Goal Mini-AI. Diagnostica del dispositivo: current=OK, previous=OK, auto-baseline=NO; Validation L4 0.8%, TEST L4 2.3%; memorie L0 95.6%, L1 100%, L2 95.8%, L3 100%. Il checkpoint pre-disinstallazione al 96% non è stato recuperato né esiste prova di un backup completo sul cloud. L'utente ha approvato un LAB di sola ispezione, esportazione e ripristino esplicito.

## Fonte di verità
Progetto Br174/MotorAI. Branch lab/seed021r3-emergency-recovery.
CI source commit d007530c0795222b0291948bd098ad4af516f208.
GitHub Actions https://github.com/Br174/MotorAI/actions/runs/38102582218 SUCCESS.
Artefatto id 11688491793, archive ZIP hash 93d8126d341a107323ee0627c3291de7b822f600b25f006618a6649225576961.
APK SHA256 7c76fa15c85758621294370bece7f77d99c8868a92c01e972e5c91dd2c5d0cbb, 6,109,999 bytes, versionCode 26, versionName 0.21.3-seed021r3.

## Interventi
- Default SOS mode paused with persistent preference and cancellation JobScheduler ids 11011/11012.
- MainActivity and background gate training and background jobs against SOS pause.
- New menu entry in existing Laboratorio opens MotorAIRecoveryActivity, without redesigning approved UI.
- Read-only inspect of current, previous, autotrain-baseline and previous-r2-safety, validates native checkpoint format, reports level, step, accuracy, nine goal step counts, checksum.
- Android SAF ACTION_CREATE_DOCUMENT exports ZIP of existing valid checkpoints and training preference snapshots, with per-entry SHA256 manifest; user should save to Drive/SSD.
- Android SAF ACTION_OPEN_DOCUMENT verifies a matching MotorAI ZIP, allowlist paths, size limits and hashes before offering user-confirmed import.
- Explicit restore previous or ZIP only after successful export in session and additional confirmation. Existing current is preserved in r3-pre-restore-protected. Training remains paused.
- The prior R2 / branch main and earlier restore references are not overwritten.

## Safety and remaining checks
GitHub CI gates passed (native L0-L5, 10 mini goals, Goal3 recovery, Goal10 integration, safe handoff, Android build/sign/upload, emergency source gate).
Real-device export/import and GUI acceptance have NOT been performed; no claim of restored 96%.
Emergency ZIP is not encrypted. Keep private. Do not confuse CLI artifact ZIP with a user-created backup.
Next: install R3 over installed R2 without uninstall/wipe. Inspect current/previous; export ZIP outside app; user supplies screenshot; compare before any restore or training. Keep original R2 and R1 as safety references.
