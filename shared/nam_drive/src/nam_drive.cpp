/**
 * Fairy Tapes · The Stripper — NAM Drive DSP implementation.
 *
 * Inference path priority:
 *   1. GPU (custom CUDA via shared/gpu_nam) — if available and model supported.
 *   2. CPU (NeuralAmpModelerCore) — automatic fallback.
 *
 * Stereo signal is converted to mono (mid = (L+R)/2), processed through the
 * NAM model (GPU or CPU), then auto-compensated via RMS tracking to maintain
 * perceived loudness. Always 100% wet.
 */

#include "nam_drive.h"

#ifndef NAM_SAMPLE_FLOAT
#define NAM_SAMPLE_FLOAT
#endif
#include "NAM/get_dsp.h"
#include "NAM/dsp.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

namespace FairyTapes::TheStripper::DSP {

static constexpr float kRmsTimeConstantSec = 0.1f;
static constexpr float kRmsFloor = 1e-6f;
static constexpr float kCompensationMinDb = -20.0f;
static constexpr float kCompensationMaxDb = 20.0f;

static float computeRmsAlpha(double sampleRate, int blockSize) {
  const double block_duration = static_cast<double>(blockSize) / sampleRate;
  return static_cast<float>(1.0 - std::exp(-block_duration / kRmsTimeConstantSec));
}

static float blockRms(const float* buf, int n) {
  if (n <= 0)
    return 0.0f;
  float sum_sq = 0.0f;
  for (int i = 0; i < n; ++i)
    sum_sq += buf[i] * buf[i];
  return std::sqrt(sum_sq / static_cast<float>(n));
}

NamDrive::NamDrive() = default;

NamDrive::~NamDrive() {
  unloadModel();
}

void NamDrive::setSampleRate(double sr) {
  if (sr <= 0.0)
    return;
  sample_rate_ = sr;
  if (cpu_model_)
    cpu_model_->Reset(sr, max_block_size_);
  // GPU engine sample rate is set at loadModel time; changing it requires
  // a full reload (handled by the host calling loadModel again).
}

void NamDrive::reset() {
  drive_ = 0.0f;
  input_rms_smooth_ = 0.0f;
  output_rms_smooth_ = 0.0f;
  mono_in_.clear();
  mono_out_.clear();

  if (gpu_active_) {
    auto& mgr = GpuNam::GpuNamManager::instance();
    mgr.resetInstance(gpu_model_handle_, gpu_instance_id_);
  } else if (cpu_model_) {
    cpu_model_->ResetAndPrewarm(sample_rate_, max_block_size_);
  }
}

void NamDrive::setDrive(float drive) {
  drive_ = std::clamp(drive, 0.0f, 1.0f);
}

bool NamDrive::loadModel(const std::filesystem::path& path) {
  unloadModel();

  // ---- Try GPU path first ----
  auto& mgr = GpuNam::GpuNamManager::instance();
  if (mgr.isAvailable()) {
    GpuNam::ModelHandle mh;
    auto r = mgr.loadModel(path, sample_rate_, max_block_size_, mh);
    if (r == GpuNam::GpuNamResult::Ok) {
      GpuNam::InstanceId iid;
      r = mgr.registerInstance(mh, iid);
      if (r == GpuNam::GpuNamResult::Ok) {
        gpu_model_handle_ = mh;
        gpu_instance_id_  = iid;
        gpu_active_       = true;
        input_rms_smooth_  = 0.0f;
        output_rms_smooth_ = 0.0f;
        std::cout << "[NamDrive] model loaded on GPU\n";
        return true;
      }
      mgr.unloadModel(mh);
    }
    // GPU load failed (unsupported model or CUDA error) — fall through to CPU
    std::cout << "[NamDrive] GPU load failed, falling back to CPU\n";
  }

  // ---- CPU fallback ----
  try {
    auto dsp = nam::get_dsp(path);
    if (!dsp)
      return false;
    dsp->Reset(sample_rate_, max_block_size_);
    dsp->prewarm();
    cpu_model_ = std::move(dsp);
    input_rms_smooth_  = 0.0f;
    output_rms_smooth_ = 0.0f;
    std::cout << "[NamDrive] model loaded on CPU\n";
    return true;
  } catch (...) {
    cpu_model_.reset();
    return false;
  }
}

void NamDrive::unloadModel() {
  if (gpu_active_) {
    auto& mgr = GpuNam::GpuNamManager::instance();
    mgr.unregisterInstance(gpu_model_handle_, gpu_instance_id_);
    mgr.unloadModel(gpu_model_handle_);
    gpu_model_handle_ = GpuNam::kInvalidModel;
    gpu_instance_id_  = GpuNam::kInvalidInstance;
    gpu_active_ = false;
  }
  cpu_model_.reset();
}

bool NamDrive::isModelLoaded() const {
  return gpu_active_ || cpu_model_ != nullptr;
}

double NamDrive::modelExpectedSampleRate() const {
  if (gpu_active_) {
    auto& mgr = GpuNam::GpuNamManager::instance();
    return mgr.modelSampleRate(gpu_model_handle_);
  }
  if (cpu_model_)
    return cpu_model_->GetExpectedSampleRate();
  return -1.0;
}

void NamDrive::process(float* left, float* right, int numSamples) {
  if (numSamples <= 0 || !isModelLoaded())
    return;

  const size_t n = static_cast<size_t>(numSamples);

  if (mono_in_.size() < n) {
    mono_in_.resize(n);
    mono_out_.resize(n);
  }

  const float drive_lin = static_cast<float>(std::pow(10.0f, drive_));

  for (size_t i = 0; i < n; ++i) {
    float s = (left[i] + right[i]) * 0.5f * drive_lin;
    mono_in_[i] = std::clamp(s, -10.0f, 10.0f);
  }

  const float in_rms = blockRms(mono_in_.data(), numSamples);

  // ---- Inference (GPU or CPU) ----
  if (gpu_active_) {
    auto& mgr = GpuNam::GpuNamManager::instance();
    auto r = mgr.process(gpu_model_handle_, gpu_instance_id_,
                         mono_in_.data(), mono_out_.data(), numSamples);
    if (r != GpuNam::GpuNamResult::Ok) {
      std::fill(mono_out_.begin(), mono_out_.begin() + n, 0.0f);
    }
  } else {
    NAM_SAMPLE* in_ptr  = mono_in_.data();
    NAM_SAMPLE* out_ptr = mono_out_.data();
    NAM_SAMPLE* in_ptrs[1]  = {in_ptr};
    NAM_SAMPLE* out_ptrs[1] = {out_ptr};
    cpu_model_->process(in_ptrs, out_ptrs, numSamples);
  }

  for (size_t i = 0; i < n; ++i) {
    float s = mono_out_[i];
    if (!std::isfinite(s))
      s = 0.0f;
    mono_out_[i] = std::clamp(s, -10.0f, 10.0f);
  }

  const float out_rms = blockRms(mono_out_.data(), numSamples);

  const float alpha = computeRmsAlpha(sample_rate_, numSamples);
  input_rms_smooth_  += alpha * (in_rms  - input_rms_smooth_);
  output_rms_smooth_ += alpha * (out_rms - output_rms_smooth_);

  float compensation = 1.0f;
  if (output_rms_smooth_ > kRmsFloor && input_rms_smooth_ > kRmsFloor) {
    compensation = input_rms_smooth_ / output_rms_smooth_;
    const float min_lin = std::pow(10.0f, kCompensationMinDb / 20.0f);
    const float max_lin = std::pow(10.0f, kCompensationMaxDb / 20.0f);
    compensation = std::clamp(compensation, min_lin, max_lin);
  }

  for (size_t i = 0; i < n; ++i) {
    const float processed = mono_out_[i] * compensation;
    left[i]  = processed;
    right[i] = processed;
  }
}

} // namespace FairyTapes::TheStripper::DSP
