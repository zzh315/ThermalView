cmake_minimum_required(VERSION 3.22)
# Optimized unless asked otherwise: the harness chews through whole benchmark dumps.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
project(thermalview_harness LANGUAGES CXX)

# Mac CLI that runs native/core on recorded dumps (CLAUDE.md "Repo layout"; docs/PLAN.md M2, M3).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../native/core ${CMAKE_CURRENT_BINARY_DIR}/core)
add_executable(harness src/main.cpp)
target_link_libraries(harness PRIVATE thermalcore)
target_compile_options(harness PRIVATE -Wall -Wextra)
target_compile_definitions(harness PRIVATE TV_REPO_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../..")

# native/core's noise filters for the Python tools (tools/py/nr_detail.py, via ctypes).
set_property(TARGET thermalcore PROPERTY POSITION_INDEPENDENT_CODE ON)
add_library(tvnr SHARED src/nrlib.cpp)
target_link_libraries(tvnr PRIVATE thermalcore)
target_compile_options(tvnr PRIVATE -Wall -Wextra)

# Android only: the GPU BM3D on its own from the adb shell (src/gpu_bench.cpp), to time its shaders
# without the app.
if(ANDROID)
  set(TV_ANDROID ${CMAKE_CURRENT_SOURCE_DIR}/../../native/android)
  add_executable(gpu_bench src/gpu_bench.cpp ${TV_ANDROID}/gpu_bm3d.cpp ${TV_ANDROID}/gpu_bm3d_shader.cpp)
  target_include_directories(gpu_bench PRIVATE ${TV_ANDROID})
  target_link_libraries(gpu_bench PRIVATE thermalcore EGL GLESv3 log)
endif()
