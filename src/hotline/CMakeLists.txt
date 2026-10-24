# Fairy Tapes · Hotline — VST3 telephone / lo-fi device emulation
# Requires VST3 SDK at ${VST3_SDK_PATH} (defined by the suite root CMakeLists.txt)

cmake_minimum_required(VERSION 3.25)
project(Hotline VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

set(VST3_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../vendors/vst3sdk" CACHE PATH "Path to Steinberg VST3 SDK")

if(NOT EXISTS "${VST3_SDK_PATH}/base")
  message(FATAL_ERROR "VST3 SDK not found at ${VST3_SDK_PATH}. Clone it with:\n  git clone https://github.com/steinbergmedia/vst3sdk ${VST3_SDK_PATH}")
endif()

# The `hotline` MODULE target (bundle Hotline.vst3) is added in Task 6, once the processor/controller sources exist.

# ---------------------------------------------------------------------------
# Tests (CTest) — header-only dependency on the VST3 SDK (no SDK libs linked)
# ---------------------------------------------------------------------------
add_executable(hotline_tests
  tests/test_hotline.cpp
  src/devices.cpp
  src/engine.cpp
)
target_include_directories(hotline_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${VST3_SDK_PATH}
  ${VST3_SDK_PATH}/pluginterfaces/base
  ${VST3_SDK_PATH}/pluginterfaces/vst
)
target_link_libraries(hotline_tests PRIVATE fairy_ui)
add_test(NAME hotline_tests COMMAND hotline_tests)
