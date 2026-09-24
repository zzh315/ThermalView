// JNI surface for dev.thermalview.NativeBridge. Native threads never call back into Java; the UI
// polls status and overlay text instead.
#include <android/native_window_jni.h>
#include <jni.h>

#include <string>
#include <vector>

#include "field_log.h"
#include "session.h"

namespace {

std::string toString(JNIEnv* env, jstring s) {
  if (!s) return {};
  const char* chars = env->GetStringUTFChars(s, nullptr);
  std::string out = chars ? chars : "";
  if (chars) env->ReleaseStringUTFChars(s, chars);
  return out;
}

jstring toJava(JNIEnv* env, const std::string& s) { return env->NewStringUTF(s.c_str()); }

tv::Session& session() { return tv::Session::get(); }

}  // namespace

extern "C" {

JNIEXPORT void JNICALL Java_dev_thermalview_NativeBridge_init(JNIEnv* env, jobject, jstring storageDir,
                                                              jstring appVersion) {
  session().init(toString(env, storageDir), toString(env, appVersion));
}

JNIEXPORT jboolean JNICALL Java_dev_thermalview_NativeBridge_openCamera(JNIEnv* env, jobject, jint fd,
                                                                        jstring manufacturer,
                                                                        jstring product, jstring serial) {
  return session().openCamera(fd, toString(env, manufacturer), toString(env, product),
                              toString(env, serial));
}

JNIEXPORT void JNICALL Java_dev_thermalview_NativeBridge_closeCamera(JNIEnv*, jobject) {
  session().closeCamera();
}

JNIEXPORT void JNICALL Java_dev_thermalview_NativeBridge_setSurface(JNIEnv* env, jobject, jobject surface) {
  // ANativeWindow_fromSurface returns an acquired reference; the renderer takes ownership.
  session().setWindow(surface ? ANativeWindow_fromSurface(env, surface) : nullptr);
}

JNIEXPORT jstring JNICALL Java_dev_thermalview_NativeBridge_overlayText(JNIEnv* env, jobject) {
  return toJava(env, session().overlayText());
}

JNIEXPORT jstring JNICALL Java_dev_thermalview_NativeBridge_status(JNIEnv* env, jobject) {
  return toJava(env, session().statusLine());
}

JNIEXPORT jstring JNICALL Java_dev_thermalview_NativeBridge_startDump(JNIEnv* env, jobject, jint frames) {
  return toJava(env, session().startDump(frames));
}

JNIEXPORT void JNICALL Java_dev_thermalview_NativeBridge_registerDriftMap(JNIEnv* env, jobject, jstring serial,
                                                                          jbyteArray data) {
  const jsize n = env->GetArrayLength(data);
  jbyte* bytes = env->GetByteArrayElements(data, nullptr);
  session().registerDriftMap(toString(env, serial), tv::driftMapFromBytes(bytes, size_t(n)));
  env->ReleaseByteArrayElements(data, bytes, JNI_ABORT);
}

JNIEXPORT jstring JNICALL Java_dev_thermalview_NativeBridge_setPipeline(JNIEnv* env, jobject, jstring stages) {
  return toJava(env, session().setPipeline(toString(env, stages)));
}

JNIEXPORT jstring JNICALL Java_dev_thermalview_NativeBridge_setDisplay(JNIEnv* env, jobject, jint upscaler,
                                                                       jstring paletteJson) {
  return toJava(env, session().setDisplay(int(upscaler), toString(env, paletteJson)));
}

JNIEXPORT void JNICALL Java_dev_thermalview_NativeBridge_requestReadback(JNIEnv* env, jobject, jstring prefix,
                                                                          jstring palette) {
  session().requestReadback(toString(env, prefix), toString(env, palette));
}

JNIEXPORT jstring JNICALL Java_dev_thermalview_NativeBridge_startReplay(JNIEnv* env, jobject, jstring base) {
  return toJava(env, session().startReplay(toString(env, base)));
}

JNIEXPORT void JNICALL Java_dev_thermalview_NativeBridge_stopReplay(JNIEnv*, jobject) {
  session().stopReplay();
}

JNIEXPORT jstring JNICALL Java_dev_thermalview_NativeBridge_sendShutter(JNIEnv* env, jobject) {
  return toJava(env, session().sendShutter());
}

JNIEXPORT void JNICALL Java_dev_thermalview_NativeBridge_setOptions(JNIEnv*, jobject, jboolean skipStartupShutter,
                                                                    jboolean statsCsv, jboolean fallbackOrder,
                                                                    jboolean dumpOnLockout, jboolean autoRange,
                                                                    jboolean highMathInfiCam, jboolean lockoutEnabled,
                                                                    jint rangeSettleMs, jboolean bigCores) {
  session().setOptions({bool(skipStartupShutter), bool(statsCsv), bool(fallbackOrder), bool(dumpOnLockout),
                        bool(autoRange), bool(highMathInfiCam), bool(lockoutEnabled), int(rangeSettleMs),
                        bool(bigCores)});
}

JNIEXPORT jstring JNICALL Java_dev_thermalview_NativeBridge_setRange(JNIEnv* env, jobject, jboolean high) {
  return toJava(env, session().requestRange(bool(high)));
}

JNIEXPORT void JNICALL Java_dev_thermalview_NativeBridge_mark(JNIEnv* env, jobject, jstring label) {
  const char* s = env->GetStringUTFChars(label, nullptr);
  FLOG("owner mark: %s", s);
  env->ReleaseStringUTFChars(label, s);
}

JNIEXPORT jstring JNICALL Java_dev_thermalview_NativeBridge_readyCapture(JNIEnv* env, jobject, jstring label,
                                                                        jboolean rangePair) {
  const char* s = env->GetStringUTFChars(label, nullptr);
  const std::string text = s;
  env->ReleaseStringUTFChars(label, s);
  return toJava(env, session().requestCapture(text, bool(rangePair)));
}

JNIEXPORT jfloatArray JNICALL Java_dev_thermalview_NativeBridge_readouts(JNIEnv* env, jobject) {
  const std::vector<float> v = session().readouts();
  jfloatArray a = env->NewFloatArray(jsize(v.size()));
  if (a) env->SetFloatArrayRegion(a, 0, jsize(v.size()), v.data());
  return a;
}

JNIEXPORT jstring JNICALL Java_dev_thermalview_NativeBridge_triggerLockout(JNIEnv* env, jobject) {
  return env->NewStringUTF(session().triggerLockout().c_str());
}

}  // extern "C"
