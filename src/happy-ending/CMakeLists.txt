# Fairy Tapes · Happy Ending — VST3 mastering chain
# Requires VST3 SDK at ${VST3_SDK_PATH} (defined by the suite root CMakeLists.txt)

cmake_minimum_required(VERSION 3.25)
project(HappyEnding VERSION 1.0.0 LANGUAGES CXX)

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

# The chain: engine and the DSP blocks (shared by the plug-in and the engine / processor tests).
set(HAPPY_ENDING_ENGINE_SOURCES
  src/engine.cpp
  src/dsp/fine_eq.cpp
  src/dsp/vibe_eq.cpp
  src/dsp/glue.cpp
  src/dsp/oversampler.cpp
  src/dsp/limiter.cpp
  src/dsp/metalhead.cpp
  src/dsp/loudness_meter.cpp
)

# Editor (VSTGUI) sources: the plug-in, the editor tests and the render tool. They build with HAPPY_ENDING_WITH_EDITOR
# (Controller::createView gives the editor only then: the controller tests compile controller.cpp without VSTGUI).
set(HAPPY_ENDING_EDITOR_SOURCES
  src/ui/editor.cpp
  src/ui/level_views.cpp
  src/ui/panel_view.cpp
  src/ui/prefs.cpp
)

# The shared assets (panel grain tile), for the tools and tests that run outside the bundle.
set(FAIRY_UI_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/../../shared/ui/assets")

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
# MODULE (not SHARED), as the SDK's smtg_add_vst3plugin does: CMake only honours BUNDLE
# for MODULE libraries on macOS, and on Windows a MODULE .dll is a LIBRARY output.
add_library(happy_ending MODULE
  ${PLUGIN_MAIN_SOURCE}
  src/plug.cpp
  src/processor.cpp
  src/controller.cpp
  ${HAPPY_ENDING_ENGINE_SOURCES}
  ${HAPPY_ENDING_EDITOR_SOURCES}
)

target_include_directories(happy_ending PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/inflator
  ${VST3_SDK_PATH}
  ${VST3_SDK_PATH}/pluginterfaces/base
  ${VST3_SDK_PATH}/pluginterfaces/vst
)

target_compile_definitions(happy_ending PRIVATE UNICODE _UNICODE NDEBUG HAPPY_ENDING_WITH_EDITOR=1)
if(WIN32)
  target_compile_definitions(happy_ending PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Bundle layout mirrors vendors/vst3sdk/cmake/modules/SMTG_AddSMTGLibrary.cmake
# (smtg_target_make_plugin_package), written straight into dist/ (suite root).
# Output directories use a generator expression so multi-config generators
# (Visual Studio, Xcode, Ninja Multi-Config) do not append a /<Config> subfolder.
set(HAPPY_ENDING_BUNDLE "${SUITE_DIST_DIR}/HappyEnding.vst3")
set_target_properties(happy_ending PROPERTIES OUTPUT_NAME "HappyEnding" PREFIX "")
if(APPLE)
  # CMake creates "<OUTPUT_NAME>.<BUNDLE_EXTENSION>" INSIDE LIBRARY_OUTPUT_DIRECTORY:
  # dist/HappyEnding.vst3/Contents/{MacOS/HappyEnding, Info.plist}.
  set_target_properties(happy_ending PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "vst3"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/Info.plist"
    LIBRARY_OUTPUT_DIRECTORY "$<1:${SUITE_DIST_DIR}>"
  )
elseif(WIN32)
  # dist/HappyEnding.vst3/Contents/x86_64-win/HappyEnding.vst3 (the DLL renamed .vst3).
  set_target_properties(happy_ending PROPERTIES
    SUFFIX ".vst3"
    LIBRARY_OUTPUT_DIRECTORY "$<1:${HAPPY_ENDING_BUNDLE}/Contents/x86_64-win>"
    PDB_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/WIN_PDB64"
  )
else()
  # dist/HappyEnding.vst3/Contents/x86_64-linux/HappyEnding.so
  set_target_properties(happy_ending PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "$<1:${HAPPY_ENDING_BUNDLE}/Contents/x86_64-linux>"
  )
endif()

# Bitmap resources go to Contents/Resources of the bundle, where VSTGUI looks for them: the panel grain tile shared by
# the suite. Without it the panel is drawn in plain #161617.
add_custom_command(TARGET happy_ending POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${HAPPY_ENDING_BUNDLE}/Contents/Resources"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FAIRY_UI_ASSETS}/panel_grain.png"
          "${HAPPY_ENDING_BUNDLE}/Contents/Resources/panel_grain.png"
  VERBATIM)

target_link_libraries(happy_ending PRIVATE sdk fairy_ui fairy_ui_widgets)
if(APPLE)
  target_link_libraries(happy_ending PRIVATE "-framework CoreFoundation")
  target_link_options(happy_ending PRIVATE
    "-exported_symbols_list" "${VST3_SDK_PATH}/public.sdk/source/main/macexport.exp")
endif()
if(UNIX AND NOT APPLE)
  target_link_libraries(happy_ending PRIVATE pthread)
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

add_executable(happy_ending_clip_tests
  tests/test_clip.cpp
)
target_include_directories(happy_ending_clip_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
add_test(NAME happy_ending_clip_tests COMMAND happy_ending_clip_tests)

add_executable(happy_ending_limiter_tests
  tests/test_limiter.cpp
  src/dsp/limiter.cpp
)
target_include_directories(happy_ending_limiter_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
add_test(NAME happy_ending_limiter_tests COMMAND happy_ending_limiter_tests)

add_executable(happy_ending_final_tests
  tests/test_final.cpp
  src/dsp/metalhead.cpp
  src/dsp/limiter.cpp
)
target_include_directories(happy_ending_final_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/inflator
)
add_test(NAME happy_ending_final_tests COMMAND happy_ending_final_tests)

add_executable(happy_ending_loudness_tests
  tests/test_loudness.cpp
  src/dsp/loudness_meter.cpp
)
target_include_directories(happy_ending_loudness_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
add_test(NAME happy_ending_loudness_tests COMMAND happy_ending_loudness_tests)

add_executable(happy_ending_loudness_macro_tests
  tests/test_loudness_macro.cpp
)
target_include_directories(happy_ending_loudness_macro_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
add_test(NAME happy_ending_loudness_macro_tests COMMAND happy_ending_loudness_macro_tests)

add_executable(happy_ending_engine_tests
  tests/test_engine.cpp
  src/engine.cpp
  src/dsp/fine_eq.cpp
  src/dsp/vibe_eq.cpp
  src/dsp/glue.cpp
  src/dsp/oversampler.cpp
  src/dsp/limiter.cpp
  src/dsp/metalhead.cpp
  src/dsp/loudness_meter.cpp
)
target_include_directories(happy_ending_engine_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/inflator
  ${VST3_SDK_PATH}
  ${VST3_SDK_PATH}/pluginterfaces/base
  ${VST3_SDK_PATH}/pluginterfaces/vst
)
add_test(NAME happy_ending_engine_tests COMMAND happy_ending_engine_tests)

# Versioned state (pure, header-only dependency on the VST3 type headers).
add_executable(happy_ending_state_tests
  tests/test_state.cpp
)
target_include_directories(happy_ending_state_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${VST3_SDK_PATH}
  ${VST3_SDK_PATH}/pluginterfaces/base
  ${VST3_SDK_PATH}/pluginterfaces/vst
)
target_link_libraries(happy_ending_state_tests PRIVATE fairy_ui)  # header-only (fairy_ui/tape_label.h)
add_test(NAME happy_ending_state_tests COMMAND happy_ending_state_tests)

# Display parameters: normalization, hold between writes, pacing (pure; engine.h only for the meter types).
add_executable(happy_ending_display_tests
  tests/test_display.cpp
)
target_include_directories(happy_ending_display_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/inflator
  ${VST3_SDK_PATH}
  ${VST3_SDK_PATH}/pluginterfaces/base
  ${VST3_SDK_PATH}/pluginterfaces/vst
)
add_test(NAME happy_ending_display_tests COMMAND happy_ending_display_tests)

# Edit controller compiled in (no module needed): strings, MATCH, session parameters, the OVERSAMPLING restart.
add_executable(happy_ending_controller_tests
  tests/test_controller.cpp
  src/controller.cpp
)
target_include_directories(happy_ending_controller_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/inflator
  ${VST3_SDK_PATH}
  ${VST3_SDK_PATH}/pluginterfaces/base
  ${VST3_SDK_PATH}/pluginterfaces/vst
)
target_link_libraries(happy_ending_controller_tests PRIVATE sdk fairy_ui)
add_test(NAME happy_ending_controller_tests COMMAND happy_ending_controller_tests)

# Pure editor model (ui/layout.h, ui/editor_size.h, ui/screen_model.h, ui/prefs.h, ui/decor.h, ui/screw_angles.h, ui/tapes.h): no VSTGUI, only SDK type headers through params.h.
# The screen model computes the EQ curves on its own FineEq / VibeEq and uses mapParams, so the UI tests compile
# the chain's sources (never the processor or a view).
add_executable(happy_ending_ui_tests
  tests/test_ui.cpp
  src/ui/screen_model.cpp
  src/ui/prefs.cpp
  src/ui/decor.cpp
  ${HAPPY_ENDING_ENGINE_SOURCES}
)
target_include_directories(happy_ending_ui_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/inflator
  ${VST3_SDK_PATH}
  ${VST3_SDK_PATH}/pluginterfaces/base
  ${VST3_SDK_PATH}/pluginterfaces/vst
)
target_link_libraries(happy_ending_ui_tests PRIVATE fairy_ui)  # header-only (fairy_ui/silkscreen.h, screws.h)
add_test(NAME happy_ending_ui_tests COMMAND happy_ending_ui_tests)

# Linux only: the offscreen drawing and the grain lookup rely on the Linux VSTGUI factory (resource path); the SDK
# factory stub and moduleHandle are only checked there.
if(UNIX AND NOT APPLE)
  # Editor behaviour without a window (tests/test_editor.cpp): a real Controller as host, the views built in a container by
  # the EditorTest friend, offscreen drawing. Built like the render tool (SDK module init and factory, no plugin entry).
  add_executable(happy_ending_editor_tests
    tests/test_editor.cpp
    ${VST3_SDK_PATH}/public.sdk/source/main/moduleinit.cpp
    ${VST3_SDK_PATH}/public.sdk/source/main/pluginfactory.cpp
    src/controller.cpp
    ${HAPPY_ENDING_EDITOR_SOURCES}
  )
  target_include_directories(happy_ending_editor_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/inflator ${VST3_SDK_PATH})
  target_compile_definitions(happy_ending_editor_tests PRIVATE HAPPY_ENDING_WITH_EDITOR=1
    HAPPY_ENDING_ASSETS="${FAIRY_UI_ASSETS}/")
  target_link_libraries(happy_ending_editor_tests PRIVATE sdk pluginterfaces fairy_ui fairy_ui_widgets)
  add_test(NAME happy_ending_editor_tests COMMAND happy_ending_editor_tests)
endif()

# Developer tool: draws the editor offscreen into PNG files (no host needed). Not built by default:
#   cmake --build <build> --target happy_ending_render -j2
add_executable(happy_ending_render EXCLUDE_FROM_ALL
  tools/render_editor.cpp
  ${VST3_SDK_PATH}/public.sdk/source/main/moduleinit.cpp
  ${VST3_SDK_PATH}/public.sdk/source/main/pluginfactory.cpp
  src/controller.cpp
  ${HAPPY_ENDING_EDITOR_SOURCES}
)
target_include_directories(happy_ending_render PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/inflator ${VST3_SDK_PATH})
target_compile_definitions(happy_ending_render PRIVATE HAPPY_ENDING_WITH_EDITOR=1
  HAPPY_ENDING_ASSETS="${FAIRY_UI_ASSETS}/")
target_link_libraries(happy_ending_render PRIVATE sdk pluginterfaces fairy_ui fairy_ui_widgets)

# The render tool writes front.png (PNG signature, 1280 x 890); skipped while the tool is not built.
add_test(NAME happy_ending_render_smoke
  COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:happy_ending_render> -DOUT=${CMAKE_CURRENT_BINARY_DIR}/render_smoke
          -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/render_smoke.cmake)
set_tests_properties(happy_ending_render_smoke PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:")

# Module tests: dlopen the built bundle from dist/ (exit 77 = skipped when the plug-in has not been built).
if(NOT WIN32)
  if(APPLE)
    set(HAPPY_ENDING_MODULE_PATH "${SUITE_DIST_DIR}/HappyEnding.vst3/Contents/MacOS/HappyEnding")
  else()
    set(HAPPY_ENDING_MODULE_PATH "${SUITE_DIST_DIR}/HappyEnding.vst3/Contents/x86_64-linux/HappyEnding.so")
  endif()

  # Factory lifecycle, registered classes, parameter table, accepted bus layouts, the editor view (createView, SIZE).
  add_executable(happy_ending_factory_test tests/test_factory.cpp
    ${VST3_SDK_PATH}/public.sdk/source/vst/vstinitiids.cpp)
  target_include_directories(happy_ending_factory_test PRIVATE ${VST3_SDK_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_compile_definitions(happy_ending_factory_test PRIVATE
    HAPPY_ENDING_MODULE="${HAPPY_ENDING_MODULE_PATH}")
  target_link_libraries(happy_ending_factory_test PRIVATE pluginterfaces ${CMAKE_DL_LIBS})
  add_dependencies(happy_ending_factory_test happy_ending)
  add_test(NAME happy_ending_factory_test COMMAND happy_ending_factory_test)
  set_tests_properties(happy_ending_factory_test PROPERTIES SKIP_RETURN_CODE 77)

  # The processor on the real module (mock host queues): display parameters, BYPASS, latency, state, no allocation.
  # The executable exports its symbols: its operator new counts the module's allocations too.
  add_executable(happy_ending_processor_level_test tests/test_processor_levels.cpp
    ${HAPPY_ENDING_ENGINE_SOURCES}
    ${VST3_SDK_PATH}/public.sdk/source/vst/vstinitiids.cpp)
  target_include_directories(happy_ending_processor_level_test PRIVATE
    ${VST3_SDK_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/inflator)
  target_compile_definitions(happy_ending_processor_level_test PRIVATE
    HAPPY_ENDING_MODULE="${HAPPY_ENDING_MODULE_PATH}")
  target_link_libraries(happy_ending_processor_level_test PRIVATE pluginterfaces fairy_ui ${CMAKE_DL_LIBS})
  set_target_properties(happy_ending_processor_level_test PROPERTIES ENABLE_EXPORTS ON)
  add_test(NAME happy_ending_processor_level_test COMMAND happy_ending_processor_level_test)
  set_tests_properties(happy_ending_processor_level_test PROPERTIES SKIP_RETURN_CODE 77)

  # State v2 (seed, screw angles, tape texts) and the Decor messages on the real module, with the SDK's minimal host
  # classes (IHostApplication, IMessage); skipped when the module is absent. Depends on the plug-in target so that
  # the bundle in dist/ is built (and current) before the test runs.
  add_executable(happy_ending_state_module_test tests/test_state_module.cpp
    ${VST3_SDK_PATH}/public.sdk/source/vst/vstinitiids.cpp
    ${VST3_SDK_PATH}/public.sdk/source/vst/hosting/hostclasses.cpp
    ${VST3_SDK_PATH}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
    ${VST3_SDK_PATH}/public.sdk/source/vst/utility/stringconvert.cpp
    ${VST3_SDK_PATH}/public.sdk/source/common/commonstringconvert.cpp)
  target_include_directories(happy_ending_state_module_test PRIVATE ${VST3_SDK_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_compile_definitions(happy_ending_state_module_test PRIVATE
    HAPPY_ENDING_MODULE="${HAPPY_ENDING_MODULE_PATH}")
  target_link_libraries(happy_ending_state_module_test PRIVATE pluginterfaces fairy_ui ${CMAKE_DL_LIBS})
  add_dependencies(happy_ending_state_module_test happy_ending)
  add_test(NAME happy_ending_state_module_test COMMAND happy_ending_state_module_test)
  set_tests_properties(happy_ending_state_module_test PROPERTIES SKIP_RETURN_CODE 77)
endif()
