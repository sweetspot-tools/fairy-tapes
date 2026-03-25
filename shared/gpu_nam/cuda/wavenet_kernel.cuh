/**
 * Fairy Tapes · gpu_nam — CUDA kernel declarations for WaveNet inference.
 *
 * The kernels are designed for **per-instance** execution: one CUDA block
 * processes one plugin instance's audio block through the full WaveNet.
 * Batching across instances is achieved by launching with gridDim.x = B
 * (one block per instance).
 *
 * Weight layout: a single flat float array, identical to the NAM file order.
 * The host-computed WavenetDesc + weight-offset tables tell each kernel
 * where to find the weights for each operation.
 *
 * Ring buffer strategy:
 *   Each layer's dilated Conv1D needs a history of up to D*(K-1) samples.
 *   A ring buffer of (channels × ring_capacity) floats is allocated per
 *   layer per instance. ring_capacity = max_dilation * (kernel_size - 1) +
 *   max_block_size, rounded up for alignment.
 *
 * All kernels process T samples (the audio block) sequentially through L
 * layers.  Within each layer, the T samples and C channels are parallelised
 * across CUDA threads within the block.
 */

#pragma once

#include "gpu_nam_common.h"

#if GPU_NAM_HAS_CUDA

#include <cuda_runtime.h>

namespace FairyTapes::GpuNam {

// -----------------------------------------------------------------------
// Ring-buffer geometry for a single layer-instance.
// -----------------------------------------------------------------------

/// Compute the ring buffer capacity (in frames) for a given layer.
__host__ __device__ inline int ringCapacity(int dilation, int kernel_size, int max_block) {
  return dilation * (kernel_size - 1) + max_block;
}

/// Total ring-buffer floats for one instance across all layers in the model.
int computeTotalRingSize(const WavenetDesc& desc, int maxBlock);

// -----------------------------------------------------------------------
// Kernel: WaveNet inference for one instance (Phase 1).
// -----------------------------------------------------------------------

/// Process an audio block through the WaveNet for a single instance.
///
/// @param d_weights       Flat model weights on device.
/// @param desc            Model architecture descriptor (device-accessible copy).
/// @param d_ring          Per-instance ring buffer block.
/// @param ring_write_pos  Current write position in each layer's ring (device int*).
/// @param d_in            Input audio (T floats, mono).
/// @param d_out           Output audio (T floats, mono).
/// @param num_samples     Block size T.
/// @param d_layer_io      Scratch space for intermediate activations.
/// @param d_head_accum    Scratch space for head accumulation.
///
/// Launch: 1 block, threads = min(channels, 32). The kernel internally
/// loops over samples and layers sequentially.
void launchWavenetInference(
    const float*       d_weights,
    const WavenetDesc& desc,
    float*             d_ring,
    int*               d_ring_pos,
    const float*       d_in,
    float*             d_out,
    int                num_samples,
    float*             d_layer_io,
    float*             d_head_accum,
    cudaStream_t       stream = 0);

} // namespace FairyTapes::GpuNam

#endif // GPU_NAM_HAS_CUDA
