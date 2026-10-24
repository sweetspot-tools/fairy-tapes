# Fairy Tapes · Hotline — VST3 telephone / lo-fi device emulation
# Requires VST3 SDK at ${VST3_SDK_PATH} (defined by the suite root CMakeLists.txt)

cmake_minimum_required(VERSION 3.25)
project(Hotline VERSION 1.0.0 LANGUAGES CXX)

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

# MODULE (not SHARED), as the SDK's smtg_add_vst3plugin does: CMake only honours BUNDLE
# for MODULE libraries on macOS, and on Windows a MODULE .dll is a LIBRARY output.
add_library(hotline MODULE
  ${PLUGIN_MAIN_SOURCE}
  src/plug.cpp
  src/processor.cpp
  src/controller.cpp
  src/devices.cpp
  src/engine.cpp
  src/ui/big_knob.cpp
  src/ui/decor.cpp
  src/ui/editor.cpp
  src/ui/panel_view.cpp
  src/ui/prefs.cpp
  src/ui/tape_view.cpp
)

target_include_directories(hotline PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${VST3_SDK_PATH}
  ${VST3_SDK_PATH}/pluginterfaces/base
  ${VST3_SDK_PATH}/pluginterfaces/vst
)

target_compile_definitions(hotline PRIVATE UNICODE _UNICODE NDEBUG)
if(WIN32)
  target_compile_definitions(hotline PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Bundle layout mirrors vendors/vst3sdk/cmake/modules/SMTG_AddSMTGLibrary.cmake
# (smtg_target_make_plugin_package), written straight into dist/ (suite root).
set(HOTLINE_BUNDLE "${SUITE_DIST_DIR}/Hotline.vst3")
set_target_properties(hotline PROPERTIES OUTPUT_NAME "Hotline" PREFIX "")
if(APPLE)
  set_target_properties(hotline PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "vst3"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/Info.plist"
    LIBRARY_OUTPUT_DIRECTORY "$<1:${SUITE_DIST_DIR}>"
  )
elseif(WIN32)
  set_target_properties(hotline PROPERTIES
    SUFFIX ".vst3"
    LIBRARY_OUTPUT_DIRECTORY "$<1:${HOTLINE_BUNDLE}/Contents/x86_64-win>"
    PDB_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/WIN_PDB64"
  )
else()
  set_target_properties(hotline PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "$<1:${HOTLINE_BUNDLE}/Contents/x86_64-linux>"
  )
endif()

# Bitmap resources go to Contents/Resources of the bundle, where VSTGUI looks for them: the panel grain tile shared by
# the suite (shared/ui/assets). Without it the panel is drawn in plain #161617.
set(FAIRY_UI_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/../../shared/ui/assets")
add_custom_command(TARGET hotline POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${HOTLINE_BUNDLE}/Contents/Resources"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FAIRY_UI_ASSETS}/panel_grain.png"
          "${HOTLINE_BUNDLE}/Contents/Resources/panel_grain.png"
  VERBATIM)

target_link_libraries(hotline PRIVATE sdk fairy_ui fairy_ui_widgets)
if(APPLE)
  target_link_libraries(hotline PRIVATE "-framework CoreFoundation")
  target_link_options(hotline PRIVATE
    "-exported_symbols_list" "${VST3_SDK_PATH}/public.sdk/source/main/macexport.exp")
endif()
if(UNIX AND NOT APPLE)
  target_link_libraries(hotline PRIVATE pthread)
endif()

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

# Front panel decor (pure C++, no VSTGUI): tapes and screws from the seed, screw rotations, writing on the tapes, wear
# seeds, user preferences file. The wear marks themselves are shared (fairy_ui/wear.h).
add_executable(hotline_decor_tests
  tests/test_decor.cpp
  src/ui/decor.cpp
  src/ui/prefs.cpp
)
target_include_directories(hotline_decor_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${VST3_SDK_PATH})
target_link_libraries(hotline_decor_tests PRIVATE fairy_ui)
add_test(NAME hotline_decor_tests COMMAND hotline_decor_tests)

# Geometry of the big Morph knob (VSTGUI views, no window).
add_executable(hotline_ui_tests tests/test_ui.cpp src/ui/big_knob.cpp)
target_include_directories(hotline_ui_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${VST3_SDK_PATH})
target_link_libraries(hotline_ui_tests PRIVATE sdk fairy_ui_widgets)
add_test(NAME hotline_ui_tests COMMAND hotline_ui_tests)

# Developer tool: draws the editor offscreen into a PNG (no host needed). Not built by default.
add_executable(hotline_render EXCLUDE_FROM_ALL
  tools/render_editor.cpp
  ${VST3_SDK_PATH}/public.sdk/source/main/moduleinit.cpp
  ${VST3_SDK_PATH}/public.sdk/source/main/pluginfactory.cpp
  src/controller.cpp
  src/devices.cpp
  src/engine.cpp
  src/ui/big_knob.cpp
  src/ui/decor.cpp
  src/ui/editor.cpp
  src/ui/panel_view.cpp
  src/ui/prefs.cpp
  src/ui/tape_view.cpp
)
target_include_directories(hotline_render PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${VST3_SDK_PATH})
target_compile_definitions(hotline_render PRIVATE
  HOTLINE_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/../../shared/ui/assets/")
target_link_libraries(hotline_render PRIVATE sdk pluginterfaces fairy_ui fairy_ui_widgets)

# Module/factory lifecycle smoke test: dlopens the built bundle from dist/ (exit 77 = skipped
# when the plugin has not been built). Links only the pluginterfaces IIDs, not the plugin.
if(NOT WIN32)
  if(APPLE)
    set(HOTLINE_MODULE_PATH "${SUITE_DIST_DIR}/Hotline.vst3/Contents/MacOS/Hotline")
  else()
    set(HOTLINE_MODULE_PATH "${SUITE_DIST_DIR}/Hotline.vst3/Contents/x86_64-linux/Hotline.so")
  endif()
  add_executable(hotline_factory_test tests/test_factory.cpp)
  target_include_directories(hotline_factory_test PRIVATE ${VST3_SDK_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_compile_definitions(hotline_factory_test PRIVATE HOTLINE_MODULE="${HOTLINE_MODULE_PATH}")
  target_link_libraries(hotline_factory_test PRIVATE pluginterfaces ${CMAKE_DL_LIBS})
  add_test(NAME hotline_factory_test COMMAND hotline_factory_test)
  set_tests_properties(hotline_factory_test PROPERTIES SKIP_RETURN_CODE 77)

  # State v2 (seed, screw angles, tape texts) and the Decor messages on the real module, with the SDK's minimal host
  # classes (IHostApplication, IMessage); skipped when the module is absent.
  add_executable(hotline_state_module_test tests/test_state_module.cpp
    ${VST3_SDK_PATH}/public.sdk/source/vst/vstinitiids.cpp
    ${VST3_SDK_PATH}/public.sdk/source/vst/hosting/hostclasses.cpp
    ${VST3_SDK_PATH}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
    ${VST3_SDK_PATH}/public.sdk/source/vst/utility/stringconvert.cpp
    ${VST3_SDK_PATH}/public.sdk/source/common/commonstringconvert.cpp
    src/devices.cpp src/engine.cpp)
  target_include_directories(hotline_state_module_test PRIVATE ${VST3_SDK_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_compile_definitions(hotline_state_module_test PRIVATE HOTLINE_MODULE="${HOTLINE_MODULE_PATH}")
  target_link_libraries(hotline_state_module_test PRIVATE pluginterfaces fairy_ui ${CMAKE_DL_LIBS})
  add_test(NAME hotline_state_module_test COMMAND hotline_state_module_test)
  set_tests_properties(hotline_state_module_test PROPERTIES SKIP_RETURN_CODE 77)
endif()
