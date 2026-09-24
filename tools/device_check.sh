#!/bin/zsh
# On-device check of the 2026-09-25 night's work, over adb, no camera needed (replays; docs/PLAN.md
# M5, PIPELINE_LOG stage 6, DEVICE.md "CPU"). Needs the new build installed, the tablet unlocked and
# the app in front.
#
#   ADB=<adb, or the wireless wrapper> tools/device_check.sh
#
# 1. Replays `keyboard` (the same dump as bench/keyboard) and logs the overlay's performance line
#    (fps, latency, proc p95, the processing thread's core and big-core share) for: stage 6 on with
#    the thread on the big cores, stage 6 on anywhere, stage 6 off.
# 2. Screenshots at each view size and palette into build/device_check/.
# 3. Runs the GPU-vs-CPU check (tools/py/gpu_check.py) at Full and at Phone.
set -u
ADB=${ADB:-adb}
ROOT=${0:A:h:h}
OUT=$ROOT/build/device_check
mkdir -p $OUT
DUMPS=/sdcard/Android/data/dev.thermalview/files/dumps
KEYBOARD=$DUMPS/dump_20260924_223255  # md5 matches bench/keyboard/thermalview.raw
app() { $ADB shell am start -n dev.thermalview/.MainActivity "$@" > /dev/null; }
perfline() {
  $ADB logcat -c
  app --ez logOverlay true
  sleep 1
  $ADB logcat -d -s ThermalView:I | grep -E "fps .*latency" | tail -1 | sed 's/.*ThermalView: *//'
}
app --ez stopReplay true
sleep 1
app --ez detail true --ez bigCores true --ei upscaler 1 --ei palette 0 --ei viewSize 2 --es replay $KEYBOARD
sleep 12
echo "stage 6 on, big cores:  $(perfline)"
app --ez bigCores false
sleep 12
echo "stage 6 on, any core:   $(perfline)"
app --ez bigCores true --ez detail false
sleep 12
echo "stage 6 off, big cores: $(perfline)"
app --ez detail true
sleep 2
for v in 2 1 0; do
  for p in 0 1 2; do
    app --ei viewSize $v --ei palette $p
    sleep 2
    $ADB exec-out screencap -p > $OUT/view${v}_palette${p}.png
  done
done
echo "screenshots: $OUT"
for v in 2 0; do
  app --ei viewSize $v --ei palette 1
  sleep 2
  $ROOT/tools/py/.venv/bin/python $ROOT/tools/py/gpu_check.py --adb $ADB
done
