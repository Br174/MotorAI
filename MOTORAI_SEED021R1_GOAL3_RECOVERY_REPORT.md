# MotorAI Seed021R1 — Goal3 anti-loop LAB

Operation: MOTORAI-SEED021R1-GOAL3-RECOVERY-20261011

Reported on-device: Goal1 and Goal2 accepted; Goal3 at 73%, native memory validation
dropped at step 60; rollback restored prior checkpoint; identical cycle repeated.

Change Compatibility Guard: the existing MiniAiTrainingCoordinator, the native
Goal3 brain, the Android checkpoint mechanism and JobScheduler remain authoritative.
The new Goal3Recovery is a narrow **policy helper** shared between the existing two
execution modes. It does not create another runner, scheduler, checkpoint writer,
neural model, completion gate or registry.

- Use normal Goal3 learning rate 0.08, adaptive recovery rates 0.04, 0.02, 0.01, 0.005, 0.0025. After four consecutive safe chunks, return to the normal rate to prevent chronic under-training. An identical rejected chunk is not replayed at the same rate.
- Stop rather than spin if five recovery retries fail; retain prior checkpoint
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

CI FINAL: VERIFIED SUCCESS. GitHub Actions 38099248753, artifact 11686548704.
CERTIFIED APP SOURCE HEAD: 641c8b3b028a5deea30fb07079950a79e1aa69ae.
APK SHA256: 222deec86e610b871fb0f2e71f858222b4b5ebd8764a906d8200b12174b7bc6d.
ZIP GitHub SHA256: 4a83cd1e3c792403bdbb15a320389160f68350193c53e89f6c57732e43e1e4f1.
NATIVE RECOVERY BENCHMARK: seeds 174/175/176 all PASS after simulated step-60 reject, each 91.6667% independent test accuracy; no real-device acceptance claimed.
DEVICE ACCEPTANCE: PENDING. Keep previous Seed021 main and accepted restore Seed010 unchanged.
