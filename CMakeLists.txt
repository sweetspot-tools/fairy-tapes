# Fairy Tapes — Suite of VST3 plugins
# Build from this directory: mkdir build && cd build && cmake .. && cmake --build .

cmake_minimum_required(VERSION 3.25)
project(FairyTapes VERSION 1.0.0 LANGUAGES CXX)

# VST3 SDK: default path when building from suite root
set(VST3_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/vendors/vst3sdk" CACHE PATH "Path to Steinberg VST3 SDK")

# Build the VST3 SDK libraries (base, pluginterfaces, sdk_common, sdk) from source.
# Examples, hosting samples and VSTGUI are off; EXCLUDE_FROM_ALL builds only what plugins link.
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
add_subdirectory(${VST3_SDK_PATH} vst3sdk EXCLUDE_FROM_ALL)

enable_testing()
add_subdirectory(src/the-stripper)
