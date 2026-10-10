# Fairy Tapes · nam_drive — NAM drive DSP wrapper shared by plugins
#
# Static library: NeuralAmpModelerCore sources + NamDrive (GPU path via gpu_nam, CPU fallback).
# gpu_nam must be added before this directory (add_subdirectory(shared/gpu_nam ...)).
#
# Usage from a plugin CMakeLists.txt:
#   add_subdirectory(../../shared/gpu_nam gpu_nam)
#   add_subdirectory(../../shared/nam_drive nam_drive)
#   target_link_libraries(my_plugin PRIVATE nam_drive)

set(NAM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../vendors/NeuralAmpModelerCore")
set(NAM_SOURCES
  ${NAM_ROOT}/NAM/activations.cpp
  ${NAM_ROOT}/NAM/container.cpp
  ${NAM_ROOT}/NAM/conv1d.cpp
  ${NAM_ROOT}/NAM/convnet.cpp
  ${NAM_ROOT}/NAM/dsp.cpp
  ${NAM_ROOT}/NAM/get_dsp.cpp
  ${NAM_ROOT}/NAM/lstm.cpp
  ${NAM_ROOT}/NAM/ring_buffer.cpp
  ${NAM_ROOT}/NAM/slimmable_wavenet.cpp
  ${NAM_ROOT}/NAM/util.cpp
  ${NAM_ROOT}/NAM/wavenet.cpp
)

add_library(nam_drive STATIC src/nam_drive.cpp ${NAM_SOURCES})
target_compile_features(nam_drive PUBLIC cxx_std_20)
set_target_properties(nam_drive PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(nam_drive PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${NAM_ROOT}
  ${NAM_ROOT}/Dependencies/eigen
  ${NAM_ROOT}/Dependencies/nlohmann
)
target_compile_definitions(nam_drive PUBLIC NAM_SAMPLE_FLOAT)
target_link_libraries(nam_drive PUBLIC gpu_nam)
