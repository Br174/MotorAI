# MotorAI Seed021R3R3 — Manual training and pause menu
Operation ID: MOTORAI-SEED021R3R3-TRAINING-MENU-20261011
User approved after report: expose "🧠 Impara manualmente" and "⏸️ Pausa allenamento" in the existing Laboratory ⋮ popup menu, preserve existing "Avvia/Riprendi training".
Cause: learn and pause Buttons were created and had click handlers but were not added to layout or the popup menu. Manual training code already exists, and foreground mutual exclusion already uses AtomicBoolean.compareAndSet(false,true) in both modes.
Change: only add two existing actions to popup and route to corresponding performClick, update versionCode 28->29 and versionName 0.21.5->0.21.6. No change to native engine, data, checkpoint, thresholds, scheduling or approved UI composition.
Safety: current/previous checkpoint and external SOS ZIP preserved; update in place with original lab signing configuration; keep R3R2 as last-good.
Test: static source/behavior wiring check, all existing CI native and Android build/sign gates, then device verification by user.
