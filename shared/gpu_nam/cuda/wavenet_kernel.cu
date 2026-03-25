/**
 * Fairy Tapes · gpu_nam — CUDA WaveNet inference kernel.
 *
 * Architecture overview (per audio block of T samples):
 *
 *   for each layer_array a:
 *     rechannel (1×1 conv)  →  layer_input[channels × T]
 *     for each layer l in a:
 *       dilated_conv(layer_input, ring_buf[a][l]) → z[conv_out × T]
 *       mixin(condition)                          → z += mixin_out
 *       gated_activation(z)                       → act[bottleneck × T]
 *       layer1x1(act)                             → residual
 *       layer_input += residual
 *       head_accum  += head1x1(act) or act
 *     head_rechannel(head_accum) → output
 *     output *= head_scale
 *
 * Thread model (Phase 1 — single instance):
 *   1 CUDA block with `channels` threads. Each thread owns one output channel
 *   for matmul accumulation. The kernel loops over samples sequentially
 *   (WaveNet is causal), and over layers sequentially (each depends on prior).
 *
 * Weight access: all weights live in global memory (L2-cached); for small
 * models they fit entirely in L2. Weight matrices are column-major (matching
 * Eigen's default and the NAM file order after reinterpretation).
 *
 * NAM weight order for Conv1D(in_ch, out_ch, kernel_size, groups=1):
 *   for i in 0..out_ch:
 *     for j in 0..in_ch:
 *       for k in 0..kernel_size:
 *         w[i][j][k]
 *   then bias[0..out_ch]
 *
 * To access weight(out_o, in_i, tap_k) we index:
 *   offset + o * in_ch * kernel_size + i * kernel_size + k
 */

#include "wavenet_kernel.cuh"

#if GPU_NAM_HAS_CUDA

#include <cmath>
#include <cstdio>
#include <cuda_runtime.h>

namespace FairyTapes::GpuNam {

// -----------------------------------------------------------------------
// Helper: compute total ring buffer size for one instance.
// -----------------------------------------------------------------------

int computeTotalRingSize(const WavenetDesc& desc, int maxBlock) {
  int total = 0;
  for (int a = 0; a < desc.num_layer_arrays; ++a) {
    const auto& ad = desc.arrays[a];
    for (int l = 0; l < ad.num_layers; ++l) {
      int cap = ringCapacity(ad.layers[l].dilation, ad.layers[l].kernel_size, maxBlock);
      total += ad.channels * cap; // ring stores `channels` per frame
    }
  }
  return total;
}

// -----------------------------------------------------------------------
// Device helpers
// -----------------------------------------------------------------------

/// 1×1 convolution: out[o] = sum_i( W[o*in + i] * x[i] )  + bias[o]
/// Each thread computes one output channel.
__device__ void conv1x1(const float* __restrict__ W,
                        const float* bias, // nullptr if no bias
                        int in_ch, int out_ch,
                        const float* __restrict__ x,
                        float* __restrict__ y,
                        int tid) {
  if (tid >= out_ch) return;
  float acc = 0.0f;
  for (int i = 0; i < in_ch; ++i) {
    acc += W[tid * in_ch + i] * x[i];
  }
  if (bias) acc += bias[tid];
  y[tid] = acc;
}

/// Dilated Conv1D for a single output sample.
/// ring[channels × cap] stores the input history (circular).
/// The newest sample is at ring[:, (write_pos - 1) % cap].
///
/// NAM weight layout: w[out_o * in_ch * K + in_i * K + tap_k]
/// For dilation D and kernel size K, the taps look back at
///   t - D*(K - 1 - k)  for k=0..K-1.
__device__ void dilatedConv1d(const float* __restrict__ W,
                              const float* __restrict__ bias,
                              int in_ch, int out_ch, int K, int D,
                              const float* __restrict__ ring,
                              int ring_cap, int write_pos,
                              float* __restrict__ y,
                              int tid) {
  if (tid >= out_ch) return;
  float acc = 0.0f;
  for (int k = 0; k < K; ++k) {
    int lookback = D * (K - 1 - k);
    int pos = ((write_pos - 1 - lookback) % ring_cap + ring_cap) % ring_cap;
    for (int i = 0; i < in_ch; ++i) {
      acc += W[tid * in_ch * K + i * K + k] * ring[pos * in_ch + i];
    }
  }
  if (bias) acc += bias[tid];
  y[tid] = acc;
}

// -----------------------------------------------------------------------
// Main WaveNet inference kernel (single instance, Phase 1).
// -----------------------------------------------------------------------

/// Shared memory layout (dynamically sized):
///   float smem[0 .. max_ch-1]            — layer_input   (channels)
///   float smem[max_ch .. 2*max_ch-1]     — conv_z        (conv_out_channels)
///   float smem[2*max_ch .. 3*max_ch-1]   — activation    (bottleneck)
///   float smem[3*max_ch .. 4*max_ch-1]   — residual / scratch
///   float smem[4*max_ch .. 5*max_ch-1]   — head_accum    (head_output_size)
///   float smem[5*max_ch .. 6*max_ch-1]   — condition      (condition_size)
///   float smem[6*max_ch .. 7*max_ch-1]   — rechannel out  (channels)

__global__ void wavenetInferenceKernel(
    const float* __restrict__ d_weights,
    const WavenetDesc desc,
    float*       d_ring,
    int*         d_ring_pos,
    const float* __restrict__ d_in,
    float*       d_out,
    int          num_samples,
    float*       d_layer_io,   // unused in Phase 1 (all in smem)
    float*       d_head_accum) // unused in Phase 1 (all in smem)
{
  extern __shared__ float smem[];

  const int tid = threadIdx.x;
  const int max_ch = kMaxChannels;

  float* s_layer_input = smem;
  float* s_z           = smem + max_ch;
  float* s_act         = smem + 2 * max_ch;
  float* s_scratch     = smem + 3 * max_ch;
  float* s_head        = smem + 4 * max_ch;
  float* s_cond        = smem + 5 * max_ch;
  float* s_rechannel   = smem + 6 * max_ch;

  // For each sample in the block
  for (int t = 0; t < num_samples; ++t) {
    // The input sample (mono) — same as condition for standard WaveNet.
    float in_sample = d_in[t];

    // Reset ring tracking per-sample so we walk layers deterministically.
    int cur_ring_offset = 0;
    int cur_ring_pos_idx = 0;

    // Process layer arrays
    for (int a = 0; a < desc.num_layer_arrays; ++a) {
      const auto& ad = desc.arrays[a];
      const auto& ao = desc.offsets[a];

      // ---- Condition: for standard WaveNet, condition = input ----
      if (tid < ad.condition_size) {
        s_cond[tid] = in_sample; // mono: condition_size is typically 1
      }
      __syncthreads();

      // ---- Rechannel: 1×1 conv (input_size → channels) ----
      // Input to first array = raw input; subsequent arrays = previous head output.
      if (a == 0) {
        if (tid < ad.input_size) {
          s_layer_input[tid] = in_sample;
        }
      }
      // For subsequent arrays, s_layer_input already contains previous
      // array's last layer output — set below after head rechannel.
      __syncthreads();

      conv1x1(d_weights + ao.rechannel_weight, nullptr,
              ad.input_size, ad.channels,
              s_layer_input, s_rechannel, tid);
      __syncthreads();

      // Copy rechannel output to layer_input
      if (tid < ad.channels) {
        s_layer_input[tid] = s_rechannel[tid];
      }
      __syncthreads();

      // ---- Zero head accumulator ----
      if (a == 0) {
        if (tid < ad.head_output_size) {
          s_head[tid] = 0.0f;
        }
      }
      // For subsequent arrays, s_head contains previous array's head output
      // (already accumulated from the previous array's head_rechannel step).
      __syncthreads();

      // ---- Process layers ----
      for (int l = 0; l < ad.num_layers; ++l) {
        const auto& ld = ad.layers[l];
        const auto& lo = ao.layers[l];

        int ring_cap = ringCapacity(ld.dilation, ld.kernel_size, 1);
        // Actually, ring_cap needs to hold enough history.
        // We use the formula: D * (K-1) + 1 (minimum to hold the receptive field + 1 new sample)
        ring_cap = ld.dilation * (ld.kernel_size - 1) + 1;

        int ring_size = ad.channels * ring_cap;
        float* layer_ring = d_ring + cur_ring_offset;

        int write_pos = d_ring_pos[cur_ring_pos_idx];

        // Write current layer input into ring buffer at write_pos
        if (tid < ad.channels) {
          layer_ring[write_pos * ad.channels + tid] = s_layer_input[tid];
        }
        __syncthreads();

        // Advance write position (for the read that follows)
        int new_write_pos = (write_pos + 1) % ring_cap;

        // ---- Dilated Conv1D ----
        dilatedConv1d(d_weights + lo.conv_weight,
                      d_weights + lo.conv_bias,
                      ad.channels, ld.conv_out_channels,
                      ld.kernel_size, ld.dilation,
                      layer_ring, ring_cap,
                      new_write_pos, // read relative to just-advanced pos
                      s_z, tid);
        __syncthreads();

        // ---- Input mixin: Conv1x1(condition → conv_out_channels) ----
        if (tid < ld.conv_out_channels) {
          float mixin_val = 0.0f;
          const float* mixin_w = d_weights + lo.mixin_weight;
          for (int i = 0; i < ad.condition_size; ++i) {
            mixin_val += mixin_w[tid * ad.condition_size + i] * s_cond[i];
          }
          s_z[tid] += mixin_val;
        }
        __syncthreads();

        // ---- Gated activation: tanh(top) × sigmoid(bottom) ----
        if (ld.gated) {
          if (tid < ld.bottleneck) {
            float top = tanhf(s_z[tid]);
            float bot = 1.0f / (1.0f + expf(-s_z[ld.bottleneck + tid]));
            s_act[tid] = top * bot;
          }
        } else {
          if (tid < ld.bottleneck) {
            s_act[tid] = tanhf(s_z[tid]);
          }
        }
        __syncthreads();

        // ---- Layer 1×1 (residual path): bottleneck → channels ----
        if (ld.has_layer1x1) {
          conv1x1(d_weights + lo.layer1x1_weight,
                  (lo.layer1x1_bias >= 0) ? d_weights + lo.layer1x1_bias : nullptr,
                  ld.bottleneck, ad.channels,
                  s_act, s_scratch, tid);
          __syncthreads();

          // Residual: layer_input += layer1x1_output
          if (tid < ad.channels) {
            s_layer_input[tid] += s_scratch[tid];
          }
        } else {
          // No layer1x1: residual = input (identity) — already in s_layer_input
          // (bottleneck must == channels when layer1x1 is inactive)
        }
        __syncthreads();

        // ---- Head 1×1 (skip path): bottleneck → head_output_size ----
        if (ld.has_head1x1) {
          conv1x1(d_weights + lo.head1x1_weight,
                  (lo.head1x1_bias >= 0) ? d_weights + lo.head1x1_bias : nullptr,
                  ld.bottleneck, ld.head1x1_out,
                  s_act, s_scratch, tid);
          __syncthreads();

          if (tid < ad.head_output_size) {
            s_head[tid] += s_scratch[tid];
          }
        } else {
          // No head1x1: accumulate activation directly
          if (tid < ld.bottleneck) {
            s_head[tid] += s_act[tid];
          }
        }
        __syncthreads();

        // Update ring buffer write position for this layer
        if (tid == 0) {
          d_ring_pos[cur_ring_pos_idx] = new_write_pos;
        }

        cur_ring_offset   += ring_size;
        cur_ring_pos_idx  += 1;
      }

      // ---- Head rechannel: Conv1x1(head_output_size → head_size, bias) ----
      conv1x1(d_weights + ao.head_rechannel_weight,
              (ao.head_rechannel_bias >= 0) ? d_weights + ao.head_rechannel_bias : nullptr,
              ad.head_output_size, ad.head_size,
              s_head, s_scratch, tid);
      __syncthreads();

      // For subsequent layer arrays, the "layer input" is the last layer's
      // output, and the "head input" is the head_rechannel output.
      // We set s_layer_input = last layer output (already there),
      // and s_head = head_rechannel output.
      if (tid < ad.head_size) {
        s_head[tid] = s_scratch[tid];
      }

      // Prepare s_layer_input for next array (= last layer output of this array)
      // Already done via the residual updates above.
      __syncthreads();
    }

    // ---- Output: head_scale × final head ----
    if (tid == 0) {
      d_out[t] = desc.head_scale * s_head[0];
    }
    __syncthreads();
  }
}

// -----------------------------------------------------------------------
// Host-side launch wrapper
// -----------------------------------------------------------------------

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
    cudaStream_t       stream)
{
  // Determine the number of threads: must cover the widest channel count.
  int max_ch = 1;
  for (int a = 0; a < desc.num_layer_arrays; ++a) {
    const auto& ad = desc.arrays[a];
    if (ad.channels > max_ch) max_ch = ad.channels;
    for (int l = 0; l < ad.num_layers; ++l) {
      if (ad.layers[l].conv_out_channels > max_ch) max_ch = ad.layers[l].conv_out_channels;
    }
  }
  // Round up to next warp size (32) for efficiency
  int threads = ((max_ch + 31) / 32) * 32;
  if (threads < 32) threads = 32;

  // Shared memory: 7 × kMaxChannels floats
  size_t smem_bytes = 7 * kMaxChannels * sizeof(float);

  wavenetInferenceKernel<<<1, threads, smem_bytes, stream>>>(
      d_weights, desc, d_ring, d_ring_pos,
      d_in, d_out, num_samples,
      d_layer_io, d_head_accum);
}

} // namespace FairyTapes::GpuNam

#endif // GPU_NAM_HAS_CUDA
