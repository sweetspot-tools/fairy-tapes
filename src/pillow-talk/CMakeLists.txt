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
# MODULE (not SHARED), as the SDK's smtg_add_vst3plugin does: CMake only honours BUNDLE
# for MODULE libraries on macOS, and on Windows a MODULE .dll is a LIBRARY output.
add_library(pillow_talk MODULE
  ${PLUGIN_MAIN_SOURCE}
  src/plug.cpp
  src/processor.cpp
  src/controller.cpp
  src/engine.cpp
  src/dsp/ducker.cpp
  src/dsp/tape_echo.cpp
  src/ui/duck_scope.cpp
  src/ui/editor.cpp
  src/ui/panel_view.cpp
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

# Bundle layout mirrors vendors/vst3sdk/cmake/modules/SMTG_AddSMTGLibrary.cmake
# (smtg_target_make_plugin_package), written straight into dist/ (suite root).
# Output directories use a generator expression so multi-config generators
# (Visual Studio, Xcode, Ninja Multi-Config) do not append a /<Config> subfolder.
set(PILLOW_TALK_BUNDLE "${SUITE_DIST_DIR}/Pillow Talk.vst3")
set_target_properties(pillow_talk PROPERTIES OUTPUT_NAME "Pillow Talk" PREFIX "")
if(APPLE)
  # CMake creates "<OUTPUT_NAME>.<BUNDLE_EXTENSION>" INSIDE LIBRARY_OUTPUT_DIRECTORY:
  # dist/Pillow Talk.vst3/Contents/{MacOS/Pillow Talk, Info.plist}.
  set_target_properties(pillow_talk PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "vst3"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/Info.plist"
    LIBRARY_OUTPUT_DIRECTORY "$<1:${SUITE_DIST_DIR}>"
  )
elseif(WIN32)
  # dist/Pillow Talk.vst3/Contents/x86_64-win/Pillow Talk.vst3 (the DLL renamed .vst3).
  set_target_properties(pillow_talk PROPERTIES
    SUFFIX ".vst3"
    LIBRARY_OUTPUT_DIRECTORY "$<1:${PILLOW_TALK_BUNDLE}/Contents/x86_64-win>"
    PDB_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/WIN_PDB64"
  )
else()
  # dist/Pillow Talk.vst3/Contents/x86_64-linux/Pillow Talk.so
  set_target_properties(pillow_talk PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "$<1:${PILLOW_TALK_BUNDLE}/Contents/x86_64-linux>"
  )
endif()

target_link_libraries(pillow_talk PRIVATE sdk fairy_ui fairy_ui_widgets)
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
target_link_libraries(pillow_talk_tests PRIVATE fairy_ui)
add_test(NAME pillow_talk_tests COMMAND pillow_talk_tests)

# Module/factory lifecycle smoke test: dlopens the built bundle from dist/ (exit 77 = skipped
# when the plugin has not been built). Links only the pluginterfaces IIDs, not the plugin.
if(NOT WIN32)
  if(APPLE)
    set(PILLOW_TALK_MODULE_PATH "${SUITE_DIST_DIR}/Pillow Talk.vst3/Contents/MacOS/Pillow Talk")
  else()
    set(PILLOW_TALK_MODULE_PATH "${SUITE_DIST_DIR}/Pillow Talk.vst3/Contents/x86_64-linux/Pillow Talk.so")
  endif()
  add_executable(pillow_talk_factory_test tests/test_factory.cpp)
  target_include_directories(pillow_talk_factory_test PRIVATE ${VST3_SDK_PATH})
  target_compile_definitions(pillow_talk_factory_test PRIVATE
    PILLOW_TALK_MODULE="${PILLOW_TALK_MODULE_PATH}")
  target_link_libraries(pillow_talk_factory_test PRIVATE pluginterfaces ${CMAKE_DL_LIBS})
  add_test(NAME pillow_talk_factory_test COMMAND pillow_talk_factory_test)
  set_tests_properties(pillow_talk_factory_test PROPERTIES SKIP_RETURN_CODE 77)
endif()
