/**
 * Fairy Tapes · gpu_nam — GPU-side NAM model representation.
 *
 * GpuNamModel loads a .nam file, extracts the WaveNet architecture and weights,
 * linearises them into a single contiguous float array, and uploads to GPU
 * device memory. Multiple plugin instances sharing the same model can reference
 * the same GpuNamModel (weights are read-only once uploaded).
 *
 * This class is NOT thread-safe during construction/destruction, but the device
 * pointers it exposes are safe to read concurrently from CUDA kernels.
 */

#pragma once

#include "gpu_nam_common.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace FairyTapes::GpuNam {

class GpuNamModel {
public:
  GpuNamModel();
  ~GpuNamModel();

  GpuNamModel(const GpuNamModel&) = delete;
  GpuNamModel& operator=(const GpuNamModel&) = delete;
  GpuNamModel(GpuNamModel&&) noexcept;
  GpuNamModel& operator=(GpuNamModel&&) noexcept;

  /// Load a .nam model file.
  /// Parses the JSON config, extracts the WaveNet architecture descriptor,
  /// linearises all weights, and uploads them to GPU device memory.
  /// Returns Ok on success, ModelNotSupported if the model isn't a standard
  /// WaveNet, or CudaError on device allocation failure.
  GpuNamResult load(const std::filesystem::path& path);

  /// Free GPU resources and reset to unloaded state.
  void unload();

  bool isLoaded() const { return d_weights_ != nullptr; }

  /// Model architecture descriptor (CPU-side, read-only after load).
  const WavenetDesc& desc() const { return desc_; }

  /// Pointer to linearised weights in GPU device memory (read-only).
  const float* deviceWeights() const { return d_weights_; }

  /// Host-side copy of the linearised weights (useful for CPU fallback comparison).
  const std::vector<float>& hostWeights() const { return h_weights_; }

  /// Human-readable model path (for logging / debugging).
  const std::string& modelPath() const { return model_path_; }

  /// Expected sample rate from the .nam metadata (-1 if unknown).
  double expectedSampleRate() const { return expected_sample_rate_; }

private:
  WavenetDesc desc_{};
  float* d_weights_ = nullptr;       ///< Device pointer (cudaMalloc)
  std::vector<float> h_weights_;     ///< Host-side linearised weights
  std::string model_path_;
  double expected_sample_rate_ = -1.0;
};

} // namespace FairyTapes::GpuNam
