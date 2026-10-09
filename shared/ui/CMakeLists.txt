# Fairy Tapes shared UI: header-only control math (no VSTGUI dependency).
cmake_minimum_required(VERSION 3.25)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(fairy_ui INTERFACE)
target_include_directories(fairy_ui INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(fairy_ui INTERFACE cxx_std_20)

add_executable(fairy_ui_tests tests/test_knob_math.cpp)
target_link_libraries(fairy_ui_tests PRIVATE fairy_ui)
add_test(NAME fairy_ui_tests COMMAND fairy_ui_tests)
