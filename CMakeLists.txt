# Fairy Tapes — Suite of VST3 plugins
# Build from this directory: mkdir build && cd build && cmake .. && cmake --build .

cmake_minimum_required(VERSION 3.16)
project(FairyTapes VERSION 1.0.0 LANGUAGES CXX)

# VST3 SDK: default path when building from suite root
set(VST3_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/vendors/vst3sdk" CACHE PATH "Path to Steinberg VST3 SDK")

enable_testing()
add_subdirectory(src/the-stripper)
