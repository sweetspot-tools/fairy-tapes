# Fairy Tapes · Pillow Talk — VST3 tape echo with ducking
# Requires VST3 SDK at ${VST3_SDK_PATH} (defined by the suite root CMakeLists.txt)

cmake_minimum_required(VERSION 3.25)
project(PillowTalk VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

set(VST3_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../vendors/vst3sdk" CACHE PATH "Path to Steinberg VST3 SDK")

if(NOT EXISTS "${VST3_SDK_PATH}/base")
  message(FATAL_ERROR "VST3 SDK not found at ${VST3_SDK_PATH}. Clone it with:\n  git clone https://github.com/steinbergmedia/vst3sdk ${VST3_SDK_PATH}")
endif()

# ---------------------------------------------------------------------------
# Tests (CTest) — header-only dependency on the VST3 SDK (no SDK libs linked)
# ---------------------------------------------------------------------------
add_executable(pillow_talk_tests
  tests/test_pillow_talk.cpp
)
target_include_directories(pillow_talk_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${VST3_SDK_PATH}
  ${VST3_SDK_PATH}/pluginterfaces/base
  ${VST3_SDK_PATH}/pluginterfaces/vst
)
add_test(NAME pillow_talk_tests COMMAND pillow_talk_tests)
