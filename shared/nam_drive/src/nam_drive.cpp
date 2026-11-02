/**
 * Fairy Tapes · shared — NAM Drive DSP implementation.
 *
 * Inference path priority:
 *   1. GPU (custom CUDA via shared/gpu_nam) — if available and model supported.
 *   2. CPU (NeuralAmpModelerCore) — automatic fallback.
 *
 * Stereo (2026-11-02): dual mono. Two instances of the model, one per channel (before: L+R summed to mono and the
 * same signal written to both sides). Bit-identical channels run the L instance only and copy its output; the R
 * instance is kept in step as described in nam_drive.h (process). The output is auto-compensated via RMS tracking,
 * linked over both channels (for a mono source exactly the old behaviour). Always 100% wet.
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
// The drive gain alone is up to +20 dB, and a model can add its own gain on top: leave room to bring it all back.
static constexpr float kCompensationMinDb = -40.0f;
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
  std::unique_ptr<nam::DSP> cpu[2];        // L and R instances (CPU path)
  bool gpu = false;
  GpuNam::ModelHandle gpu_model = GpuNam::kInvalidModel;   // one model on the GPU (loaded once) ...
  GpuNam::InstanceId gpu_instance[2] = {GpuNam::kInvalidInstance, GpuNam::kInvalidInstance};   // ... two instance states
  double expected_sr = -1.0;
  double built_sr = 0.0;                   // rate the instances were reset at
  int catch_up = NamDrive::kHistory;       // samples a skipped R instance replays to match L again (the model's memory)
  bool valid() const { return gpu || cpu[0] != nullptr; }
  ~Loaded() {
    if (gpu) {
      auto& mgr = GpuNam::GpuNamManager::instance();
      for (auto id : gpu_instance)
        if (id != GpuNam::kInvalidInstance)
          mgr.unregisterInstance(gpu_model, id);
      mgr.unloadModel(gpu_model);
    }
  }
};

/// File names are reported as UTF-8 on every platform (a plain .string() would be the ANSI code page on Windows).
static std::string toUtf8(const std::filesystem::path& p) {
  const auto u = p.u8string();
  return std::string(u.begin(), u.end());
}

NamDrive::NamDrive() {
  for (int c = 0; c < 2; ++c) {
    in_[c].assign(kMaxBlock, 0.0f);
    out_[c].assign(kMaxBlock, 0.0f);
  }
  hist_.assign(kHistory, 0.0f);
  catch_in_.assign(kHistory, 0.0f);
  catch_out_.assign(kHistory, 0.0f);
}

void NamDrive::resyncChannels() {
  r_skipping_ = true;   // both instances hold the same state: R can wait until the channels differ
  identical_run_ = 0;
  r_lag_ = 0;
}

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
  // The processor calls this every block: only a real change of rate resets the model. (Until 2026-11-02 every call
  // ran nam::DSP::Reset, which wipes the model's history and prewarms: a discontinuity and a large prewarm every block.)
  Loaded* m = active_.load(std::memory_order_acquire);
  if (!m || m->built_sr == sr)
    return;
  m->built_sr = sr;
  if (!m->gpu) {
    for (auto& c : m->cpu)
      if (c)
        c->Reset(sr, max_block_size_);
    resyncChannels();
  }
  // GPU engine sample rate is set at load time; changing it requires
  // a full reload (handled by the host calling loadModel/requestLoad again).
}

void NamDrive::reset() {
  drive_ = 0.0f;
  input_rms_smooth_ = 0.0f;
  output_rms_smooth_ = 0.0f;

  Loaded* m = active_.load(std::memory_order_acquire);
  if (!m)
    return;
  if (m->gpu) {
    auto& mgr = GpuNam::GpuNamManager::instance();
    for (auto id : m->gpu_instance)
      mgr.resetInstance(m->gpu_model, id);
  } else {
    for (auto& c : m->cpu)
      if (c)
        c->ResetAndPrewarm(sample_rate_, max_block_size_);
  }
  resyncChannels();
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
  // One model in the manager (loaded once), two registered instances: each instance has its own state on the GPU.
  // If the second instance cannot be registered, both channels go to the CPU (never L on the GPU and R on the CPU).
  auto& mgr = GpuNam::GpuNamManager::instance();
  if (mgr.isAvailable()) {
    GpuNam::ModelHandle mh;
    auto r = mgr.loadModel(path, sr, blockSize, mh);
    if (r == GpuNam::GpuNamResult::Ok) {
      GpuNam::InstanceId iid[2] = {GpuNam::kInvalidInstance, GpuNam::kInvalidInstance};
      r = mgr.registerInstance(mh, iid[0]);
      if (r == GpuNam::GpuNamResult::Ok) {
        r = mgr.registerInstance(mh, iid[1]);
        if (r == GpuNam::GpuNamResult::Ok) {
          auto m = std::make_unique<Loaded>();
          m->gpu = true;
          m->gpu_model = mh;
          m->gpu_instance[0] = iid[0];
          m->gpu_instance[1] = iid[1];
          m->expected_sr = mgr.modelSampleRate(mh);
          m->built_sr = sr;
          m->catch_up = kHistory;   // the GPU engine does not report the model's memory: replay all we keep
          std::cout << "[NamDrive] model loaded on GPU (2 instances)\n";
          return m;
        }
        mgr.unregisterInstance(mh, iid[0]);
      }
      mgr.unloadModel(mh);
    }
    // GPU load failed (unsupported model or CUDA error) — fall through to CPU
    std::cout << "[NamDrive] GPU load failed, falling back to CPU\n";
  }

  // ---- CPU fallback ----
  try {
    auto m = std::make_unique<Loaded>();
    for (auto& c : m->cpu) {   // two independent instances of the same file (L and R)
      auto dsp = nam::get_dsp(path);
      if (!dsp) {
        error = "Unsupported model";
        return nullptr;
      }
      dsp->Reset(sr, blockSize);
      dsp->prewarm();   // both instances start in the same state
      c = std::move(dsp);
    }
    m->expected_sr = m->cpu[0]->GetExpectedSampleRate();
    m->built_sr = sr;
    // The model's memory as NAM reports it (WaveNet: its receptive field); 0 (e.g. Linear) means "unknown": replay all we keep.
    const int prewarm = m->cpu[0]->GetPrewarmSamples();
    m->catch_up = prewarm > 0 ? std::min(prewarm, static_cast<int>(kHistory)) : static_cast<int>(kHistory);
    std::cout << "[NamDrive] model loaded on CPU (2 instances)\n";
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

void NamDrive::setLoadCallback(std::function<void()> cb) {
  std::lock_guard<std::mutex> lk(cb_mutex_);
  load_cb_ = std::move(cb);
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
  resyncChannels();
  if (!m) {
    setInfo(LoadState::Failed, "", err);
    return false;
  }
  active_.store(m.release(), std::memory_order_release);
  setInfo(LoadState::Loaded, toUtf8(path.filename()), "");
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
    std::unique_ptr<Loaded> m;
    if (!path.empty())
      m = build(path, sr, block, err);

    bool published = false;
    {
      std::lock_guard<std::mutex> il(info_mutex_);
      if (generation_ == gen) {
        // Publish: an empty model means "unload" (a failed load leaves the Drive in passthrough).
        Loaded* next = m ? m.release() : new Loaded();
        delete pending_.exchange(next, std::memory_order_acq_rel);
        if (next->valid())
          setInfo(LoadState::Loaded, toUtf8(path.filename()), "");
        else if (path.empty())
          setInfo(LoadState::Idle, "", "");     // an explicit "no model" is not a failure
        else
          setInfo(LoadState::Failed, "", err);
        published = true;
      }
      // else: a newer request superseded this one; the result is dropped (m is freed here)
    }
    collectGarbage();
    if (published) {
      std::lock_guard<std::mutex> cl(cb_mutex_);
      if (load_cb_)
        load_cb_();
    }
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
  resyncChannels();
}

bool NamDrive::isModelLoaded() const {
  return active_.load(std::memory_order_acquire) != nullptr;
}

bool NamDrive::isGpuActive() const {
  Loaded* m = active_.load(std::memory_order_acquire);
  return m && m->gpu;
}

std::uint64_t NamDrive::instanceSamples(int channel) const {
  return (channel == 0 || channel == 1) ? runs_[channel].load(std::memory_order_relaxed) : 0;
}

double NamDrive::modelExpectedSampleRate() const {
  Loaded* m = active_.load(std::memory_order_acquire);
  return m ? m->expected_sr : -1.0;
}

void NamDrive::process(float* left, float* right, int numSamples) {
  adoptPending();
  Loaded* model = active_.load(std::memory_order_acquire);
  if (numSamples <= 0 || !model || !left || !right)
    return;
  // Buffers are fixed (allocated in the constructor): a longer host block is processed in pieces.
  for (int off = 0; off < numSamples; off += kMaxBlock)
    processChunk(*model, left + off, right + off, std::min(static_cast<int>(kMaxBlock), numSamples - off));
}

void NamDrive::runInstance(Loaded& m, int ch, float* in, float* out, int n) {
  runs_[ch].fetch_add(static_cast<std::uint64_t>(n), std::memory_order_relaxed);
  if (m.gpu) {
    auto& mgr = GpuNam::GpuNamManager::instance();
    if (mgr.process(m.gpu_model, m.gpu_instance[ch], in, out, n) != GpuNam::GpuNamResult::Ok)
      std::fill(out, out + n, 0.0f);
  } else {
    NAM_SAMPLE* in_ptrs[1] = {in};
    NAM_SAMPLE* out_ptrs[1] = {out};
    m.cpu[ch]->process(in_ptrs, out_ptrs, n);
  }
}

/// The R instance was skipped for r_lag_ identical samples: run it on the last of them (L's input, the same signal) so
/// its state matches L's again. Exact when the gap is shorter than the replay; otherwise exact for models whose memory
/// fits in the replay (WaveNet, ConvNet, Linear up to kHistory taps), close for LSTM (a fading memory).
void NamDrive::catchUpRight(Loaded& m) {
  const int len = std::min(r_lag_, m.catch_up);
  r_lag_ = 0;
  if (len <= 0)
    return;
  int src = hist_pos_ - len;
  if (src < 0)
    src += kHistory;
  for (int i = 0; i < len; ++i) {
    catch_in_[i] = hist_[src];
    if (++src == kHistory)
      src = 0;
  }
  runInstance(m, 1, catch_in_.data(), catch_out_.data(), len);   // the output is not used
}

void NamDrive::processChunk(Loaded& m, float* left, float* right, int n) {
  const float drive_lin = static_cast<float>(std::pow(10.0f, drive_));
  const size_t bytes = static_cast<size_t>(n) * sizeof(float);
  const bool identical = std::memcmp(left, right, bytes) == 0;
  bool silent = identical;
  for (int i = 0; silent && i < n; ++i)
    silent = left[i] == 0.0f;

  // The compensation reference is the signal *before* the drive gain, so the output keeps the input's loudness whatever
  // the Drive setting. Linked over both channels: for a mono source this is exactly the old (L+R)/2 reference.
  float dry_sum_sq = 0.0f;
  for (int i = 0; i < n; ++i) {
    dry_sum_sq += left[i] * left[i];
    in_[0][i] = std::clamp(left[i] * drive_lin, -10.0f, 10.0f);
  }
  if (identical) {
    dry_sum_sq *= 2.0f;
  } else {
    for (int i = 0; i < n; ++i) {
      dry_sum_sq += right[i] * right[i];
      in_[1][i] = std::clamp(right[i] * drive_lin, -10.0f, 10.0f);
    }
  }
  const float in_rms = std::sqrt(dry_sum_sq / static_cast<float>(2 * n));

  // Does the R instance run? Different channels: always. Identical: not while it is already skipped; while it still runs,
  // it stops after kMonoEntrySamples identical non-silent samples (digital silence never stops it: a stereo source with
  // gaps would otherwise stop and catch up at every phrase).
  bool run_r = true;
  if (!identical) {
    r_skipping_ = false;
    identical_run_ = 0;
  } else if (r_skipping_) {
    run_r = false;
  } else {
    if (!silent)
      identical_run_ += n;
    if (identical_run_ >= kMonoEntrySamples) {
      r_skipping_ = true;
      run_r = false;
    }
  }

  runInstance(m, 0, in_[0].data(), out_[0].data(), n);
  if (run_r) {
    if (r_lag_ > 0)
      catchUpRight(m);
    if (identical)
      std::memcpy(in_[1].data(), in_[0].data(), bytes);
    runInstance(m, 1, in_[1].data(), out_[1].data(), n);
  } else {
    r_lag_ = std::min(r_lag_ + n, static_cast<int>(kHistory));
  }

  // L's driven input into the ring (what R would have seen in an identical block)
  for (int i = 0; i < n; ++i) {
    hist_[hist_pos_] = in_[0][i];
    if (++hist_pos_ == kHistory)
      hist_pos_ = 0;
  }

  const int channels = run_r ? 2 : 1;
  float out_sum_sq = 0.0f;
  for (int c = 0; c < channels; ++c) {
    for (int i = 0; i < n; ++i) {
      float v = out_[c][i];
      if (!std::isfinite(v))
        v = 0.0f;
      v = std::clamp(v, -10.0f, 10.0f);
      out_[c][i] = v;
      out_sum_sq += v * v;
    }
  }
  const float out_rms = std::sqrt(out_sum_sq / static_cast<float>(channels * n));

  const float alpha = computeRmsAlpha(sample_rate_, n);
  input_rms_smooth_  += alpha * (in_rms  - input_rms_smooth_);
  output_rms_smooth_ += alpha * (out_rms - output_rms_smooth_);

  float compensation = 1.0f;
  if (output_rms_smooth_ > kRmsFloor && input_rms_smooth_ > kRmsFloor) {
    compensation = input_rms_smooth_ / output_rms_smooth_;
    const float min_lin = std::pow(10.0f, kCompensationMinDb / 20.0f);
    const float max_lin = std::pow(10.0f, kCompensationMaxDb / 20.0f);
    compensation = std::clamp(compensation, min_lin, max_lin);
  }

  const float* out_r = run_r ? out_[1].data() : out_[0].data();   // skipped: R is L's result
  for (int i = 0; i < n; ++i) {
    left[i]  = out_[0][i] * compensation;
    right[i] = out_r[i] * compensation;
  }
}

} // namespace FairyTapes::Shared::DSP
