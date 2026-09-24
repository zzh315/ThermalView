cmake_minimum_required(VERSION 3.22)
project(thermalcore LANGUAGES CXX)

# Portable core: no Android or GL dependencies, so it builds and tests on the Mac (CLAUDE.md).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(thermalcore STATIC
  src/command_gate.cpp
  src/dump.cpp
  src/frame.cpp
  src/readouts.cpp
  src/stats.cpp
  src/temperature.cpp
)
target_include_directories(thermalcore PUBLIC include)
target_compile_options(thermalcore PRIVATE -Wall -Wextra -Wpedantic)

if(PROJECT_IS_TOP_LEVEL)
  include(CTest)
  set(TV_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
  add_executable(core_tests
    tests/main.cpp
    tests/test_command_gate.cpp
    tests/test_dump.cpp
    tests/test_frame.cpp
    tests/test_queues.cpp
    tests/test_readouts.cpp
    tests/test_stats.cpp
    tests/test_temperature.cpp
  )
  target_include_directories(core_tests PRIVATE ${TV_ROOT}/native/third_party/doctest/doctest)
  target_link_libraries(core_tests PRIVATE thermalcore)
  target_compile_definitions(core_tests PRIVATE TV_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden")
  target_compile_options(core_tests PRIVATE -Wall -Wextra)
  add_test(NAME core_tests COMMAND core_tests)
  add_test(NAME no_stray_control_writes
    COMMAND ${CMAKE_COMMAND} -DROOT=${TV_ROOT} -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_control_writes.cmake)
endif()
