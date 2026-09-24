#!/bin/bash
# Captures one benchmark scene (docs/PLAN.md M3) into bench/<scene>/:
#   1. ThermalView (debug build, "Ready records a capture" on) waits for the owner's Ready tap, then
#      records 200 frames after a recalibration; the dump is pulled as thermalview.raw/.json.
#   2. Each reference app in turn: launched over adb, the owner taps Allow for the camera, it settles,
#      then a screenshot (<app>_full.png) and a 10 s screen recording (<app>.mp4).
#   3. ThermalView comes back to the front for the next scene.
# Afterwards: tools/py/bench_crop.py bench/<scene>, then tools/py/bench.py.
#
#   ADB=/path/to/adb tools/bench_scene.sh SCENE [SETTLE_SECONDS]   (REFS_ONLY=1 skips step 1)
#
# macOS ships bash 3.2: no `case` inside $(...), hence shortname().
ADB=${ADB:-adb}
REPO=$(cd "$(dirname "$0")/.." && pwd)
SCENE="$1"; SETTLE=${2:-45}
[ -n "$SCENE" ] || { echo "usage: tools/bench_scene.sh SCENE [SETTLE_SECONDS]" >&2; exit 2; }
OUT="$REPO/bench/$SCENE"; mkdir -p "$OUT"
D=/sdcard/Android/data/dev.thermalview/files/dumps
top() { "$ADB" shell dumpsys activity activities 2>/dev/null | tr -d '\r' | awk '/topResumedActivity/{print $3; exit}' | cut -d/ -f1; }
log() { echo "$(date +%H:%M:%S) $*"; }
shortname() { case "$1" in com.hti.Xtherm) echo hti;; com.infiRay.Xtherm) echo xtherm;; *) echo inficamplus;; esac; }

if [ -z "$REFS_ONLY" ]; then
  if [ "$(top)" != dev.thermalview ]; then
    "$ADB" shell monkey -p dev.thermalview -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1; sleep 8
  fi
  base=$("$ADB" logcat -d -s 'ThermalView:*' 2>/dev/null | grep -c "capture: done")
  log "waiting for Ready in ThermalView"
  for i in $(seq 1 600); do
    n=$("$ADB" logcat -d -s 'ThermalView:*' 2>/dev/null | grep -c "capture: done"); [ "$n" -gt "$base" ] && break; sleep 2
  done
  name=$("$ADB" logcat -d -s 'ThermalView:*' 2>/dev/null | tr -d '\r' | grep "capture: recording" | tail -1 | sed 's/.*recording //')
  sleep 3
  "$ADB" pull "$D/$name.raw" "$OUT/thermalview.raw" >/dev/null 2>&1 &&
    "$ADB" pull "$D/$name.json" "$OUT/thermalview.json" >/dev/null 2>&1 &&
    log "ThermalView dump $name -> bench/$SCENE"
fi

# InfiCamPlus goes last: it writes the camera's user area (volatile; a replug clears it).
prev=dev.thermalview
for app in com.hti.Xtherm com.infiRay.Xtherm be.ntmn.inficam; do
  short=$(shortname "$app")
  "$ADB" shell am force-stop "$prev"
  "$ADB" shell monkey -p "$app" -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
  log "$short launched; waiting for it to own the screen (tap Allow)"
  for i in $(seq 1 120); do [ "$(top)" = "$app" ] && break; sleep 2; done
  log "$short in front; settling ${SETTLE}s"; sleep "$SETTLE"
  "$ADB" exec-out screencap -p > "$OUT/${short}_full.png" 2>/dev/null
  "$ADB" shell screenrecord --time-limit 10 /sdcard/Download/tv_bench.mp4 >/dev/null 2>&1
  "$ADB" pull /sdcard/Download/tv_bench.mp4 "$OUT/$short.mp4" >/dev/null 2>&1
  "$ADB" shell rm -f /sdcard/Download/tv_bench.mp4
  log "$short captured (screenshot + 10 s clip)"
  prev=$app
done

"$ADB" shell am force-stop "$prev"
"$ADB" shell monkey -p dev.thermalview -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
log "scene $SCENE done"; ls -la "$OUT"
