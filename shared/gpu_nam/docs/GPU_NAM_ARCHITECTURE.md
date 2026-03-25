# GPU NAM — Architecture & Developer Guide

Custom CUDA implementation for Neural Amp Modeler (NAM) WaveNet inference,
designed for multi-instance performance in a DAW context.

## Overview

The `gpu_nam` module offloads NAM WaveNet inference from CPU to GPU, enabling
dozens of plugin instances (e.g. The Stripper on every track) to run
simultaneously without saturating CPU cores.

**Key design decisions:**

- **Custom CUDA kernels** — no dependency on `gpuaudio-sdk` (license-incompatible
  with GPL-3.0) or external inference frameworks.
- **Singleton GPU Manager** — one process-wide manager handles model
  deduplication and instance routing.
- **Automatic prewarm** — GPU ring buffers are pre-filled with the model's
  bias-derived state to match CPU NAM's `Reset()` behaviour (critical for
  numerical correctness).
- **CPU fallback** — when CUDA is unavailable, the module compiles as a no-op
  stub and the plugin falls back to NeuralAmpModelerCore on CPU.

## Module Layout

```
shared/gpu_nam/
├── include/
│   ├── gpu_nam_common.h     # Types, constants, WavenetDesc
│   ├── gpu_nam_model.h      # Model loader (JSON → GPU weights)
│   ├── gpu_nam_engine.h     # Per-instance state + inference
│   └── gpu_nam_manager.h    # Singleton manager
├── src/
│   ├── gpu_nam_model.cu     # Model parsing + device upload
│   ├── gpu_nam_engine.cu    # Engine lifecycle, prewarm, process
│   └── gpu_nam_manager.cpp  # Singleton, deduplication, routing
├── cuda/
│   ├── wavenet_kernel.cuh   # Kernel declarations
│   └── wavenet_kernel.cu    # CUDA WaveNet inference kernel
├── tests/
│   └── test_gpu_wavenet.cpp # Test suite (GPU vs CPU comparison)
├── docs/
│   └── GPU_NAM_ARCHITECTURE.md  # This file
└── CMakeLists.txt           # Build system (CUDA + CPU stub)
```

## Component Responsibilities

### `GpuNamModel`

Loads a `.nam` file, parses the WaveNet JSON config into a `WavenetDesc`
struct, extracts the flat weight array, and uploads it to GPU device memory
via `cudaMalloc` + `cudaMemcpy`.

- **Model validation**: rejects unsupported features (FiLM, condition DSP,
  grouped convolutions, blended gating) with `ModelNotSupported`.
- **Weight offset computation**: pre-computes byte offsets for every Conv1D,
  Conv1x1, bias, and head operation so the CUDA kernel can index weights
  directly without parsing at runtime.
- **Deduplication-ready**: weights are read-only once uploaded; multiple
  engines can reference the same `GpuNamModel`.

### `GpuNamEngine`

Manages per-instance GPU state and executes inference.

- **Ring buffers**: pre-allocated for `kMaxInstances` (256) instances. Each
  instance gets its own contiguous block of ring buffer memory.
- **Prewarm**: on `registerInstance()` and `resetInstance()`, automatically
  processes `prewarm_samples_` zeros through the WaveNet. This fills the ring
  buffers with the model's bias-derived state, matching CPU NAM's
  `DSP::Reset()` → `prewarm()` sequence. Without this, outputs diverge from
  the CPU from the very first sample.
- **Process**: copies host audio → device, launches kernel, copies output back.
  Phase 1 is synchronous; Phase 2 will add batched async launches.

### `GpuNamManager`

Process-wide singleton (`GpuNamManager::instance()`).

- **CUDA detection**: queries device count and properties at construction.
- **Model deduplication**: if two plugin instances load the same `.nam` file
  (by canonical path), they share GPU weights.
- **Thread safety**: `loadModel`, `unloadModel`, `registerInstance`,
  `unregisterInstance` are mutex-protected. `process()` is lock-free
  (safe for different instance IDs from different threads).

### `wavenet_kernel.cu`

The core CUDA kernel implementing WaveNet inference.

**Architecture per audio block of T samples:**

```
for each sample t in [0, T):
  for each layer_array a:
    rechannel(input, 1×1) → layer_input[channels]
    for each layer l in a:
      dilated_conv(layer_input, ring) → z[conv_out]
      mixin(condition) → z += mixin_out
      gated_activation(z) → act[bottleneck]
      layer1x1(act) → residual; layer_input += residual
      head_accum += head1x1(act) or act
    head_rechannel(head_accum) → output
  output = head_scale × final_head
```

**Thread model (Phase 1):**
- 1 CUDA block, `channels` threads (rounded to warp size).
- Each thread owns one output channel for matmul accumulation.
- Samples processed sequentially (WaveNet is causal).
- Layers processed sequentially (each depends on prior).
- Shared memory holds all intermediate activations (7 × kMaxChannels floats).

**Weight access:**
- All weights in global memory (L2-cached for small models).
- Column-major layout matching Eigen's default and NAM file order.

## Prewarm Mechanism

NAM's CPU `DSP::Reset()` calls `prewarm()`, which processes N zero-input
samples through the network. With biases in the dilated convolutions, zero
input produces non-zero outputs that propagate through layers and fill ring
buffers with bias-derived state.

**Prewarm sample count** (matches `WaveNet::PrewarmSamples()`):

```
N = 1 + Σ (dilation × (kernel_size - 1))  for all layers in all arrays
```

Examples:
- Simple model (4 layers, dilations [1,2,4,8], kernel 3): N = 23
- A1 standard (20 layers, dilations up to 512, kernel 3): N = 4093

Without prewarm, the GPU output diverges from CPU from sample 0. With
prewarm, GPU and CPU agree within float epsilon accumulation (~1e-7 for small
models, ~4e-7 for large models).

## Numerical Accuracy

After prewarm alignment, the GPU kernel matches the CPU reference
(NeuralAmpModelerCore) with the following measured precision:

| Model               | Weights | Layers | max_abs_err |
|---------------------|---------|--------|-------------|
| wavenet.nam         | 131     | 3      | 1.12e-07    |
| wavenet_a1_standard | 13,802  | 20     | 3.87e-07    |

The small differences come from floating-point operation ordering differences
between sequential CPU (Eigen) and parallel GPU (CUDA) computation.

## Build System

The CMakeLists.txt uses `check_language(CUDA)` for conditional compilation:
- **CUDA found**: builds full library + kernels, defines `GPU_NAM_HAS_CUDA=1`.
- **CUDA not found**: builds a CPU-only stub with `GPU_NAM_HAS_CUDA=0`.

Target architectures: `"75;86;89"` (Turing, Ampere, Ada Lovelace).

### Build commands

```bash
cd shared/gpu_nam
mkdir -p build && cd build
cmake .. -DCMAKE_CUDA_COMPILER=/opt/cuda/bin/nvcc
make -j$(nproc)

# Run tests (requires a .nam model):
GPU_NAM_TEST_MODEL=../../../vendors/NeuralAmpModelerCore/example_models/wavenet.nam \
  ./gpu_nam_tests
```

## Supported Model Features

| Feature                  | Supported | Notes                          |
|--------------------------|-----------|--------------------------------|
| WaveNet architecture     | Yes       | Standard and A1/A2 variants    |
| Gated activation         | Yes       | tanh × sigmoid                 |
| Non-gated activation     | Yes       | tanh only                      |
| Layer 1×1 (residual)     | Yes       | With or without                |
| Head 1×1 (skip path)     | Yes       | With or without                |
| Multiple layer arrays    | Yes       | Up to 4                        |
| FiLM conditioning        | No        | Returns ModelNotSupported       |
| Condition DSP            | No        | Returns ModelNotSupported       |
| Grouped convolutions     | No        | Returns ModelNotSupported       |
| Blended gating           | No        | Returns ModelNotSupported       |
| LSTM / ConvNet models    | No        | WaveNet only                   |

## Phase 2 Roadmap (Batched Multi-Instance)

Phase 1 processes one instance per kernel launch. Phase 2 will:

1. **Batch kernel launch**: `gridDim.x = B` (one CUDA block per instance),
   processing all active instances in a single launch.
2. **Async double-buffering**: overlap CPU↔GPU transfers with kernel execution
   using CUDA streams and events.
3. **Shared weight reads**: all instances in a batch read the same weight array
   (already shared via `GpuNamModel`).

## Integration with The Stripper

The `NamDrive` DSP stage in The Stripper will:

1. Check `GpuNamManager::instance().isAvailable()`.
2. If GPU available: load model via manager, register instance, use
   `manager.process()` in the audio callback.
3. If GPU unavailable: fall back to `NeuralAmpModelerCore` CPU inference.
4. The fallback is transparent — same model file, same audio behaviour.
