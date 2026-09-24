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
