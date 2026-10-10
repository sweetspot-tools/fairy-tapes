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

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
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

  /// Load a .nam model file synchronously. Returns true on success.
  /// Attempts GPU loading first; falls back to CPU if GPU is unavailable
  /// or the model is unsupported on GPU.
  /// Must NOT be called from the audio thread, nor while process() is running.
  /// (For use while audio is running, use requestLoad().)
  bool loadModel(const std::filesystem::path& path);

  // ---- Asynchronous loading (message thread -> worker thread -> audio thread) ----

  enum class LoadState { Idle, Loading, Loaded, Failed };

  /// Ask for a model to be loaded on a worker thread. Never blocks. A newer request
  /// supersedes an older one (the last one wins). The audio thread adopts the ready
  /// model at the start of its next block with an atomic pointer exchange: process()
  /// never allocates, locks or waits, and a replaced model is freed off the audio thread.
  /// A failed load unloads the current model: the Drive passes the signal through.
  void requestLoad(const std::filesystem::path& path);

  /// Called from the worker thread each time a request has completed (loaded, failed or unloaded), never for a
  /// request that was superseded. Keep it short; pass nullptr to clear (returns once a running call has finished).
  void setLoadCallback(std::function<void()> cb);

  LoadState loadState() const { return static_cast<LoadState>(state_.load(std::memory_order_acquire)); }

  /// File name (no folder) of the last model that finished loading; empty when none or after a failure.
  std::string loadedModelName() const;

  /// Why the last load failed; empty unless loadState() == Failed.
  std::string lastError() const;

  /// Unload the current model (passthrough mode). Not for use while process() is running.
  void unloadModel();

  /// True once the audio thread has adopted a model (after requestLoad: from the next block).
  bool isModelLoaded() const;

  /// True if inference is running on GPU, false if CPU fallback.
  bool isGpuActive() const;

  /// Expected sample rate of the loaded model (-1 if unknown).
  double modelExpectedSampleRate() const;

  /// Process stereo audio in-place.
  /// If no model is loaded, the signal passes through unmodified.
  /// Output is auto-compensated to match input loudness.
  void process(float* left, float* right, int numSamples);

private:
  /// A model ready for inference (CPU or GPU path); an empty one means "no model".
  struct Loaded;
  static std::unique_ptr<Loaded> build(const std::filesystem::path& path, double sr, int blockSize, std::string& error);
  void workerLoop();
  void adoptPending();            // audio thread
  void collectGarbage();          // any non-audio thread
  void setInfo(LoadState st, const std::string& name, const std::string& err);  // caller holds info_mutex_

  std::atomic<Loaded*> active_{nullptr};   // read by the audio thread, replaced only by it (or by loadModel/unloadModel when idle)
  std::atomic<Loaded*> pending_{nullptr};  // handed over by the worker
  static constexpr int kGraveyard = 8;
  std::atomic<Loaded*> graveyard_[kGraveyard] = {};  // replaced models, freed off the audio thread

  std::atomic<int> state_{static_cast<int>(LoadState::Idle)};
  mutable std::mutex info_mutex_;          // generation_, name and error
  std::uint64_t generation_ = 0;
  std::string model_name_;
  std::string last_error_;

  std::mutex req_mutex_;                   // the single request slot
  std::condition_variable req_cv_;
  std::filesystem::path req_path_;
  double req_sr_ = 48000.0;
  int req_block_ = 8192;
  std::uint64_t req_gen_ = 0;
  bool req_pending_ = false;
  bool stop_ = false;
  std::thread worker_;

  std::atomic<double> sample_rate_{48000.0};   // written by the audio thread, read by requestLoad on the message thread
  std::mutex cb_mutex_;
  std::function<void()> load_cb_;
  int max_block_size_ = 8192;
  float drive_ = 0.0f;

  float input_rms_smooth_ = 0.0f;
  float output_rms_smooth_ = 0.0f;

  std::vector<float> mono_in_;
  std::vector<float> mono_out_;
};

} // namespace FairyTapes::Shared::DSP
