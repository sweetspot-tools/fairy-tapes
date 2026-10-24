# Fairy Tapes · Happy Ending — VST3 mastering chain
# Requires VST3 SDK at ${VST3_SDK_PATH} (defined by the suite root CMakeLists.txt)

cmake_minimum_required(VERSION 3.25)
project(HappyEnding VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

set(VST3_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../vendors/vst3sdk" CACHE PATH "Path to Steinberg VST3 SDK")

if(NOT EXISTS "${VST3_SDK_PATH}/base")
  message(FATAL_ERROR "VST3 SDK not found at ${VST3_SDK_PATH}. Clone it with:\n  git clone https://github.com/steinbergmedia/vst3sdk ${VST3_SDK_PATH}")
endif()

# ---------------------------------------------------------------------------
# Tests (CTest) — header-only dependency on the VST3 SDK (no SDK libs linked).
# One executable per block, named happy_ending_<block>_tests.
# ---------------------------------------------------------------------------
add_executable(happy_ending_params_tests
  tests/test_params.cpp
)
target_include_directories(happy_ending_params_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${VST3_SDK_PATH}
  ${VST3_SDK_PATH}/pluginterfaces/base
  ${VST3_SDK_PATH}/pluginterfaces/vst
)
add_test(NAME happy_ending_params_tests COMMAND happy_ending_params_tests)

add_executable(happy_ending_fine_eq_tests
  tests/test_fine_eq.cpp
  src/dsp/fine_eq.cpp
)
target_include_directories(happy_ending_fine_eq_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${VST3_SDK_PATH}
  ${VST3_SDK_PATH}/pluginterfaces/base
  ${VST3_SDK_PATH}/pluginterfaces/vst
)
add_test(NAME happy_ending_fine_eq_tests COMMAND happy_ending_fine_eq_tests)

add_executable(happy_ending_vibe_eq_tests
  tests/test_vibe_eq.cpp
  src/dsp/vibe_eq.cpp
)
target_include_directories(happy_ending_vibe_eq_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${VST3_SDK_PATH}
  ${VST3_SDK_PATH}/pluginterfaces/base
  ${VST3_SDK_PATH}/pluginterfaces/vst
)
add_test(NAME happy_ending_vibe_eq_tests COMMAND happy_ending_vibe_eq_tests)

add_executable(happy_ending_glue_tests
  tests/test_glue.cpp
  src/dsp/glue.cpp
)
target_include_directories(happy_ending_glue_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${VST3_SDK_PATH}
  ${VST3_SDK_PATH}/pluginterfaces/base
  ${VST3_SDK_PATH}/pluginterfaces/vst
)
add_test(NAME happy_ending_glue_tests COMMAND happy_ending_glue_tests)

add_executable(happy_ending_oversampler_tests
  tests/test_oversampler.cpp
  src/dsp/oversampler.cpp
)
target_include_directories(happy_ending_oversampler_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${VST3_SDK_PATH}
  ${VST3_SDK_PATH}/pluginterfaces/base
  ${VST3_SDK_PATH}/pluginterfaces/vst
)
add_test(NAME happy_ending_oversampler_tests COMMAND happy_ending_oversampler_tests)
