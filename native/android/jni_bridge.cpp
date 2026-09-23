// JNI surface for dev.thermalview.NativeBridge. Native threads never call back into Java; the UI
// polls status and overlay text instead.
#include <android/native_window_jni.h>
#include <jni.h>

#include <string>

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
                                                                    jboolean dumpOnLockout) {
  session().setOptions({bool(skipStartupShutter), bool(statsCsv), bool(fallbackOrder), bool(dumpOnLockout)});
}

JNIEXPORT jstring JNICALL Java_dev_thermalview_NativeBridge_triggerLockout(JNIEnv* env, jobject) {
  return env->NewStringUTF(session().triggerLockout().c_str());
}

}  // extern "C"
