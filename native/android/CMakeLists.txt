cmake_minimum_required(VERSION 3.22)
project(thermalview LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(TV_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(TP ${TV_ROOT}/native/third_party)

# Debug APKs carry the overlay that measures the performance budget, so their native code is
# optimized too (symbols kept).
set(CMAKE_C_FLAGS_DEBUG "-O2 -g")
set(CMAKE_CXX_FLAGS_DEBUG "-O2 -g")

# Portable core (decode, command-gate logic, dump format, stats).
add_subdirectory(${TV_ROOT}/native/core ${CMAKE_CURRENT_BINARY_DIR}/core)

# libusb, pinned submodule, built like its android/jni/libusb.mk.
add_library(usb1 STATIC
  ${TP}/libusb/libusb/core.c
  ${TP}/libusb/libusb/descriptor.c
  ${TP}/libusb/libusb/hotplug.c
  ${TP}/libusb/libusb/io.c
  ${TP}/libusb/libusb/sync.c
  ${TP}/libusb/libusb/strerror.c
  ${TP}/libusb/libusb/os/linux_usbfs.c
  ${TP}/libusb/libusb/os/events_posix.c
  ${TP}/libusb/libusb/os/threads_posix.c
  ${TP}/libusb/libusb/os/linux_netlink.c
)
target_include_directories(usb1 PUBLIC ${TP}/libusb/libusb PRIVATE ${TP}/libusb/android ${TP}/libusb/libusb/os)
target_compile_options(usb1 PRIVATE -fvisibility=hidden -pthread -w)
target_link_libraries(usb1 PUBLIC log)

# libuvc, pinned submodule, without JPEG support (the camera streams uncompressed frames).
set(libuvc_VERSION_MAJOR 0)
set(libuvc_VERSION_MINOR 0)
set(libuvc_VERSION_PATCH 8)
set(libuvc_VERSION 0.0.8)
configure_file(${TP}/libuvc/include/libuvc/libuvc_config.h.in
               ${CMAKE_CURRENT_BINARY_DIR}/libuvc_generated/libuvc/libuvc_config.h)
add_library(uvc STATIC
  ${TP}/libuvc/src/ctrl.c
  ${TP}/libuvc/src/ctrl-gen.c
  ${TP}/libuvc/src/device.c
  ${TP}/libuvc/src/diag.c
  ${TP}/libuvc/src/frame.c
  ${TP}/libuvc/src/init.c
  ${TP}/libuvc/src/misc.c
  ${TP}/libuvc/src/stream.c
)
target_include_directories(uvc PUBLIC ${TP}/libuvc/include ${CMAKE_CURRENT_BINARY_DIR}/libuvc_generated)
target_compile_options(uvc PRIVATE -w)
target_link_libraries(uvc PUBLIC usb1)

add_library(thermalview SHARED
  command_sender.cpp
  field_log.cpp
  gpu_nlm.cpp
  gpu_nlm_shader.cpp
  jni_bridge.cpp
  perf_hint.cpp
  renderer.cpp
  session.cpp
)
target_compile_options(thermalview PRIVATE -Wall -Wextra)
target_link_libraries(thermalview PRIVATE thermalcore uvc usb1 android log EGL GLESv3)

# CLAUDE.md rule 1: fail every build if anything but command_sender.cpp writes a camera control.
add_custom_target(check_control_writes ALL
  COMMAND ${CMAKE_COMMAND} -DROOT=${TV_ROOT} -P ${TV_ROOT}/native/core/tests/check_control_writes.cmake
  COMMENT "Checking for camera control writes outside the command sender")
add_dependencies(thermalview check_control_writes)
