# Fairy Tapes · shared Inflator DSP (waveshaper maths ported from ZLInflator, GPL-3.0). Header-only, no JUCE.
# Standalone test build: cmake -S shared/inflator -B <scratch>/inflator && cmake --build <scratch>/inflator && ctest --test-dir <scratch>/inflator

cmake_minimum_required(VERSION 3.25)
project(Inflator VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

add_executable(inflator_tests tests/test_inflator.cpp)
target_include_directories(inflator_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME inflator_tests COMMAND inflator_tests)
