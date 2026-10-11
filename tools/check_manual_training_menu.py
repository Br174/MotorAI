#!/usr/bin/env python3
"""Verify actual UI exposes already-wired training actions and a single training owner."""
import pathlib
import re
s = pathlib.Path("android/app/src/main/java/it/motorai/seed/MainActivity.java").read_text()
assert s.count('p.getMenu().add("🧠 Impara manualmente")') == 1
assert s.count('p.getMenu().add("⏸️ Pausa allenamento")') == 1
assert s.count('else if ("🧠 Impara manualmente".equals(title)) learn.performClick()') == 1
assert s.count('else if ("⏸️ Pausa allenamento".equals(title)) pause.performClick()') == 1
assert s.count('learn.setOnClickListener(v -> startTraining())') == 1
assert s.count('pause.setOnClickListener(v -> stopTraining("Pausa richiesta"))') == 1
assert s.count('autoTrain.setOnClickListener(v -> startAutoTraining())') == 1
assert s.count('if (!training.compareAndSet(false, true)) return;') == 2
assert 'if (MotorAIRecoveryMode.paused(this))' in s
assert 'button("Riparti da pesi casuali")' in s
g = pathlib.Path("android/app/build.gradle.kts").read_text()
assert "versionCode = 30" in g
assert 'versionName = "0.21.7-seed021r3r4"' in g
print("PASS: manual and pause visible, existing handler reused, dual-training guard intact")
