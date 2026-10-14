# Fairy Tapes shared UI: header-only control math (no VSTGUI dependency) and
# the code-drawn VSTGUI controls (fairy_ui_widgets).
cmake_minimum_required(VERSION 3.25)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(fairy_ui INTERFACE)
target_include_directories(fairy_ui INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(fairy_ui INTERFACE cxx_std_20)

add_executable(fairy_ui_tests tests/test_knob_math.cpp)
target_link_libraries(fairy_ui_tests PRIVATE fairy_ui)
add_test(NAME fairy_ui_tests COMMAND fairy_ui_tests)

add_executable(fairy_ui_meter_math_tests tests/test_meter_math.cpp)
target_link_libraries(fairy_ui_meter_math_tests PRIVATE fairy_ui)
add_test(NAME fairy_ui_meter_math_tests COMMAND fairy_ui_meter_math_tests)

add_executable(fairy_ui_editor_size_tests tests/test_editor_size.cpp)
target_link_libraries(fairy_ui_editor_size_tests PRIVATE fairy_ui)
add_test(NAME fairy_ui_editor_size_tests COMMAND fairy_ui_editor_size_tests)

add_executable(fairy_ui_rack_geometry_tests tests/test_rack_geometry.cpp)
target_link_libraries(fairy_ui_rack_geometry_tests PRIVATE fairy_ui)
add_test(NAME fairy_ui_rack_geometry_tests COMMAND fairy_ui_rack_geometry_tests)

add_executable(fairy_ui_control_wear_tests tests/test_control_wear.cpp)
target_link_libraries(fairy_ui_control_wear_tests PRIVATE fairy_ui)
add_test(NAME fairy_ui_control_wear_tests COMMAND fairy_ui_control_wear_tests)

add_executable(fairy_ui_sanitize_tests tests/test_sanitize.cpp)
target_link_libraries(fairy_ui_sanitize_tests PRIVATE fairy_ui)
add_test(NAME fairy_ui_sanitize_tests COMMAND fairy_ui_sanitize_tests)

# Code-drawn VSTGUI controls and optional bitmap skin.
add_library(fairy_ui_widgets STATIC src/controls.cpp src/skin.cpp src/meters.cpp src/rack_controls.cpp)
target_include_directories(fairy_ui_widgets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(fairy_ui_widgets PUBLIC fairy_ui vstgui_support)
# Linked into plugin modules (shared objects).
set_target_properties(fairy_ui_widgets PROPERTIES POSITION_INDEPENDENT_CODE ON)
# VSTGUI headers use multi-character constants; the SDK silences this only in its own directories.
target_compile_options(fairy_ui_widgets PUBLIC $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-multichar>)

# Meter control behaviour that does not need a window (links the VSTGUI-based widgets library).
add_executable(fairy_ui_meter_control_tests tests/test_meter_controls.cpp)
target_link_libraries(fairy_ui_meter_control_tests PRIVATE fairy_ui_widgets)
add_test(NAME fairy_ui_meter_control_tests COMMAND fairy_ui_meter_control_tests)
