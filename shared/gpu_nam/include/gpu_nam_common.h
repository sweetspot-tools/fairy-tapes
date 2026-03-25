/**
 * Fairy Tapes · gpu_nam — Common types and constants for GPU NAM inference.
 *
 * This header is safe to include from both CUDA (.cu) and plain C++ (.cpp) files.
 * All public types live in the FairyTapes::GpuNam namespace.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace FairyTapes::GpuNam {

/// Maximum number of layers in a single WaveNet layer array.
static constexpr int kMaxLayersPerArray = 30;

/// Maximum number of layer arrays in a WaveNet model.
static constexpr int kMaxLayerArrays = 4;

/// Maximum number of channels (in/out) per layer.
/// Covers all known NAM models (typically 8–32 channels).
static constexpr int kMaxChannels = 64;

/// Maximum dilated-conv kernel size.
static constexpr int kMaxKernelSize = 16;

/// Maximum number of plugin instances that can register with the GPU manager.
static constexpr int kMaxInstances = 256;

/// Maximum audio block size (in frames) the GPU engine will accept.
static constexpr int kMaxBlockSize = 8192;

// -----------------------------------------------------------------------
// Model topology descriptor (plain-old-data, no pointers)
// -----------------------------------------------------------------------

/// Per-layer metadata extracted from the .nam config.
struct LayerDesc {
  int in_channels;        ///< Input channels to this layer (= array channels)
  int bottleneck;         ///< Internal/bottleneck channels (often == in_channels)
  int conv_out_channels;  ///< Output channels of dilated conv (2*bottleneck if gated)
  int kernel_size;        ///< Dilated-conv kernel size (typically 3)
  int dilation;           ///< Dilation factor
  bool gated;             ///< True → conv_out = 2*bottleneck, gated activation
  bool has_layer1x1;      ///< True → 1×1 conv from bottleneck → in_channels (residual)
  bool has_head1x1;       ///< True → 1×1 conv from bottleneck → head_out_channels (skip)
  int head1x1_out;        ///< Output channels of head 1×1 (if active)
  bool has_bias_conv;     ///< Conv1D has bias
  bool has_bias_layer1x1; ///< Layer 1×1 has bias
  bool has_bias_head1x1;  ///< Head 1×1 has bias
};

/// Per-layer-array metadata.
struct LayerArrayDesc {
  int input_size;      ///< Input channels to rechannel (typically 1 for first array)
  int condition_size;  ///< Condition dimension (typically 1 or == condition_dsp output)
  int channels;        ///< Working channels across all layers
  int head_size;       ///< Final head output channels (typically 1)
  int num_layers;      ///< Number of layers in this array
  bool head_bias;      ///< Head rechannel has bias

  /// Head output size from each layer (= head1x1_out if head1x1 active, else bottleneck).
  int head_output_size;

  LayerDesc layers[kMaxLayersPerArray];
};

/// Weight byte-offsets (in floats, not bytes) into the flat weight array.
/// Computed at model load time so the CUDA kernel can index weights directly.
struct LayerWeightOffsets {
  int conv_weight;      ///< Dilated Conv1D weight start
  int conv_bias;        ///< Dilated Conv1D bias start (-1 = no bias)
  int mixin_weight;     ///< Input-mixin Conv1x1 weight start
  int mixin_bias;       ///< Input-mixin Conv1x1 bias start (-1 = no bias)
  int layer1x1_weight;  ///< Layer 1×1 weight start (-1 = not present)
  int layer1x1_bias;    ///< Layer 1×1 bias start (-1 = no bias)
  int head1x1_weight;   ///< Head 1×1 weight start (-1 = not present)
  int head1x1_bias;     ///< Head 1×1 bias start (-1 = no bias)
};

struct LayerArrayWeightOffsets {
  int rechannel_weight;      ///< Rechannel Conv1x1 weight start
  int rechannel_bias;        ///< -1 (no bias on rechannel)
  int head_rechannel_weight; ///< Head rechannel Conv1x1 weight start
  int head_rechannel_bias;   ///< Head rechannel bias start (-1 = no bias)
  LayerWeightOffsets layers[kMaxLayersPerArray];
};

/// Complete WaveNet model descriptor.
struct WavenetDesc {
  int in_channels;         ///< Model input channels (typically 1)
  int out_channels;        ///< Model output channels (typically 1)
  int num_layer_arrays;    ///< Number of layer arrays
  float head_scale;        ///< Output scaling factor
  int total_weight_count;  ///< Total number of float weights
  int max_receptive_field; ///< Largest receptive field across all arrays
  int head_scale_offset;   ///< Offset (in floats) to head_scale in flat weights

  LayerArrayDesc arrays[kMaxLayerArrays];
  LayerArrayWeightOffsets offsets[kMaxLayerArrays];
};

// -----------------------------------------------------------------------
// Result codes
// -----------------------------------------------------------------------

enum class GpuNamResult : int {
  Ok = 0,
  CudaNotAvailable,
  CudaError,
  ModelNotSupported,
  InvalidParameter,
  NotInitialized,
  AlreadyInitialized,
  InstanceLimitReached,
};

} // namespace FairyTapes::GpuNam
