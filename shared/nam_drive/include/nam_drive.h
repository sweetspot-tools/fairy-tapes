/**
 * Fairy Tapes · shared — NAM Drive DSP wrapper.
 *
 * Single-knob drive: the Drive parameter controls how hard the signal is
 * pushed into the NAM model (0 = unity / coloration only, 1 = +20 dB).
 * Output level is auto-compensated via smoothed RMS tracking so that
 * increasing Drive changes timbre without changing perceived loudness.
 * Always 100% wet when active; bypass toggle (kBypassDrive) turns it off.
 *
 * Stereo (2026-11-02): dual mono, two instances of the model (L and R). When the two channels of a block are identical
 * the R instance is skipped and L's result is copied, so a mono source costs one instance; see process() for how the
 * skipped instance is kept in step. The level compensation is linked (one gain for both channels).
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

  /// Cheap when the rate is unchanged (the processor calls it every block); a new rate resets the model
  /// (nam::DSP::Reset allocates and prewarms), so call it from setActive/setupProcessing when the host changes the rate.
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

  /// Process stereo audio in-place: L through one model instance, R through the other (dual mono).
  /// If no model is loaded, the signal passes through unmodified.
  /// Output is auto-compensated to match input loudness (linked: one gain for both channels).
  /// When L and R of a block are bit-identical the R instance is skipped and L's output copied. The skipped instance is
  /// kept in step: digital silence never stops it, it stops only after kMonoEntrySamples identical non-silent samples
  /// (it has then seen the same input as L for longer than any model's memory, so both states match), and when the
  /// channels differ again it first replays the identical samples it missed (at most the model's memory, from a
  /// kHistory ring of L's input) before the block: no click, at the price of one larger block at that moment.
  void process(float* left, float* right, int numSamples);

  /// Samples run through the model instance of a channel (0 = L, 1 = R). A diagnostic for tests.
  std::uint64_t instanceSamples(int channel) const;

  static constexpr int kMaxBlock = 8192;            // process() works in chunks of at most this many samples
  static constexpr int kHistory = 8192;             // samples of the common signal kept to bring a skipped R instance back
  static constexpr int kMonoEntrySamples = 16384;   // identical, non-silent samples before the R instance stops

private:
  /// A model ready for inference (CPU or GPU path); an empty one means "no model".
  struct Loaded;
  static std::unique_ptr<Loaded> build(const std::filesystem::path& path, double sr, int blockSize, std::string& error);
  void workerLoop();
  void adoptPending();            // audio thread
  void collectGarbage();          // any non-audio thread
  void setInfo(LoadState st, const std::string& name, const std::string& err);  // caller holds info_mutex_
  void processChunk(Loaded& m, float* left, float* right, int n);   // audio thread, n <= kMaxBlock
  void runInstance(Loaded& m, int ch, float* in, float* out, int n);
  void catchUpRight(Loaded& m);
  void resyncChannels();          // both instances are in the same state (fresh model, reset)

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
  int max_block_size_ = kMaxBlock;
  float drive_ = 0.0f;

  float input_rms_smooth_ = 0.0f;
  float output_rms_smooth_ = 0.0f;

  // Audio-thread state of the dual mono path. Buffers are allocated once in the constructor.
  std::vector<float> in_[2], out_[2];
  std::vector<float> hist_;                // ring of L's driven input: what a skipped R instance missed
  std::vector<float> catch_in_, catch_out_;
  int hist_pos_ = 0;
  bool r_skipping_ = true;                 // the R instance is skipped while L and R are identical
  int identical_run_ = 0;                  // identical non-silent samples in a row while R still runs
  int r_lag_ = 0;                          // samples the skipped R instance has missed (capped at kHistory)
  std::atomic<std::uint64_t> runs_[2] = {};
};

} // namespace FairyTapes::Shared::DSP
