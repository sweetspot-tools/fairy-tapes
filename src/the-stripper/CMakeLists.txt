# Fairy Tapes · The Stripper — VST3 Channel Strip
# Requires VST3 SDK at ${VST3_SDK_PATH} (default: ../../../vendors/vst3sdk from repo root)

cmake_minimum_required(VERSION 3.16)
project(TheStripper VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

# VST3 SDK path (clone https://github.com/steinbergmedia/vst3sdk into vendors/vst3sdk at repo root)
set(VST3_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../vendors/vst3sdk" CACHE PATH "Path to Steinberg VST3 SDK")
# Distribution folder: built .vst3 bundles go here (gitignored). Suite root = CMAKE_SOURCE_DIR.
set(SUITE_DIST_DIR "${CMAKE_SOURCE_DIR}/dist")

if(NOT EXISTS "${VST3_SDK_PATH}/base")
  message(FATAL_ERROR "VST3 SDK not found at ${VST3_SDK_PATH}. Clone it with:\n  git clone https://github.com/steinbergmedia/vst3sdk ${VST3_SDK_PATH}")
endif()

# SDK include and source (minimal set for a plugin)
set(VST3_SDK_BASE "${VST3_SDK_PATH}/base")
set(VST3_SDK_PLUGINTERFACES "${VST3_SDK_PATH}/pluginterfaces")
set(VST3_SDK_PUBLIC "${VST3_SDK_PATH}/public.sdk")

# ---------------------------------------------------------------------------
# NeuralAmpModelerCore (CPU inference for Drive stage)
# ---------------------------------------------------------------------------
set(NAM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../vendors/NeuralAmpModelerCore")
set(NAM_SOURCES
  ${NAM_ROOT}/NAM/activations.cpp
  ${NAM_ROOT}/NAM/container.cpp
  ${NAM_ROOT}/NAM/conv1d.cpp
  ${NAM_ROOT}/NAM/convnet.cpp
  ${NAM_ROOT}/NAM/dsp.cpp
  ${NAM_ROOT}/NAM/get_dsp.cpp
  ${NAM_ROOT}/NAM/lstm.cpp
  ${NAM_ROOT}/NAM/ring_buffer.cpp
  ${NAM_ROOT}/NAM/slimmable_wavenet.cpp
  ${NAM_ROOT}/NAM/util.cpp
  ${NAM_ROOT}/NAM/wavenet.cpp
)
set(NAM_INCLUDE_DIRS
  ${NAM_ROOT}
  ${NAM_ROOT}/Dependencies/eigen
  ${NAM_ROOT}/Dependencies/nlohmann
)

# ---------------------------------------------------------------------------
# gpu_nam (GPU-accelerated NAM inference via CUDA, with CPU-stub fallback)
# ---------------------------------------------------------------------------
set(GPU_NAM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../shared/gpu_nam")
add_subdirectory(${GPU_NAM_DIR} ${CMAKE_CURRENT_BINARY_DIR}/gpu_nam)


add_library(the_stripper SHARED
  src/plug.cpp
  src/processor.cpp
  src/controller.cpp
  src/dsp/compression_stub.cpp
  src/dsp/multicomp_engines.cpp
  src/dsp/fourk_eq.cpp
  src/dsp/blockparty.cpp
  src/dsp/denoise.cpp
  src/dsp/nam_drive.cpp
  ${NAM_SOURCES}
)

target_include_directories(the_stripper PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/hardware_emulation
  ${VST3_SDK_PATH}
  ${VST3_SDK_BASE}/source
  ${VST3_SDK_PLUGINTERFACES}/base
  ${VST3_SDK_PLUGINTERFACES}/vst
  ${VST3_SDK_PUBLIC}/source
  ${VST3_SDK_PUBLIC}/source/vst
  ${NAM_INCLUDE_DIRS}
)

target_compile_definitions(the_stripper PRIVATE
  UNICODE
  _UNICODE
  NDEBUG
  NAM_SAMPLE_FLOAT
)

if(WIN32)
  target_compile_definitions(the_stripper PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Output as VST3 bundle directly into dist/ (suite root). No pack step; each build writes the final bundle there.
if(APPLE)
  set_target_properties(the_stripper PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "vst3"
    OUTPUT_NAME "The Stripper"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/Info.plist"
  )
  set_target_properties(the_stripper PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${SUITE_DIST_DIR}/The Stripper.vst3/Contents/MacOS"
  )
elseif(WIN32)
  set_target_properties(the_stripper PROPERTIES
    PREFIX ""
    OUTPUT_NAME "The Stripper"
    RUNTIME_OUTPUT_DIRECTORY "${SUITE_DIST_DIR}/The Stripper.vst3/Contents/x86_64-win"
  )
else()
  # Linux
  set_target_properties(the_stripper PROPERTIES
    PREFIX ""
    OUTPUT_NAME "The Stripper"
    LIBRARY_OUTPUT_DIRECTORY "${SUITE_DIST_DIR}/The Stripper.vst3/Contents/x86_64-linux"
  )
endif()

# Link gpu_nam (GPU NAM inference with automatic CPU-stub fallback).
# cudart is propagated as a PUBLIC dependency of gpu_nam.
target_link_libraries(the_stripper PRIVATE gpu_nam)

# Link platform libs
if(UNIX AND NOT APPLE)
  target_link_libraries(the_stripper PRIVATE pthread stdc++fs)
endif()

# ---------------------------------------------------------------------------
# Tests (CTest)
# ---------------------------------------------------------------------------
add_executable(the_stripper_tests
  tests/test_params.cpp
  src/dsp/fourk_eq.cpp
  src/dsp/compression_stub.cpp
  src/dsp/multicomp_engines.cpp
  src/dsp/blockparty.cpp
  src/dsp/denoise.cpp
  src/dsp/nam_drive.cpp
  ${NAM_SOURCES}
)
target_include_directories(the_stripper_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/hardware_emulation
  ${VST3_SDK_PATH}
  ${VST3_SDK_PLUGINTERFACES}/base
  ${VST3_SDK_PLUGINTERFACES}/vst
  ${NAM_INCLUDE_DIRS}
)
target_compile_definitions(the_stripper_tests PRIVATE NAM_SAMPLE_FLOAT)
target_link_libraries(the_stripper_tests PRIVATE gpu_nam)
add_test(NAME the_stripper_tests COMMAND the_stripper_tests)

# ---------------------------------------------------------------------------
# Coverage (gcov/lcov) — enable with: cmake -DCOVERAGE=ON ..
# ---------------------------------------------------------------------------
option(COVERAGE "Build with code coverage instrumentation" OFF)
if(COVERAGE)
  target_compile_options(the_stripper_tests PRIVATE --coverage -fprofile-arcs -ftest-coverage -O0 -g)
  target_link_options(the_stripper_tests PRIVATE --coverage)
  target_compile_options(the_stripper PRIVATE --coverage -fprofile-arcs -ftest-coverage -O0 -g)
  target_link_options(the_stripper PRIVATE --coverage)
endif()
