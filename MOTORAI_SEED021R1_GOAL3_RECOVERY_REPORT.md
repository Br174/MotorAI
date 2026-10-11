# MotorAI Seed021R1 — Goal3 anti-loop LAB

Operation: MOTORAI-SEED021R1-GOAL3-RECOVERY-20261011

Reported on-device: Goal1 and Goal2 accepted; Goal3 at 73%, native memory validation
dropped at step 60; rollback restored prior checkpoint; identical cycle repeated.

Change Compatibility Guard: the existing MiniAiTrainingCoordinator, the native
Goal3 brain, the Android checkpoint mechanism and JobScheduler remain authoritative.
The new Goal3Recovery is a narrow **policy helper** shared between the existing two
execution modes. It does not create another runner, scheduler, checkpoint writer,
neural model, completion gate or registry.

- Introduce persistent bounded adaptive Goal3 learning rate (0.04, 0.02, 0.01,
  0.005, 0.0025), so identical deterministic rejected chunks are not re-run with
  identical parameters after rollback.
- Stop rather than spin if five severe regressions recur; retain prior checkpoint
  and stored progress, surface explicit pause/error and require further diagnosis.
- Preserve existing 20 percentage-point severe-regression guard, 90% memory
  validation (4 stable chunks), 90% independent final test and 2500-step cap.
- Share recovery attempt state through pre-existing motorai_runtime preferences
  and clear only after acceptance; no automatic reset on each foreground resume.
- Fix stale green training indication and incorrect static Goal 1 title without
  layout changes.
- Preserve Android update family applicationId/signing configuration and increment
  versionCode 23 -> 24. Never uninstall to test.

Safety: previous main SHA fb5a6aabc603f5fb84a1a80ef7c4eac678868dad
and current certified Seed021 source remain untouched; no promotion to main until
real-device acceptance. Android rollback safety ref is the previous main SHA.
