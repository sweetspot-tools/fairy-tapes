# Fairy Tapes · The Stripper — VST3 Channel Strip
# Requires VST3 SDK at ${VST3_SDK_PATH} (default: ../../../vendors/vst3sdk from repo root)

cmake_minimum_required(VERSION 3.25)
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
# gpu_nam (GPU-accelerated NAM inference via CUDA, with CPU-stub fallback)
# ---------------------------------------------------------------------------
set(GPU_NAM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../shared/gpu_nam")
add_subdirectory(${GPU_NAM_DIR} ${CMAKE_CURRENT_BINARY_DIR}/gpu_nam)

# NamDrive + NeuralAmpModelerCore (shared with other plugins; needs gpu_nam above)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../shared/nam_drive ${CMAKE_CURRENT_BINARY_DIR}/nam_drive)


# Platform module entry point from the VST3 SDK (ModuleEntry / bundleEntry / InitDll).
if(WIN32)
  set(PLUGIN_MAIN_SOURCE ${VST3_SDK_PATH}/public.sdk/source/main/dllmain.cpp)
elseif(APPLE)
  set(PLUGIN_MAIN_SOURCE ${VST3_SDK_PATH}/public.sdk/source/main/macmain.cpp)
else()
  set(PLUGIN_MAIN_SOURCE ${VST3_SDK_PATH}/public.sdk/source/main/linuxmain.cpp)
endif()

# MODULE (not SHARED), as the SDK's smtg_add_vst3plugin does: CMake only honours BUNDLE for MODULE libraries on macOS.
add_library(the_stripper MODULE
  ${PLUGIN_MAIN_SOURCE}
  src/plug.cpp
  src/processor.cpp
  src/controller.cpp
  src/ui/editor.cpp
  src/ui/panel_view.cpp
  src/ui/browser_widgets.cpp
  src/ui/model_browser.cpp
  src/ui/model_strip.cpp
  src/ui/tape_text.cpp
  src/ui/tape_piece_view.cpp
  src/ui/back_widgets.cpp
  src/ui/back_view.cpp
  src/ui/flip_view.cpp
  src/dsp/compression_stub.cpp
  src/dsp/multicomp_engines.cpp
  src/dsp/fourk_eq.cpp
  src/dsp/blockparty.cpp
  src/dsp/denoise.cpp
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
# Generator expressions ("$<1:...>") stop multi-config generators (Visual Studio, Xcode) from appending /<CONFIG>.
set(THE_STRIPPER_BUNDLE "${SUITE_DIST_DIR}/TheStripper.vst3")
set_target_properties(the_stripper PROPERTIES OUTPUT_NAME "TheStripper" PREFIX "")
if(APPLE)
  # CMake creates "<OUTPUT_NAME>.<BUNDLE_EXTENSION>" INSIDE LIBRARY_OUTPUT_DIRECTORY:
  # dist/TheStripper.vst3/Contents/{MacOS/TheStripper, Info.plist}.
  set_target_properties(the_stripper PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "vst3"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/Info.plist"
    LIBRARY_OUTPUT_DIRECTORY "$<1:${SUITE_DIST_DIR}>"
  )
elseif(WIN32)
  # dist/TheStripper.vst3/Contents/x86_64-win/TheStripper.vst3 (the DLL renamed .vst3).
  set_target_properties(the_stripper PROPERTIES
    SUFFIX ".vst3"
    LIBRARY_OUTPUT_DIRECTORY "$<1:${THE_STRIPPER_BUNDLE}/Contents/x86_64-win>"
    RUNTIME_OUTPUT_DIRECTORY "$<1:${THE_STRIPPER_BUNDLE}/Contents/x86_64-win>"
    PDB_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/WIN_PDB64"
  )
else()
  # dist/TheStripper.vst3/Contents/x86_64-linux/TheStripper.so
  set_target_properties(the_stripper PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "$<1:${THE_STRIPPER_BUNDLE}/Contents/x86_64-linux>"
  )
endif()

# Bitmap resources (the panel grain tile) go to Contents/Resources of the bundle, where VSTGUI looks for them.
add_custom_command(TARGET the_stripper POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${THE_STRIPPER_BUNDLE}/Contents/Resources"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/assets/panel_grain.png"
          "${THE_STRIPPER_BUNDLE}/Contents/Resources/panel_grain.png"
  VERBATIM)

# Link gpu_nam (GPU NAM inference with automatic CPU-stub fallback).
# cudart is propagated as a PUBLIC dependency of gpu_nam.
nam_drive_link(the_stripper)
target_link_libraries(the_stripper PRIVATE gpu_nam sdk fairy_ui fairy_ui_widgets)   # shared code-drawn VSTGUI controls (shared/ui)
if(APPLE)
  target_link_libraries(the_stripper PRIVATE "-framework CoreFoundation")
  target_link_options(the_stripper PRIVATE
    "-exported_symbols_list" "${VST3_SDK_PATH}/public.sdk/source/main/macexport.exp")
endif()

# Link platform libs
if(UNIX AND NOT APPLE)
  target_link_libraries(the_stripper PRIVATE pthread stdc++fs)
endif()

# ---------------------------------------------------------------------------
# Tests (CTest)
# ---------------------------------------------------------------------------
add_executable(the_stripper_tests
  tests/test_params.cpp
  tests/test_meter.cpp
  src/dsp/fourk_eq.cpp
  src/dsp/compression_stub.cpp
  src/dsp/multicomp_engines.cpp
  src/dsp/blockparty.cpp
  src/dsp/denoise.cpp
)
target_include_directories(the_stripper_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/hardware_emulation
  ${VST3_SDK_PATH}
  ${VST3_SDK_PLUGINTERFACES}/base
  ${VST3_SDK_PLUGINTERFACES}/vst
)
nam_drive_link(the_stripper_tests)
target_link_libraries(the_stripper_tests PRIVATE gpu_nam)
add_test(NAME the_stripper_tests COMMAND the_stripper_tests)

# State and frozen-parameter-table tests: header-only, no SDK libraries needed.
add_executable(the_stripper_state_tests tests/test_state.cpp)
target_include_directories(the_stripper_state_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${VST3_SDK_PATH}
  ${VST3_SDK_PLUGINTERFACES}/base
)
add_test(NAME the_stripper_state_tests COMMAND the_stripper_state_tests)

# Editor layout table and dimming rules (header-only, no VSTGUI).
add_executable(the_stripper_layout_tests tests/test_layout.cpp)
target_include_directories(the_stripper_layout_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${VST3_SDK_PATH} ${VST3_SDK_PLUGINTERFACES}/base)
add_test(NAME the_stripper_layout_tests COMMAND the_stripper_layout_tests)

# Local model source (folder scan, filter, name helpers): header-only, no VSTGUI.
add_executable(the_stripper_model_source_tests tests/test_model_source.cpp)
target_include_directories(the_stripper_model_source_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME the_stripper_model_source_tests COMMAND the_stripper_model_source_tests)

# User preferences file (header-only, no SDK).
add_executable(the_stripper_preferences_tests tests/test_preferences.cpp)
target_include_directories(the_stripper_preferences_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(the_stripper_preferences_tests PRIVATE fairy_ui)
add_test(NAME the_stripper_preferences_tests COMMAND the_stripper_preferences_tests)

# What the shared silkscreen layer prints for this plug-in (header-only, no VSTGUI).
add_executable(the_stripper_silkscreen_items_tests tests/test_silkscreen_items.cpp)
target_include_directories(the_stripper_silkscreen_items_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${VST3_SDK_PATH} ${VST3_SDK_PLUGINTERFACES}/base)
target_link_libraries(the_stripper_silkscreen_items_tests PRIVATE fairy_ui)
add_test(NAME the_stripper_silkscreen_items_tests COMMAND the_stripper_silkscreen_items_tests)

# Tape fonts as vector paths (header-only, no VSTGUI).
add_executable(the_stripper_tape_font_tests tests/test_tape_font.cpp)
target_include_directories(the_stripper_tape_font_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(the_stripper_tape_font_tests PRIVATE fairy_ui)
add_test(NAME the_stripper_tape_font_tests COMMAND the_stripper_tape_font_tests)

# Writable tape pieces (header-only, no VSTGUI).
add_executable(the_stripper_tape_pieces_tests tests/test_tape_pieces.cpp)
target_include_directories(the_stripper_tape_pieces_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${VST3_SDK_PATH} ${VST3_SDK_PLUGINTERFACES}/base)
target_link_libraries(the_stripper_tape_pieces_tests PRIVATE fairy_ui)
add_test(NAME the_stripper_tape_pieces_tests COMMAND the_stripper_tape_pieces_tests)

# Back of the panel: credits, button help, settings options, word wrap (header-only, no VSTGUI).
add_executable(the_stripper_back_panel_tests tests/test_back_panel.cpp)
target_include_directories(the_stripper_back_panel_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${VST3_SDK_PATH} ${VST3_SDK_PLUGINTERFACES}/base)
target_link_libraries(the_stripper_back_panel_tests PRIVATE fairy_ui)
add_test(NAME the_stripper_back_panel_tests COMMAND the_stripper_back_panel_tests)

# The pose of the turning panel (header-only, no VSTGUI).
add_executable(the_stripper_flip_tests tests/test_flip.cpp)
target_include_directories(the_stripper_flip_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME the_stripper_flip_tests COMMAND the_stripper_flip_tests)

# Developer tool: draws the editor offscreen into a PNG (no host needed). Not built by default.
add_executable(the_stripper_render EXCLUDE_FROM_ALL tools/render_editor.cpp ${VST3_SDK_PUBLIC}/source/main/moduleinit.cpp ${VST3_SDK_PUBLIC}/source/main/pluginfactory.cpp src/controller.cpp src/ui/editor.cpp src/ui/panel_view.cpp
  src/ui/browser_widgets.cpp
  src/ui/model_browser.cpp
  src/ui/model_strip.cpp src/ui/tape_text.cpp src/ui/tape_piece_view.cpp src/ui/back_widgets.cpp src/ui/back_view.cpp src/ui/flip_view.cpp)
target_include_directories(the_stripper_render PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${VST3_SDK_PATH} ${VST3_SDK_BASE}/source ${VST3_SDK_PLUGINTERFACES}/base ${VST3_SDK_PLUGINTERFACES}/vst ${VST3_SDK_PUBLIC}/source ${VST3_SDK_PUBLIC}/source/vst)
target_link_libraries(the_stripper_render PRIVATE sdk pluginterfaces fairy_ui fairy_ui_widgets)

# Model messages (header-only, fake message objects): no SDK libraries needed.
add_executable(the_stripper_message_tests tests/test_messages.cpp)
target_include_directories(the_stripper_message_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME the_stripper_message_tests COMMAND the_stripper_message_tests)

# Module/factory lifecycle smoke test: dlopens the built bundle from dist/ (exit 77 = skipped
# when the plugin has not been built). Links only the pluginterfaces IIDs, not the plugin.
if(NOT WIN32)
  if(APPLE)
    set(THE_STRIPPER_MODULE_PATH "${SUITE_DIST_DIR}/TheStripper.vst3/Contents/MacOS/TheStripper")
  else()
    set(THE_STRIPPER_MODULE_PATH "${SUITE_DIST_DIR}/TheStripper.vst3/Contents/x86_64-linux/TheStripper.so")
  endif()
  add_executable(the_stripper_factory_test tests/test_factory.cpp)
  target_include_directories(the_stripper_factory_test PRIVATE ${VST3_SDK_PATH})
  target_compile_definitions(the_stripper_factory_test PRIVATE
    THE_STRIPPER_MODULE="${THE_STRIPPER_MODULE_PATH}")
  target_link_libraries(the_stripper_factory_test PRIVATE pluginterfaces ${CMAKE_DL_LIBS})
  add_test(NAME the_stripper_factory_test COMMAND the_stripper_factory_test)
  set_tests_properties(the_stripper_factory_test PROPERTIES SKIP_RETURN_CODE 77)

  # Processor metering through the real module (mock host queues); skipped when the module is not built.
  add_executable(the_stripper_meter_module_test tests/test_processor_meters.cpp ${VST3_SDK_PATH}/public.sdk/source/vst/vstinitiids.cpp)
  target_include_directories(the_stripper_meter_module_test PRIVATE ${VST3_SDK_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_compile_definitions(the_stripper_meter_module_test PRIVATE
    THE_STRIPPER_MODULE="${THE_STRIPPER_MODULE_PATH}")
  target_link_libraries(the_stripper_meter_module_test PRIVATE pluginterfaces ${CMAKE_DL_LIBS})
  add_test(NAME the_stripper_meter_module_test COMMAND the_stripper_meter_module_test)
  set_tests_properties(the_stripper_meter_module_test PROPERTIES SKIP_RETURN_CODE 77)

  # Model loading through messages, on the real module (mock host, peer and streams); skipped when the module is not built.
  add_executable(the_stripper_model_module_test tests/test_processor_model.cpp ${VST3_SDK_PATH}/public.sdk/source/vst/vstinitiids.cpp)
  target_include_directories(the_stripper_model_module_test PRIVATE ${VST3_SDK_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_compile_definitions(the_stripper_model_module_test PRIVATE
    THE_STRIPPER_MODULE="${THE_STRIPPER_MODULE_PATH}"
    NAM_DRIVE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/../../shared/nam_drive/tests/fixtures")
  target_link_libraries(the_stripper_model_module_test PRIVATE pluginterfaces pthread ${CMAKE_DL_LIBS})
  add_test(NAME the_stripper_model_module_test COMMAND the_stripper_model_module_test)
  set_tests_properties(the_stripper_model_module_test PROPERTIES SKIP_RETURN_CODE 77)
endif()

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
