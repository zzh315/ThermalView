#pragma once

#include <android/log.h>

#define TV_LOG_TAG "ThermalView"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TV_LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TV_LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TV_LOG_TAG, __VA_ARGS__)
