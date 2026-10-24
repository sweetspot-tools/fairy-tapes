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
  ${NAM_ROOT}/NAM/linear.cpp
  ${NAM_ROOT}/NAM/lstm.cpp
  ${NAM_ROOT}/NAM/nam_file.cpp
  ${NAM_ROOT}/NAM/ring_buffer.cpp
  ${NAM_ROOT}/NAM/sequential.cpp
  ${NAM_ROOT}/NAM/util.cpp
  ${NAM_ROOT}/NAM/wav.cpp
  # v0.6.0 moved WaveNet (the most common .nam architecture) into NAM/wavenet/: without these files the
  # WaveNet parser is never registered and those models fail with "No config parser registered".
  ${NAM_ROOT}/NAM/wavenet/model.cpp
  ${NAM_ROOT}/NAM/wavenet/a2_fast.cpp
  ${NAM_ROOT}/NAM/wavenet/slimmable.cpp
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

# NeuralAmpModelerCore registers its architectures (Linear, WaveNet, ...) with static initialisers inside the
# .cpp files. In a static library the linker drops object files nobody references, which would silently remove
# those architectures ("No config parser registered for architecture: ..."). Consumers must therefore link the
# whole archive: use nam_drive_link(<target>) instead of target_link_libraries(<target> ... nam_drive).
function(nam_drive_link target)
  target_link_libraries(${target} PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,nam_drive>")
endfunction()

# Tests (CTest): asynchronous loading. Fixtures live in tests/fixtures.
add_executable(nam_drive_tests tests/test_nam_drive_async.cpp)
nam_drive_link(nam_drive_tests)
target_compile_definitions(nam_drive_tests PRIVATE NAM_DRIVE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
if(UNIX AND NOT APPLE)
  target_link_libraries(nam_drive_tests PRIVATE pthread)
endif()
add_test(NAME nam_drive_tests COMMAND nam_drive_tests)
