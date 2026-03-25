/**
 * Fairy Tapes · gpu_nam — GpuNamModel implementation.
 *
 * Parses a .nam file (JSON + flat weights), extracts the WaveNet architecture
 * descriptor and weight-offset table, then uploads the linearised weight array
 * to GPU device memory.
 *
 * The weight layout on device is identical to the NAM file's flat weight order:
 *   for each layer_array:
 *     rechannel weights
 *     for each layer:
 *       conv1d weights  (iter: group→out→in→tap)  + bias
 *       mixin  weights  (iter: group→out→in)       + bias (if any)
 *       layer1x1 weights + bias (if present)
 *       head1x1  weights + bias (if present)
 *       [FiLM weights if present — currently unsupported on GPU]
 *     head_rechannel weights + bias
 *   head_scale (1 float)
 */

#include "gpu_nam_model.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

#if GPU_NAM_HAS_CUDA
#include <cuda_runtime.h>
#endif

// NAM headers for parsing the .nam config
#include "json.hpp"

namespace FairyTapes::GpuNam {

namespace {

// -----------------------------------------------------------------------
// Weight-counting helpers.
// They mirror the order in which NeuralAmpModelerCore's set_weights_ reads.
// -----------------------------------------------------------------------

/// Count weights consumed by a Conv1D(in, out, kernel, bias, groups).
int countConv1dWeights(int in_ch, int out_ch, int kernel_size, bool bias, int groups = 1) {
  int w = (out_ch / groups) * (in_ch / groups) * kernel_size;
  if (bias) w += out_ch;
  return w;
}

/// Count weights consumed by a Conv1x1(in, out, bias, groups).
int countConv1x1Weights(int in_ch, int out_ch, bool bias, int groups = 1) {
  return countConv1dWeights(in_ch, out_ch, /*kernel_size=*/1, bias, groups);
}

// -----------------------------------------------------------------------
// JSON parsing helpers — extract WavenetDesc + compute weight offsets.
// -----------------------------------------------------------------------

bool parseWavenetConfig(const nlohmann::json& config, double sampleRate,
                        WavenetDesc& desc, std::string& errorMsg) {
  if (config.find("layers") == config.end() || !config["layers"].is_array()) {
    errorMsg = "missing or invalid 'layers' array in WaveNet config";
    return false;
  }

  desc.num_layer_arrays = static_cast<int>(config["layers"].size());
  if (desc.num_layer_arrays < 1 || desc.num_layer_arrays > kMaxLayerArrays) {
    errorMsg = "unsupported number of layer arrays: " + std::to_string(desc.num_layer_arrays);
    return false;
  }

  desc.in_channels = config.value("in_channels", 1);
  desc.head_scale  = config["head_scale"].get<float>();
  desc.max_receptive_field = 0;

  int offset = 0; // running weight offset

  for (int a = 0; a < desc.num_layer_arrays; ++a) {
    const auto& lc = config["layers"][a];
    auto& ad = desc.arrays[a];
    auto& ao = desc.offsets[a];

    ad.input_size     = lc["input_size"].get<int>();
    ad.condition_size = lc["condition_size"].get<int>();
    ad.channels       = lc["channels"].get<int>();
    ad.head_size      = lc["head_size"].get<int>();
    ad.head_bias      = lc["head_bias"].get<bool>();

    int bottleneck = lc.value("bottleneck", ad.channels);

    // Check for FiLM (not supported on GPU yet)
    auto has_film = [&lc](const char* key) {
      return lc.find(key) != lc.end() && lc[key] != false;
    };
    if (has_film("conv_pre_film") || has_film("conv_post_film") ||
        has_film("input_mixin_pre_film") || has_film("input_mixin_post_film") ||
        has_film("activation_pre_film") || has_film("activation_post_film") ||
        has_film("layer1x1_post_film") || has_film("head1x1_post_film")) {
      errorMsg = "FiLM layers not supported on GPU; use CPU fallback";
      return false;
    }

    // Check for condition DSP (not supported on GPU yet)
    if (config.find("condition_dsp") != config.end() && !config["condition_dsp"].is_null()) {
      errorMsg = "condition_dsp not supported on GPU; use CPU fallback";
      return false;
    }

    // Rechannel: Conv1x1(input_size → channels, no bias)
    ao.rechannel_weight = offset;
    ao.rechannel_bias   = -1;
    offset += countConv1x1Weights(ad.input_size, ad.channels, /*bias=*/false);

    // Dilations
    const auto& dilations = lc["dilations"];
    ad.num_layers = static_cast<int>(dilations.size());
    if (ad.num_layers > kMaxLayersPerArray) {
      errorMsg = "too many layers (" + std::to_string(ad.num_layers) + ") in array " + std::to_string(a);
      return false;
    }

    // Kernel size (scalar or per-layer)
    std::vector<int> kernel_sizes;
    if (lc.find("kernel_sizes") != lc.end()) {
      for (const auto& ks : lc["kernel_sizes"]) kernel_sizes.push_back(ks.get<int>());
    } else {
      int ks = lc["kernel_size"].get<int>();
      kernel_sizes.resize(ad.num_layers, ks);
    }

    // Gating mode
    bool gated_global = false;
    if (lc.find("gated") != lc.end()) {
      gated_global = lc["gated"].get<bool>();
    } else if (lc.find("gating_mode") != lc.end()) {
      if (lc["gating_mode"].is_string()) {
        std::string gm = lc["gating_mode"].get<std::string>();
        gated_global = (gm == "gated");
        if (gm == "blended") {
          errorMsg = "blended gating not supported on GPU; use CPU fallback";
          return false;
        }
      } else if (lc["gating_mode"].is_array()) {
        errorMsg = "per-layer gating_mode arrays not supported on GPU; use CPU fallback";
        return false;
      }
    }

    // Layer1x1 params
    bool layer1x1_active = true;
    int  layer1x1_groups = 1;
    if (lc.find("layer1x1") != lc.end()) {
      layer1x1_active = lc["layer1x1"]["active"].get<bool>();
      layer1x1_groups = lc["layer1x1"]["groups"].get<int>();
    }

    // Head1x1 params
    bool head1x1_active    = false;
    int  head1x1_out       = bottleneck;
    int  head1x1_groups    = 1;
    if (lc.find("head1x1") != lc.end()) {
      head1x1_active    = lc["head1x1"]["active"].get<bool>();
      head1x1_out       = lc["head1x1"]["out_channels"].get<int>();
      head1x1_groups    = lc["head1x1"]["groups"].get<int>();
    }

    ad.head_output_size = head1x1_active ? head1x1_out : bottleneck;

    int groups_input = lc.value("groups_input", 1);
    int groups_mixin = lc.value("groups_input_mixin", 1);

    if (groups_input > 1 || groups_mixin > 1 || layer1x1_groups > 1 || head1x1_groups > 1) {
      errorMsg = "grouped convolutions (groups > 1) not yet supported on GPU";
      return false;
    }

    long array_receptive_field = 0;

    for (int li = 0; li < ad.num_layers; ++li) {
      auto& ld = ad.layers[li];
      auto& lo = ao.layers[li];

      ld.in_channels       = ad.channels;
      ld.bottleneck        = bottleneck;
      ld.kernel_size       = kernel_sizes[li];
      ld.dilation          = dilations[li].get<int>();
      ld.gated             = gated_global;
      ld.conv_out_channels = gated_global ? 2 * bottleneck : bottleneck;
      ld.has_bias_conv     = true; // Conv1D in NAM always has bias

      // Dilated Conv1D: channels → conv_out_channels, kernel=k, bias=true
      lo.conv_weight = offset;
      offset += (ld.conv_out_channels / groups_input) * (ad.channels / groups_input) * ld.kernel_size;
      lo.conv_bias = offset;
      offset += ld.conv_out_channels;

      // Input mixin: Conv1x1(condition_size → conv_out_channels, no bias)
      lo.mixin_weight = offset;
      offset += (ld.conv_out_channels / groups_mixin) * (ad.condition_size / groups_mixin);
      lo.mixin_bias = -1; // mixin has no bias

      // Layer 1×1
      ld.has_layer1x1 = layer1x1_active;
      if (layer1x1_active) {
        ld.has_bias_layer1x1 = true;
        lo.layer1x1_weight = offset;
        offset += bottleneck * ad.channels; // 1x1: bottleneck → channels
        lo.layer1x1_bias = offset;
        offset += ad.channels;
      } else {
        ld.has_bias_layer1x1 = false;
        lo.layer1x1_weight = -1;
        lo.layer1x1_bias   = -1;
      }

      // Head 1×1
      ld.has_head1x1 = head1x1_active;
      ld.head1x1_out = head1x1_out;
      if (head1x1_active) {
        ld.has_bias_head1x1 = true;
        lo.head1x1_weight = offset;
        offset += bottleneck * head1x1_out;
        lo.head1x1_bias = offset;
        offset += head1x1_out;
      } else {
        ld.has_bias_head1x1 = false;
        lo.head1x1_weight = -1;
        lo.head1x1_bias   = -1;
      }

      array_receptive_field += ld.dilation * (ld.kernel_size - 1);
    }

    // Head rechannel: Conv1x1(head_output_size → head_size, bias=head_bias)
    ao.head_rechannel_weight = offset;
    offset += ad.head_output_size * ad.head_size;
    if (ad.head_bias) {
      ao.head_rechannel_bias = offset;
      offset += ad.head_size;
    } else {
      ao.head_rechannel_bias = -1;
    }

    if (array_receptive_field > desc.max_receptive_field) {
      desc.max_receptive_field = static_cast<int>(array_receptive_field);
    }
  }

  // head_scale is the very last weight
  desc.head_scale_offset = offset;
  offset += 1;
  desc.total_weight_count = offset;

  // Output channels = head_size of last array
  desc.out_channels = desc.arrays[desc.num_layer_arrays - 1].head_size;

  return true;
}

} // anonymous namespace

// -----------------------------------------------------------------------
// GpuNamModel public interface
// -----------------------------------------------------------------------

GpuNamModel::GpuNamModel() = default;

GpuNamModel::~GpuNamModel() { unload(); }

GpuNamModel::GpuNamModel(GpuNamModel&& o) noexcept
  : desc_(o.desc_)
  , d_weights_(o.d_weights_)
  , h_weights_(std::move(o.h_weights_))
  , model_path_(std::move(o.model_path_))
  , expected_sample_rate_(o.expected_sample_rate_)
{
  o.d_weights_ = nullptr;
}

GpuNamModel& GpuNamModel::operator=(GpuNamModel&& o) noexcept {
  if (this != &o) {
    unload();
    desc_                 = o.desc_;
    d_weights_            = o.d_weights_;
    h_weights_            = std::move(o.h_weights_);
    model_path_           = std::move(o.model_path_);
    expected_sample_rate_ = o.expected_sample_rate_;
    o.d_weights_          = nullptr;
  }
  return *this;
}

GpuNamResult GpuNamModel::load(const std::filesystem::path& path) {
  unload();

  // ---- Read JSON ----
  if (!std::filesystem::exists(path)) {
    std::cerr << "[gpu_nam] file not found: " << path << "\n";
    return GpuNamResult::InvalidParameter;
  }

  nlohmann::json j;
  {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
      std::cerr << "[gpu_nam] cannot open: " << path << "\n";
      return GpuNamResult::InvalidParameter;
    }
    ifs >> j;
  }

  // ---- Architecture check ----
  std::string arch = j.value("architecture", "");
  if (arch != "WaveNet") {
    std::cerr << "[gpu_nam] unsupported architecture: " << arch << "\n";
    return GpuNamResult::ModelNotSupported;
  }

  // ---- Sample rate ----
  expected_sample_rate_ = j.contains("sample_rate") ? j["sample_rate"].get<double>() : -1.0;

  // ---- Parse config → WavenetDesc + weight offsets ----
  std::string error_msg;
  if (!parseWavenetConfig(j["config"], expected_sample_rate_, desc_, error_msg)) {
    std::cerr << "[gpu_nam] config not supported for GPU: " << error_msg << "\n";
    return GpuNamResult::ModelNotSupported;
  }

  // ---- Extract weights ----
  auto it = j.find("weights");
  if (it == j.end()) {
    std::cerr << "[gpu_nam] missing weights in .nam file\n";
    return GpuNamResult::InvalidParameter;
  }
  h_weights_ = it->get<std::vector<float>>();

  if (static_cast<int>(h_weights_.size()) != desc_.total_weight_count) {
    std::cerr << "[gpu_nam] weight count mismatch: file has " << h_weights_.size()
              << ", expected " << desc_.total_weight_count << "\n";
    return GpuNamResult::InvalidParameter;
  }

  // Read head_scale from the flat weights (last element)
  desc_.head_scale = h_weights_[desc_.head_scale_offset];

  model_path_ = path.string();

#if GPU_NAM_HAS_CUDA
  // ---- Upload to GPU ----
  size_t bytes = h_weights_.size() * sizeof(float);
  cudaError_t err = cudaMalloc(&d_weights_, bytes);
  if (err != cudaSuccess) {
    std::cerr << "[gpu_nam] cudaMalloc failed: " << cudaGetErrorString(err) << "\n";
    d_weights_ = nullptr;
    return GpuNamResult::CudaError;
  }

  err = cudaMemcpy(d_weights_, h_weights_.data(), bytes, cudaMemcpyHostToDevice);
  if (err != cudaSuccess) {
    std::cerr << "[gpu_nam] cudaMemcpy failed: " << cudaGetErrorString(err) << "\n";
    cudaFree(d_weights_);
    d_weights_ = nullptr;
    return GpuNamResult::CudaError;
  }

  std::cout << "[gpu_nam] loaded model '" << path.filename().string()
            << "' (" << h_weights_.size() << " weights, "
            << desc_.num_layer_arrays << " arrays, receptive_field="
            << desc_.max_receptive_field << ")\n";
#endif

  return GpuNamResult::Ok;
}

void GpuNamModel::unload() {
#if GPU_NAM_HAS_CUDA
  if (d_weights_) {
    cudaFree(d_weights_);
    d_weights_ = nullptr;
  }
#endif
  h_weights_.clear();
  desc_ = {};
  model_path_.clear();
  expected_sample_rate_ = -1.0;
}

} // namespace FairyTapes::GpuNam
