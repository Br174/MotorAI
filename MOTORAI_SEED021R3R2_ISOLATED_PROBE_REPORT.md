# MotorAI Seed021R3R2 — isolated checkpoint probe
OPERATION_ID=MOTORAI-SEED021R3R2-ISOLATED-PROBE-20261011
OWNER_APPROVAL=OK to read-only native verification of previous
BASELINE=R3R1 certified, unchanged
TARGET=Historical MOTAI008 checkpoint previous; current L4 step 1760 unchanged
BEHAVIOR=Fresh motorai::Engine in native JNI, never g_engine.loadCheckpoint
SAFETY=SOS pause required; checkpoint filesystem mutex; SHA-256 both weights and metadata before/after for current/previous; no restore, no training, no preference writes
RISK=Host historic synthetic fixture cannot prove particular real-world backup will load
TEST=Native historic load/corrupt reject/live preservation, Android compile/sign, all previous gates
VERSION=0.21.5-seed021r3r2, versionCode28; package ID and signing unchanged
NEXT=Install over R3R1, keep training paused, press isolated PREVIOUS test, send output screenshot, do not restore.
