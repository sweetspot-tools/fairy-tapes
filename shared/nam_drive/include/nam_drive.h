/**
 * Fairy Tapes · shared — NAM Drive DSP wrapper.
 *
 * Single-knob drive: the Drive parameter controls how hard the signal is
 * pushed into the NAM model (0 = unity / coloration only, 1 = +20 dB).
 * Output level is auto-compensated via smoothed RMS tracking so that
 * increasing Drive changes timbre without changing perceived loudness.
 * Always 100% wet when active; bypass toggle (kBypassDrive) turns it off.
 *
 * Inference path: GPU (custom CUDA via gpu_nam) with automatic CPU fallback
 * (NeuralAmpModelerCore). The fallback is transparent to the caller.
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "gpu_nam_manager.h"

namespace nam {
class DSP;
}

namespace FairyTapes::Shared::DSP {

class NamDrive {
public:
  NamDrive();
  ~NamDrive();

  NamDrive(const NamDrive&) = delete;
  NamDrive& operator=(const NamDrive&) = delete;

  void setSampleRate(double sr);
  void reset();

  /// Set drive amount (0..1 normalized).
  /// 0 = unity gain into model (natural coloration), 1 = +20 dB (heavy saturation).
  void setDrive(float drive);

  /// Load a .nam model file. Returns true on success.
  /// Attempts GPU loading first; falls back to CPU if GPU is unavailable
  /// or the model is unsupported on GPU.
  /// Must NOT be called from the audio thread.
  bool loadModel(const std::filesystem::path& path);

  /// Unload the current model (passthrough mode).
  void unloadModel();

  bool isModelLoaded() const;

  /// True if inference is running on GPU, false if CPU fallback.
  bool isGpuActive() const { return gpu_active_; }

  /// Expected sample rate of the loaded model (-1 if unknown).
  double modelExpectedSampleRate() const;

  /// Process stereo audio in-place.
  /// If no model is loaded, the signal passes through unmodified.
  /// Output is auto-compensated to match input loudness.
  void process(float* left, float* right, int numSamples);

private:
  // CPU fallback path (NeuralAmpModelerCore)
  std::unique_ptr<nam::DSP> cpu_model_;

  // GPU path (gpu_nam module)
  bool gpu_active_ = false;
  GpuNam::ModelHandle gpu_model_handle_ = GpuNam::kInvalidModel;
  GpuNam::InstanceId  gpu_instance_id_  = GpuNam::kInvalidInstance;

  double sample_rate_ = 48000.0;
  int max_block_size_ = 8192;
  float drive_ = 0.0f;

  float input_rms_smooth_ = 0.0f;
  float output_rms_smooth_ = 0.0f;

  std::vector<float> mono_in_;
  std::vector<float> mono_out_;
};

} // namespace FairyTapes::Shared::DSP
