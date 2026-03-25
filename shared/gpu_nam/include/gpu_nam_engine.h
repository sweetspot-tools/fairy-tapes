/**
 * Fairy Tapes · gpu_nam — GPU inference engine.
 *
 * GpuNamEngine manages per-instance state (ring buffers, RMS smoothers) on the
 * GPU and runs WaveNet inference for one or more instances in a single batched
 * kernel launch.
 *
 * Lifecycle:
 *   1. Construct with a loaded GpuNamModel.
 *   2. Call registerInstance() for each plugin instance that will share this model.
 *   3. From the audio thread, call process() with the instance's audio buffer.
 *
 * Thread safety: process() is safe to call from any thread as long as each
 * instance ID is processed by at most one thread at a time. Model loading and
 * instance registration must NOT happen concurrently with process().
 */

#pragma once

#include "gpu_nam_common.h"
#include <cstdint>

namespace FairyTapes::GpuNam {

class GpuNamModel;

/// Opaque handle for a registered plugin instance.
using InstanceId = int32_t;
static constexpr InstanceId kInvalidInstance = -1;

class GpuNamEngine {
public:
  GpuNamEngine();
  ~GpuNamEngine();

  GpuNamEngine(const GpuNamEngine&) = delete;
  GpuNamEngine& operator=(const GpuNamEngine&) = delete;

  /// Initialise the engine for a given model.
  /// Allocates per-instance state buffers on the GPU.
  /// Must be called before registerInstance() or process().
  GpuNamResult init(const GpuNamModel& model, double sampleRate, int maxBlockSize);

  /// Release all GPU resources.
  void shutdown();

  bool isInitialised() const { return initialised_; }

  /// Register a new instance. Returns a handle used in process().
  /// Must NOT be called concurrently with process().
  GpuNamResult registerInstance(InstanceId& outId);

  /// Unregister an instance, freeing its state slot.
  void unregisterInstance(InstanceId id);

  /// Run WaveNet inference for a single instance (non-batched, Phase 1).
  ///
  /// @param id       Instance handle from registerInstance().
  /// @param inMono   Input audio buffer (mono, numSamples floats).
  /// @param outMono  Output audio buffer (mono, numSamples floats).
  /// @param numSamples Number of samples in the block (≤ maxBlockSize).
  ///
  /// The caller is responsible for stereo→mono conversion and drive gain
  /// before calling, and for RMS auto-compensation after.
  GpuNamResult process(InstanceId id, const float* inMono, float* outMono, int numSamples);

  /// Reset an instance's state (ring buffers, internal activations) and
  /// re-run the prewarm phase (processing zeros to fill ring buffers,
  /// matching CPU NAM behaviour).
  void resetInstance(InstanceId id);

  /// Number of currently registered instances.
  int numInstances() const { return num_instances_; }

  /// Number of zero-samples processed during prewarm (for diagnostics).
  int prewarmSamples() const { return prewarm_samples_; }

private:
  /// Run the prewarm phase for an instance: process prewarm_samples_ zeros
  /// through the WaveNet to fill ring buffers with bias-derived state,
  /// matching the behaviour of NeuralAmpModelerCore's DSP::Reset().
  void prewarmInstance(InstanceId id);

  bool initialised_ = false;
  int max_block_size_ = 0;
  double sample_rate_ = 48000.0;
  int num_instances_ = 0;
  int prewarm_samples_ = 0;

  // GPU-side state — opaque pointers, managed internally.
  // Ring buffers for each layer's dilated convolution, per instance.
  float* d_ring_buffers_ = nullptr;
  int ring_buffer_total_size_ = 0;

  // Intermediate layer activations on GPU (re-used across process calls).
  float* d_layer_io_ = nullptr;
  int layer_io_size_ = 0;

  // Head accumulator on GPU.
  float* d_head_accum_ = nullptr;
  int head_accum_size_ = 0;

  // Device-side input/output staging buffers.
  float* d_audio_in_ = nullptr;
  float* d_audio_out_ = nullptr;

  // Instance slot tracking.
  bool instance_active_[kMaxInstances] = {};

  // Cached model reference (non-owning).
  const GpuNamModel* model_ = nullptr;

  // Host-side copy of model descriptor for kernel launch config.
  WavenetDesc desc_{};
};

} // namespace FairyTapes::GpuNam
