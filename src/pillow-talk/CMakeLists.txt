# Fairy Tapes · Pillow Talk — VST3 tape echo with ducking
# Requires VST3 SDK at ${VST3_SDK_PATH} (defined by the suite root CMakeLists.txt)

cmake_minimum_required(VERSION 3.25)
project(PillowTalk VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

set(VST3_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../vendors/vst3sdk" CACHE PATH "Path to Steinberg VST3 SDK")

if(NOT EXISTS "${VST3_SDK_PATH}/base")
  message(FATAL_ERROR "VST3 SDK not found at ${VST3_SDK_PATH}. Clone it with:\n  git clone https://github.com/steinbergmedia/vst3sdk ${VST3_SDK_PATH}")
endif()

# Distribution folder: built .vst3 bundles go here (gitignored). Suite root = CMAKE_SOURCE_DIR.
set(SUITE_DIST_DIR "${CMAKE_SOURCE_DIR}/dist")

# Platform module entry point from the VST3 SDK (ModuleEntry / bundleEntry / InitDll).
if(WIN32)
  set(PLUGIN_MAIN_SOURCE ${VST3_SDK_PATH}/public.sdk/source/main/dllmain.cpp)
elseif(APPLE)
  set(PLUGIN_MAIN_SOURCE ${VST3_SDK_PATH}/public.sdk/source/main/macmain.cpp)
else()
  set(PLUGIN_MAIN_SOURCE ${VST3_SDK_PATH}/public.sdk/source/main/linuxmain.cpp)
endif()

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
add_library(pillow_talk SHARED
  ${PLUGIN_MAIN_SOURCE}
  src/plug.cpp
  src/processor.cpp
  src/controller.cpp
  src/engine.cpp
  src/dsp/ducker.cpp
  src/dsp/tape_echo.cpp
)

target_include_directories(pillow_talk PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${VST3_SDK_PATH}
  ${VST3_SDK_PATH}/pluginterfaces/base
  ${VST3_SDK_PATH}/pluginterfaces/vst
)

target_compile_definitions(pillow_talk PRIVATE UNICODE _UNICODE NDEBUG)
if(WIN32)
  target_compile_definitions(pillow_talk PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Output as VST3 bundle directly into dist/ (suite root).
if(APPLE)
  set_target_properties(pillow_talk PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "vst3"
    OUTPUT_NAME "Pillow Talk"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/Info.plist"
    LIBRARY_OUTPUT_DIRECTORY "${SUITE_DIST_DIR}/Pillow Talk.vst3/Contents/MacOS"
  )
elseif(WIN32)
  set_target_properties(pillow_talk PROPERTIES
    PREFIX ""
    OUTPUT_NAME "Pillow Talk"
    RUNTIME_OUTPUT_DIRECTORY "${SUITE_DIST_DIR}/Pillow Talk.vst3/Contents/x86_64-win"
  )
else()
  set_target_properties(pillow_talk PROPERTIES
    PREFIX ""
    OUTPUT_NAME "Pillow Talk"
    LIBRARY_OUTPUT_DIRECTORY "${SUITE_DIST_DIR}/Pillow Talk.vst3/Contents/x86_64-linux"
  )
endif()

target_link_libraries(pillow_talk PRIVATE sdk)
if(APPLE)
  target_link_libraries(pillow_talk PRIVATE "-framework CoreFoundation")
  target_link_options(pillow_talk PRIVATE
    "-exported_symbols_list" "${VST3_SDK_PATH}/public.sdk/source/main/macexport.exp")
endif()
if(UNIX AND NOT APPLE)
  target_link_libraries(pillow_talk PRIVATE pthread)
endif()

# ---------------------------------------------------------------------------
# Tests (CTest) — header-only dependency on the VST3 SDK (no SDK libs linked)
# ---------------------------------------------------------------------------
add_executable(pillow_talk_tests
  tests/test_pillow_talk.cpp
  src/engine.cpp
  src/dsp/ducker.cpp
  src/dsp/tape_echo.cpp
)
target_include_directories(pillow_talk_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${VST3_SDK_PATH}
  ${VST3_SDK_PATH}/pluginterfaces/base
  ${VST3_SDK_PATH}/pluginterfaces/vst
)
add_test(NAME pillow_talk_tests COMMAND pillow_talk_tests)
