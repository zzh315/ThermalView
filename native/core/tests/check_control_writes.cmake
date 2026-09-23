# CLAUDE.md rule 1: outside native/third_party, only the command sender may call a uvc_set_*
# function or libusb_control_transfer, and no Python tool may issue a control transfer.
# Usage: cmake -DROOT=<repo root> -P check_control_writes.cmake (CTest and the Android build run it).

if(NOT ROOT)
  message(FATAL_ERROR "pass -DROOT=<repo root>")
endif()

set(allowed "${ROOT}/native/android/command_sender.cpp")
file(GLOB_RECURSE native_sources LIST_DIRECTORIES false
  "${ROOT}/native/*.c" "${ROOT}/native/*.cc" "${ROOT}/native/*.cpp" "${ROOT}/native/*.h"
  "${ROOT}/native/*.hpp" "${ROOT}/app/*.kt" "${ROOT}/app/*.java" "${ROOT}/tools/*.c"
  "${ROOT}/tools/*.cpp" "${ROOT}/tools/*.h")
file(GLOB_RECURSE python_sources LIST_DIRECTORIES false "${ROOT}/tools/*.py")

set(violations "")
foreach(f IN LISTS native_sources)
  if(f MATCHES "/native/third_party/" OR f MATCHES "/build/" OR f MATCHES "/\\.cxx/" OR f STREQUAL allowed)
    continue()
  endif()
  file(STRINGS "${f}" hits REGEX "(uvc_set_[A-Za-z0-9_]+|libusb_control_transfer)[ \t]*\\(")
  if(hits)
    string(APPEND violations "\n  ${f}: ${hits}")
  endif()
endforeach()
foreach(f IN LISTS python_sources)
  if(f MATCHES "/\\.venv/")
    continue()
  endif()
  file(STRINGS "${f}" hits REGEX "ctrl_transfer[ \t]*\\(")
  if(hits)
    string(APPEND violations "\n  ${f}: ${hits}")
  endif()
endforeach()

if(violations)
  message(FATAL_ERROR "Camera control writes outside the command sender (CLAUDE.md rule 1):${violations}")
endif()
message(STATUS "No camera control writes outside ${allowed}")
