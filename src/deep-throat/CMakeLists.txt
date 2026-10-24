# Fairy Tapes · Deep Throat — VST3 vocal channel strip
# Requires VST3 SDK at ${VST3_SDK_PATH} (defined by the suite root CMakeLists.txt)

cmake_minimum_required(VERSION 3.25)
project(DeepThroat VERSION 1.0.0 LANGUAGES CXX)

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

# Pure DSP + engine sources (no SDK libs), shared by the plugin and the unit tests.
set(DEEP_THROAT_DSP_SOURCES
  src/engine.cpp
  src/dsp/rider.cpp
  src/dsp/spectral_breath_detector.cpp
  src/dsp/level_breath_detector.cpp
  src/dsp/breath_reducer.cpp
  src/dsp/corrective_eq.cpp
  src/dsp/deesser.cpp
  src/dsp/distressor_comp.cpp
  src/dsp/tone_eq.cpp
)

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
# MODULE (not SHARED), as the SDK's smtg_add_vst3plugin does: CMake only honours BUNDLE
# for MODULE libraries on macOS, and on Windows a MODULE .dll is a LIBRARY output.
add_library(deep_throat MODULE
  ${PLUGIN_MAIN_SOURCE}
  src/plug.cpp
  src/processor.cpp
  src/controller.cpp
  ${DEEP_THROAT_DSP_SOURCES}
)

target_include_directories(deep_throat PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${VST3_SDK_PATH}
  ${VST3_SDK_PATH}/pluginterfaces/base
  ${VST3_SDK_PATH}/pluginterfaces/vst
)

target_compile_definitions(deep_throat PRIVATE UNICODE _UNICODE NDEBUG)
if(WIN32)
  target_compile_definitions(deep_throat PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Bundle layout mirrors vendors/vst3sdk/cmake/modules/SMTG_AddSMTGLibrary.cmake
# (smtg_target_make_plugin_package), written straight into dist/ (suite root).
# Output directories use a generator expression so multi-config generators
# (Visual Studio, Xcode, Ninja Multi-Config) do not append a /<Config> subfolder.
set(DEEP_THROAT_BUNDLE "${SUITE_DIST_DIR}/DeepThroat.vst3")
set_target_properties(deep_throat PROPERTIES OUTPUT_NAME "DeepThroat" PREFIX "")
if(APPLE)
  # CMake creates "<OUTPUT_NAME>.<BUNDLE_EXTENSION>" INSIDE LIBRARY_OUTPUT_DIRECTORY:
  # dist/DeepThroat.vst3/Contents/{MacOS/DeepThroat, Info.plist}.
  set_target_properties(deep_throat PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "vst3"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/Info.plist"
    LIBRARY_OUTPUT_DIRECTORY "$<1:${SUITE_DIST_DIR}>"
  )
elseif(WIN32)
  # dist/DeepThroat.vst3/Contents/x86_64-win/DeepThroat.vst3 (the DLL renamed .vst3).
  set_target_properties(deep_throat PROPERTIES
    SUFFIX ".vst3"
    LIBRARY_OUTPUT_DIRECTORY "$<1:${DEEP_THROAT_BUNDLE}/Contents/x86_64-win>"
    PDB_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/WIN_PDB64"
  )
else()
  # dist/DeepThroat.vst3/Contents/x86_64-linux/DeepThroat.so
  set_target_properties(deep_throat PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "$<1:${DEEP_THROAT_BUNDLE}/Contents/x86_64-linux>"
  )
endif()

target_link_libraries(deep_throat PRIVATE sdk fairy_ui)
if(APPLE)
  target_link_libraries(deep_throat PRIVATE "-framework CoreFoundation")
  target_link_options(deep_throat PRIVATE
    "-exported_symbols_list" "${VST3_SDK_PATH}/public.sdk/source/main/macexport.exp")
endif()
if(UNIX AND NOT APPLE)
  target_link_libraries(deep_throat PRIVATE pthread)
endif()

# ---------------------------------------------------------------------------
# Tests (CTest) — header-only dependency on the VST3 SDK (no SDK libs linked)
# ---------------------------------------------------------------------------
add_executable(deep_throat_tests
  tests/test_deep_throat.cpp
  ${DEEP_THROAT_DSP_SOURCES}
)
target_include_directories(deep_throat_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${VST3_SDK_PATH}
  ${VST3_SDK_PATH}/pluginterfaces/base
  ${VST3_SDK_PATH}/pluginterfaces/vst
)
target_link_libraries(deep_throat_tests PRIVATE fairy_ui)  # header-only (fairy_ui/knob_math.h)
add_test(NAME deep_throat_tests COMMAND deep_throat_tests)

# Module/factory lifecycle smoke test: dlopens the built bundle from dist/ (exit 77 = skipped
# when the plugin has not been built). Links only the pluginterfaces IIDs, not the plugin.
if(NOT WIN32)
  if(APPLE)
    set(DEEP_THROAT_MODULE_PATH "${SUITE_DIST_DIR}/DeepThroat.vst3/Contents/MacOS/DeepThroat")
  else()
    set(DEEP_THROAT_MODULE_PATH "${SUITE_DIST_DIR}/DeepThroat.vst3/Contents/x86_64-linux/DeepThroat.so")
  endif()
  # Also compiles the pure engine to compare the plugin's mono output with Engine's mono path.
  add_executable(deep_throat_factory_test tests/test_factory.cpp ${DEEP_THROAT_DSP_SOURCES})
  target_include_directories(deep_throat_factory_test PRIVATE ${VST3_SDK_PATH}
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_compile_definitions(deep_throat_factory_test PRIVATE
    DEEP_THROAT_MODULE="${DEEP_THROAT_MODULE_PATH}")
  target_link_libraries(deep_throat_factory_test PRIVATE pluginterfaces fairy_ui ${CMAKE_DL_LIBS})
  add_test(NAME deep_throat_factory_test COMMAND deep_throat_factory_test)
  set_tests_properties(deep_throat_factory_test PROPERTIES SKIP_RETURN_CODE 77)

  # State v2 (seed, screw angles, tape texts) and the Decor messages on the real module, with the SDK's minimal host
  # classes (IHostApplication, IMessage); skipped when the module is absent.
  add_executable(deep_throat_state_module_test tests/test_state_module.cpp
    ${VST3_SDK_PATH}/public.sdk/source/vst/vstinitiids.cpp
    ${VST3_SDK_PATH}/public.sdk/source/vst/hosting/hostclasses.cpp
    ${VST3_SDK_PATH}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
    ${VST3_SDK_PATH}/public.sdk/source/vst/utility/stringconvert.cpp
    ${VST3_SDK_PATH}/public.sdk/source/common/commonstringconvert.cpp)
  target_include_directories(deep_throat_state_module_test PRIVATE ${VST3_SDK_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_compile_definitions(deep_throat_state_module_test PRIVATE
    DEEP_THROAT_MODULE="${DEEP_THROAT_MODULE_PATH}")
  target_link_libraries(deep_throat_state_module_test PRIVATE pluginterfaces fairy_ui ${CMAKE_DL_LIBS})
  add_test(NAME deep_throat_state_module_test COMMAND deep_throat_state_module_test)
  set_tests_properties(deep_throat_state_module_test PROPERTIES SKIP_RETURN_CODE 77)
endif()
