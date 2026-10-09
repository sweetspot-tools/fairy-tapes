# Fairy Tapes · gpu_nam — GPU-accelerated NAM inference via CUDA
#
# Builds a static library (gpu_nam) that provides batched WaveNet inference
# on NVIDIA GPUs. Falls back gracefully if CUDA is not available at build time:
# the library compiles as a no-op stub so that dependents can link unconditionally.
#
# Usage from a plugin CMakeLists.txt:
#   add_subdirectory(../../shared/gpu_nam gpu_nam_build)
#   target_link_libraries(my_plugin PRIVATE gpu_nam)

cmake_minimum_required(VERSION 3.18)

option(GPU_NAM_DISABLE_CUDA "Build the CPU-only stub even if CUDA is installed" OFF)

include(CheckLanguage)
if(NOT GPU_NAM_DISABLE_CUDA)
  check_language(CUDA)
endif()

if(CMAKE_CUDA_COMPILER)
  project(gpu_nam VERSION 1.0.0 LANGUAGES CXX CUDA)
  set(GPU_NAM_HAS_CUDA TRUE)
  message(STATUS "[gpu_nam] CUDA compiler found: ${CMAKE_CUDA_COMPILER}")
else()
  project(gpu_nam VERSION 1.0.0 LANGUAGES CXX)
  set(GPU_NAM_HAS_CUDA FALSE)
  message(STATUS "[gpu_nam] CUDA not found — building CPU-only stub")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# NeuralAmpModelerCore — needed for model parsing (JSON config + weights)
# ---------------------------------------------------------------------------
set(NAM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../vendors/NeuralAmpModelerCore")
set(NAM_INCLUDE_DIRS
  ${NAM_ROOT}
  ${NAM_ROOT}/Dependencies/eigen
  ${NAM_ROOT}/Dependencies/nlohmann
)

# NAM sources needed for model parsing only (get_dsp, wavenet config, etc.)
set(NAM_PARSE_SOURCES
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

# ---------------------------------------------------------------------------
# Library sources
# ---------------------------------------------------------------------------
set(GPU_NAM_HEADERS
  include/gpu_nam_common.h
  include/gpu_nam_model.h
  include/gpu_nam_engine.h
  include/gpu_nam_manager.h
)

if(GPU_NAM_HAS_CUDA)
  set(GPU_NAM_SOURCES
    src/gpu_nam_model.cu
    src/gpu_nam_engine.cu
    src/gpu_nam_manager.cpp
    cuda/wavenet_kernel.cu
  )

  add_library(gpu_nam STATIC ${GPU_NAM_SOURCES} ${GPU_NAM_HEADERS})

  set_target_properties(gpu_nam PROPERTIES
    CUDA_STANDARD 17
    CUDA_STANDARD_REQUIRED ON
    CUDA_SEPARABLE_COMPILATION ON
    CUDA_RESOLVE_DEVICE_SYMBOLS ON
    POSITION_INDEPENDENT_CODE ON
  )

  # Target compute capability 7.5+ (Turing and newer)
  set_target_properties(gpu_nam PROPERTIES CUDA_ARCHITECTURES "75;86;89")

  target_compile_definitions(gpu_nam PUBLIC GPU_NAM_HAS_CUDA=1)
  target_compile_definitions(gpu_nam PRIVATE NAM_SAMPLE_FLOAT)

  # Find CUDA toolkit include dir for cuda_runtime.h
  find_path(CUDA_INCLUDE_DIR cuda_runtime.h
    HINTS ${CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES}
          /opt/cuda/include
          /usr/local/cuda/include)

  target_include_directories(gpu_nam PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CUDA_INCLUDE_DIR}
  )
  target_include_directories(gpu_nam PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/cuda
    ${NAM_INCLUDE_DIRS}
  )

  # Find cudart with full path so static-lib consumers can resolve it.
  find_library(CUDART_LIBRARY cudart
    HINTS ${CMAKE_CUDA_IMPLICIT_LINK_DIRECTORIES}
          /opt/cuda/targets/x86_64-linux/lib
          /opt/cuda/lib64
          /usr/local/cuda/lib64)
  if(CUDART_LIBRARY)
    target_link_libraries(gpu_nam PUBLIC ${CUDART_LIBRARY})
  else()
    target_link_libraries(gpu_nam PUBLIC cudart)
  endif()

else()
  # CPU-only stub: the public headers compile but all functions return "not available"
  set(GPU_NAM_SOURCES src/gpu_nam_manager.cpp src/gpu_nam_stub.cpp)

  add_library(gpu_nam STATIC ${GPU_NAM_SOURCES} ${GPU_NAM_HEADERS})

  target_compile_definitions(gpu_nam PUBLIC GPU_NAM_HAS_CUDA=0)
  target_include_directories(gpu_nam PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  )
endif()

# gpu_nam is linked into shared libraries (VST3 modules) in both branches.
set_target_properties(gpu_nam PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(UNIX AND NOT APPLE)
  target_link_libraries(gpu_nam PRIVATE pthread stdc++fs)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(GPU_NAM_HAS_CUDA)
  enable_testing()

  add_executable(gpu_nam_tests
    tests/test_gpu_wavenet.cpp
    ${NAM_PARSE_SOURCES}
  )

  target_include_directories(gpu_nam_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${NAM_INCLUDE_DIRS}
  )

  target_compile_definitions(gpu_nam_tests PRIVATE NAM_SAMPLE_FLOAT)

  target_link_libraries(gpu_nam_tests PRIVATE gpu_nam)

  if(UNIX AND NOT APPLE)
    target_link_libraries(gpu_nam_tests PRIVATE pthread stdc++fs)
  endif()

  add_test(NAME gpu_nam_tests COMMAND gpu_nam_tests)
endif()
