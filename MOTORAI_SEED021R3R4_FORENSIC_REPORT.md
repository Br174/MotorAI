MotorAI Seed 021R3R4 — First-stage Goal10 forensic diagnostics
Operation: MOTORAI-SEED021R3R4-GOAL10-RESCUE-20261011
Owner approved full staged repair based on screenshot at 05:12:
nine of ten Goals accepted; Goal10 6/10; failed comprehension, memory,
autocontrollo-calcolo, autocontrollo-fonte; at 05:10:34 automatic recovery
rolled back and blocked Goal1 after regression.
Scope stage A:
- Add a strictly read-only Goal10 inspector that runs the same ten native
  probes as the certifier and reports actual JSON vs expected and confidence.
- Show a new Diagnostics button and include details in Copy diagnostics.
- No changes to the native neural engine, training data, checkpoint rotation,
  nine completed Goals, progress preferences or acceptance thresholds.
- Update-in-place signed LAB APK: code 30, version 0.21.7.
- Enforce CI code-level read-only check plus existing benchmarks and APK build.
- Stage B: after actual on-device read-only results, repair ONLY demonstrated
  failure causes using regression-safe bounded retries and independent tests.
- Never train on exact final-test strings to fabricate generalization.
- Keep 96% external SOS backup; no uninstall, reset, auto-unblock or restore.
