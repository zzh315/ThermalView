#!/bin/sh
# Compiles and links the renderer's shaders (native/android/renderer.cpp) on the tablet's own GPU, in an
# off-screen GLES 3 context over adb: it works with the screen locked, before the app ever draws with
# them. Prints each shader's compile log, each program's link result and a few uniform locations.
#
#   tools/shader_check.sh [ADB]     (ADB: the adb command, e.g. a wrapper that picks the device)
set -e
ADB=${1:-adb}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NDK=$(ls -d "$HOME"/Library/Android/sdk/ndk/*/toolchains/llvm/prebuilt/darwin-x86_64/bin | tail -1)
TMP=$(mktemp -d)
python3 - "$ROOT/native/android/renderer.cpp" "$TMP" <<'PY'
import re, sys
s = open(sys.argv[1]).read()
for name, out in (("kVertexShader", "vert"), ("kFragmentShader", "frag"), ("kFallbackFragmentShader", "fallback")):
    m = re.search(r'constexpr const char\* ' + name + r' = R"\((.*?)\)";', s, re.S)
    open(f"{sys.argv[2]}/{out}.glsl", "w").write(m.group(1))
PY
"$NDK/aarch64-linux-android34-clang++" -O1 -static-libstdc++ -o "$TMP/shader_check" "$ROOT/tools/shader_check/shader_check.cpp" -lEGL -lGLESv3
for f in shader_check vert.glsl frag.glsl fallback.glsl; do $ADB push "$TMP/$f" "/data/local/tmp/tv_$f" >/dev/null; done
$ADB shell chmod 755 /data/local/tmp/tv_shader_check
$ADB shell /data/local/tmp/tv_shader_check /data/local/tmp/tv_vert.glsl /data/local/tmp/tv_frag.glsl /data/local/tmp/tv_fallback.glsl
$ADB shell rm -f /data/local/tmp/tv_shader_check /data/local/tmp/tv_vert.glsl /data/local/tmp/tv_frag.glsl /data/local/tmp/tv_fallback.glsl
rm -rf "$TMP"
