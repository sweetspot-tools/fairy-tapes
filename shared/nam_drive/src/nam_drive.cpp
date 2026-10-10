/**
 * Fairy Tapes · shared — NAM Drive DSP implementation.
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

namespace FairyTapes::Shared::DSP {

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

struct NamDrive::Loaded {
  std::unique_ptr<nam::DSP> cpu;
  bool gpu = false;
  GpuNam::ModelHandle gpu_model = GpuNam::kInvalidModel;
  GpuNam::InstanceId gpu_instance = GpuNam::kInvalidInstance;
  double expected_sr = -1.0;
  bool valid() const { return gpu || cpu != nullptr; }
  ~Loaded() {
    if (gpu) {
      auto& mgr = GpuNam::GpuNamManager::instance();
      mgr.unregisterInstance(gpu_model, gpu_instance);
      mgr.unloadModel(gpu_model);
    }
  }
};

NamDrive::NamDrive() = default;

NamDrive::~NamDrive() {
  {
    std::lock_guard<std::mutex> lk(req_mutex_);
    stop_ = true;
  }
  req_cv_.notify_all();
  if (worker_.joinable())
    worker_.join();
  delete active_.exchange(nullptr);
  delete pending_.exchange(nullptr);
  collectGarbage();
}

void NamDrive::setSampleRate(double sr) {
  if (sr <= 0.0)
    return;
  sample_rate_ = sr;
  Loaded* m = active_.load(std::memory_order_acquire);
  if (m && m->cpu)
    m->cpu->Reset(sr, max_block_size_);
  // GPU engine sample rate is set at load time; changing it requires
  // a full reload (handled by the host calling loadModel/requestLoad again).
}

void NamDrive::reset() {
  drive_ = 0.0f;
  input_rms_smooth_ = 0.0f;
  output_rms_smooth_ = 0.0f;
  mono_in_.clear();
  mono_out_.clear();

  Loaded* m = active_.load(std::memory_order_acquire);
  if (!m)
    return;
  if (m->gpu) {
    auto& mgr = GpuNam::GpuNamManager::instance();
    mgr.resetInstance(m->gpu_model, m->gpu_instance);
  } else if (m->cpu) {
    m->cpu->ResetAndPrewarm(sample_rate_, max_block_size_);
  }
}

void NamDrive::setDrive(float drive) {
  drive_ = std::clamp(drive, 0.0f, 1.0f);
}

/// Builds a model for inference (GPU first, CPU fallback). Runs on a worker or message thread, never on the audio thread.
std::unique_ptr<NamDrive::Loaded> NamDrive::build(const std::filesystem::path& path, double sr, int blockSize, std::string& error) {
  error.clear();
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    error = "File not found";
    return nullptr;
  }

  // ---- Try GPU path first ----
  auto& mgr = GpuNam::GpuNamManager::instance();
  if (mgr.isAvailable()) {
    GpuNam::ModelHandle mh;
    auto r = mgr.loadModel(path, sr, blockSize, mh);
    if (r == GpuNam::GpuNamResult::Ok) {
      GpuNam::InstanceId iid;
      r = mgr.registerInstance(mh, iid);
      if (r == GpuNam::GpuNamResult::Ok) {
        auto m = std::make_unique<Loaded>();
        m->gpu = true;
        m->gpu_model = mh;
        m->gpu_instance = iid;
        m->expected_sr = mgr.modelSampleRate(mh);
        std::cout << "[NamDrive] model loaded on GPU\n";
        return m;
      }
      mgr.unloadModel(mh);
    }
    // GPU load failed (unsupported model or CUDA error) — fall through to CPU
    std::cout << "[NamDrive] GPU load failed, falling back to CPU\n";
  }

  // ---- CPU fallback ----
  try {
    auto dsp = nam::get_dsp(path);
    if (!dsp) {
      error = "Unsupported model";
      return nullptr;
    }
    dsp->Reset(sr, blockSize);
    dsp->prewarm();
    auto m = std::make_unique<Loaded>();
    m->expected_sr = dsp->GetExpectedSampleRate();
    m->cpu = std::move(dsp);
    std::cout << "[NamDrive] model loaded on CPU\n";
    return m;
  } catch (const std::exception& e) {
    error = e.what();
    if (error.empty())
      error = "Cannot read model";
    return nullptr;
  } catch (...) {
    error = "Cannot read model";
    return nullptr;
  }
}

void NamDrive::setInfo(LoadState st, const std::string& name, const std::string& err) {
  model_name_ = name;
  last_error_ = err;
  state_.store(static_cast<int>(st), std::memory_order_release);
}

std::string NamDrive::loadedModelName() const {
  std::lock_guard<std::mutex> lk(info_mutex_);
  return model_name_;
}

std::string NamDrive::lastError() const {
  std::lock_guard<std::mutex> lk(info_mutex_);
  return last_error_;
}

void NamDrive::collectGarbage() {
  for (auto& slot : graveyard_)
    delete slot.exchange(nullptr, std::memory_order_acq_rel);
}

bool NamDrive::loadModel(const std::filesystem::path& path) {
  std::string err;
  auto m = build(path, sample_rate_, max_block_size_, err);
  std::lock_guard<std::mutex> lk(info_mutex_);
  ++generation_;  // cancels any request still in flight
  delete pending_.exchange(nullptr);
  delete active_.exchange(nullptr);
  collectGarbage();
  input_rms_smooth_ = 0.0f;
  output_rms_smooth_ = 0.0f;
  if (!m) {
    setInfo(LoadState::Failed, "", err);
    return false;
  }
  active_.store(m.release(), std::memory_order_release);
  setInfo(LoadState::Loaded, path.filename().string(), "");
  return true;
}

void NamDrive::unloadModel() {
  std::lock_guard<std::mutex> lk(info_mutex_);
  ++generation_;
  delete pending_.exchange(nullptr);
  delete active_.exchange(nullptr);
  collectGarbage();
  setInfo(LoadState::Idle, "", "");
}

void NamDrive::requestLoad(const std::filesystem::path& path) {
  collectGarbage();
  {
    std::lock_guard<std::mutex> rl(req_mutex_);
    {
      std::lock_guard<std::mutex> il(info_mutex_);
      ++generation_;
      setInfo(LoadState::Loading, model_name_, "");
    }
    req_path_ = path;
    req_sr_ = sample_rate_;
    req_block_ = max_block_size_;
    {
      std::lock_guard<std::mutex> il(info_mutex_);
      req_gen_ = generation_;
    }
    req_pending_ = true;
    if (!worker_.joinable())
      worker_ = std::thread([this] { workerLoop(); });
  }
  req_cv_.notify_one();
}

void NamDrive::workerLoop() {
  std::unique_lock<std::mutex> lk(req_mutex_);
  for (;;) {
    req_cv_.wait(lk, [this] { return stop_ || req_pending_; });
    if (stop_)
      break;
    const std::filesystem::path path = req_path_;
    const double sr = req_sr_;
    const int block = req_block_;
    const std::uint64_t gen = req_gen_;
    req_pending_ = false;
    lk.unlock();

    std::string err;
    auto m = build(path, sr, block, err);

    {
      std::lock_guard<std::mutex> il(info_mutex_);
      if (generation_ == gen) {
        // Publish: an empty model means "unload" (a failed load leaves the Drive in passthrough).
        Loaded* next = m ? m.release() : new Loaded();
        delete pending_.exchange(next, std::memory_order_acq_rel);
        if (next->valid())
          setInfo(LoadState::Loaded, path.filename().string(), "");
        else
          setInfo(LoadState::Failed, "", err);
      }
      // else: a newer request superseded this one; the result is dropped (m is freed here)
    }
    collectGarbage();
    lk.lock();
  }
}

/// Audio thread: adopt a model the worker has finished. Never allocates, locks or waits;
/// the replaced model (and the empty "unload" marker) is parked in graveyard slots and freed by a non-audio thread.
/// Only this thread writes into free slots, so a slot seen free stays free until we use it.
void NamDrive::adoptPending() {
  Loaded* cand = pending_.load(std::memory_order_acquire);
  if (!cand)
    return;
  int free_slot[2];
  int n = 0;
  for (int i = 0; i < kGraveyard && n < 2; ++i)
    if (graveyard_[i].load(std::memory_order_relaxed) == nullptr)
      free_slot[n++] = i;
  if (n < 2)
    return;  // the worker has not collected yet: adopt in a later block
  if (!pending_.compare_exchange_strong(cand, nullptr, std::memory_order_acq_rel))
    return;  // replaced meanwhile: try again next block
  Loaded* old = active_.exchange(cand->valid() ? cand : nullptr, std::memory_order_acq_rel);
  int k = 0;
  if (!cand->valid())
    graveyard_[free_slot[k++]].store(cand, std::memory_order_release);
  if (old)
    graveyard_[free_slot[k++]].store(old, std::memory_order_release);
  input_rms_smooth_ = 0.0f;
  output_rms_smooth_ = 0.0f;
}

bool NamDrive::isModelLoaded() const {
  return active_.load(std::memory_order_acquire) != nullptr;
}

bool NamDrive::isGpuActive() const {
  Loaded* m = active_.load(std::memory_order_acquire);
  return m && m->gpu;
}

double NamDrive::modelExpectedSampleRate() const {
  Loaded* m = active_.load(std::memory_order_acquire);
  return m ? m->expected_sr : -1.0;
}

void NamDrive::process(float* left, float* right, int numSamples) {
  adoptPending();
  Loaded* model = active_.load(std::memory_order_acquire);
  if (numSamples <= 0 || !model)
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
  if (model->gpu) {
    auto& mgr = GpuNam::GpuNamManager::instance();
    auto r = mgr.process(model->gpu_model, model->gpu_instance,
                         mono_in_.data(), mono_out_.data(), numSamples);
    if (r != GpuNam::GpuNamResult::Ok) {
      std::fill(mono_out_.begin(), mono_out_.begin() + n, 0.0f);
    }
  } else {
    NAM_SAMPLE* in_ptr  = mono_in_.data();
    NAM_SAMPLE* out_ptr = mono_out_.data();
    NAM_SAMPLE* in_ptrs[1]  = {in_ptr};
    NAM_SAMPLE* out_ptrs[1] = {out_ptr};
    model->cpu->process(in_ptrs, out_ptrs, numSamples);
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

} // namespace FairyTapes::Shared::DSP
