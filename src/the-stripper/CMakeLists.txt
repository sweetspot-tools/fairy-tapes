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

add_library(the_stripper SHARED
  src/plug.cpp
  src/processor.cpp
  src/controller.cpp
  src/dsp/compression_stub.cpp
  src/dsp/fourk_eq.cpp
)

target_include_directories(the_stripper PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp
  ${VST3_SDK_PATH}
  ${VST3_SDK_BASE}/source
  ${VST3_SDK_PLUGINTERFACES}/base
  ${VST3_SDK_PLUGINTERFACES}/vst
  ${VST3_SDK_PUBLIC}/source
  ${VST3_SDK_PUBLIC}/source/vst
)

target_compile_definitions(the_stripper PRIVATE
  UNICODE
  _UNICODE
  NDEBUG
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

# Link platform libs
if(UNIX AND NOT APPLE)
  target_link_libraries(the_stripper PRIVATE pthread)
endif()

# ---------------------------------------------------------------------------
# Tests (run before plugin build; CTest)
# ---------------------------------------------------------------------------
add_executable(the_stripper_tests tests/test_params.cpp)
target_include_directories(the_stripper_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${VST3_SDK_PATH}
  ${VST3_SDK_PLUGINTERFACES}/base
  ${VST3_SDK_PLUGINTERFACES}/vst
)
add_test(NAME the_stripper_tests COMMAND the_stripper_tests)
