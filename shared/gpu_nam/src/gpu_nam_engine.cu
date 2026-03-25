/**
 * Fairy Tapes · gpu_nam — GpuNamEngine implementation.
 *
 * Manages per-instance GPU state (ring buffers, write positions) and
 * launches the WaveNet inference kernel for each process() call.
 *
 * Phase 1: each process() call is synchronous and processes one instance.
 * Phase 2 (planned): batch multiple instances into a single kernel launch.
 */

#include "gpu_nam_engine.h"
#include "gpu_nam_model.h"

#include <cstring>
#include <iostream>

#if GPU_NAM_HAS_CUDA
#include <cuda_runtime.h>
#include "wavenet_kernel.cuh"
#endif

namespace FairyTapes::GpuNam {

GpuNamEngine::GpuNamEngine() {
  std::memset(instance_active_, 0, sizeof(instance_active_));
}

GpuNamEngine::~GpuNamEngine() { shutdown(); }

GpuNamResult GpuNamEngine::init(const GpuNamModel& model, double sampleRate, int maxBlockSize) {
  if (initialised_) return GpuNamResult::AlreadyInitialized;
  if (!model.isLoaded()) return GpuNamResult::NotInitialized;

#if GPU_NAM_HAS_CUDA
  model_ = &model;
  desc_ = model.desc();
  sample_rate_ = sampleRate;
  max_block_size_ = maxBlockSize;

  // ---- Ring buffers (per instance) ----
  // We pre-allocate for kMaxInstances instances. Each instance gets its
  // own ring buffer block plus a ring-position int per layer.
  int ring_per_instance = computeTotalRingSize(desc_, 1);
  // ring_per_instance is in floats (channels × capacity per layer, summed).
  ring_buffer_total_size_ = ring_per_instance * kMaxInstances;

  cudaError_t err;
  err = cudaMalloc(&d_ring_buffers_, ring_buffer_total_size_ * sizeof(float));
  if (err != cudaSuccess) {
    std::cerr << "[gpu_nam] ring buffer alloc failed: " << cudaGetErrorString(err) << "\n";
    return GpuNamResult::CudaError;
  }
  cudaMemset(d_ring_buffers_, 0, ring_buffer_total_size_ * sizeof(float));

  // Ring write-position counters: one int per layer per instance.
  int total_layers = 0;
  for (int a = 0; a < desc_.num_layer_arrays; ++a)
    total_layers += desc_.arrays[a].num_layers;

  int ring_pos_total = total_layers * kMaxInstances;
  // We store ring positions in a device int array.
  err = cudaMalloc(&d_layer_io_, ring_pos_total * sizeof(int));
  if (err != cudaSuccess) {
    std::cerr << "[gpu_nam] ring pos alloc failed\n";
    shutdown();
    return GpuNamResult::CudaError;
  }
  cudaMemset(d_layer_io_, 0, ring_pos_total * sizeof(int));
  // ^ reusing d_layer_io_ as ring_pos storage (rename below)

  // ---- Audio staging buffers ----
  err = cudaMalloc(&d_audio_in_, maxBlockSize * sizeof(float));
  if (err != cudaSuccess) { shutdown(); return GpuNamResult::CudaError; }

  err = cudaMalloc(&d_audio_out_, maxBlockSize * sizeof(float));
  if (err != cudaSuccess) { shutdown(); return GpuNamResult::CudaError; }

  // ---- Head accumulator scratch (one per instance not needed in Phase 1) ----
  d_head_accum_ = nullptr;

  // Store total layers count for per-instance ring_pos offset
  layer_io_size_ = total_layers;

  // Compute prewarm samples (mirrors WaveNet::PrewarmSamples in NAM core):
  // 1 + sum of receptive_field across all layer arrays.
  prewarm_samples_ = 1;
  for (int a = 0; a < desc_.num_layer_arrays; ++a)
    for (int l = 0; l < desc_.arrays[a].num_layers; ++l)
      prewarm_samples_ += desc_.arrays[a].layers[l].dilation
                        * (desc_.arrays[a].layers[l].kernel_size - 1);

  num_instances_ = 0;
  initialised_ = true;

  std::cout << "[gpu_nam] engine initialised (max_block=" << maxBlockSize
            << ", ring_per_instance=" << ring_per_instance
            << " floats, total_layers=" << total_layers
            << ", prewarm=" << prewarm_samples_ << ")\n";
  return GpuNamResult::Ok;
#else
  return GpuNamResult::CudaNotAvailable;
#endif
}

void GpuNamEngine::shutdown() {
#if GPU_NAM_HAS_CUDA
  if (d_ring_buffers_)  { cudaFree(d_ring_buffers_);  d_ring_buffers_ = nullptr; }
  if (d_layer_io_)      { cudaFree(d_layer_io_);      d_layer_io_ = nullptr; }
  if (d_audio_in_)      { cudaFree(d_audio_in_);      d_audio_in_ = nullptr; }
  if (d_audio_out_)     { cudaFree(d_audio_out_);     d_audio_out_ = nullptr; }
  if (d_head_accum_)    { cudaFree(d_head_accum_);    d_head_accum_ = nullptr; }
#endif
  initialised_ = false;
  model_ = nullptr;
  num_instances_ = 0;
  std::memset(instance_active_, 0, sizeof(instance_active_));
}

GpuNamResult GpuNamEngine::registerInstance(InstanceId& outId) {
  if (!initialised_) return GpuNamResult::NotInitialized;

  for (int i = 0; i < kMaxInstances; ++i) {
    if (!instance_active_[i]) {
      instance_active_[i] = true;
      outId = i;
      ++num_instances_;
      prewarmInstance(outId);
      return GpuNamResult::Ok;
    }
  }
  return GpuNamResult::InstanceLimitReached;
}

void GpuNamEngine::unregisterInstance(InstanceId id) {
  if (id < 0 || id >= kMaxInstances) return;
  if (instance_active_[id]) {
    instance_active_[id] = false;
    --num_instances_;
    resetInstance(id);
  }
}

GpuNamResult GpuNamEngine::process(InstanceId id, const float* inMono, float* outMono, int numSamples) {
  if (!initialised_) return GpuNamResult::NotInitialized;
  if (id < 0 || id >= kMaxInstances || !instance_active_[id])
    return GpuNamResult::InvalidParameter;
  if (numSamples <= 0 || numSamples > max_block_size_)
    return GpuNamResult::InvalidParameter;

#if GPU_NAM_HAS_CUDA
  // Copy input to device
  cudaMemcpy(d_audio_in_, inMono, numSamples * sizeof(float), cudaMemcpyHostToDevice);

  // Compute ring buffer offset for this instance
  int ring_per_instance = computeTotalRingSize(desc_, 1);
  float* d_ring = d_ring_buffers_ + id * ring_per_instance;

  // Ring position offset: layer_io_size_ ints per instance
  int* d_ring_pos = reinterpret_cast<int*>(d_layer_io_) + id * layer_io_size_;

  // Launch kernel
  launchWavenetInference(
      model_->deviceWeights(),
      desc_,
      d_ring,
      d_ring_pos,
      d_audio_in_,
      d_audio_out_,
      numSamples,
      nullptr, // d_layer_io not used in Phase 1
      nullptr  // d_head_accum not used in Phase 1
  );

  // Copy output back to host
  cudaMemcpy(outMono, d_audio_out_, numSamples * sizeof(float), cudaMemcpyDeviceToHost);

  // Check for kernel errors
  cudaError_t err = cudaGetLastError();
  if (err != cudaSuccess) {
    std::cerr << "[gpu_nam] kernel error: " << cudaGetErrorString(err) << "\n";
    return GpuNamResult::CudaError;
  }

  return GpuNamResult::Ok;
#else
  return GpuNamResult::CudaNotAvailable;
#endif
}

void GpuNamEngine::resetInstance(InstanceId id) {
  if (id < 0 || id >= kMaxInstances) return;
#if GPU_NAM_HAS_CUDA
  if (!initialised_) return;

  // Zero ring buffer for this instance
  int ring_per_instance = computeTotalRingSize(desc_, 1);
  float* d_ring = d_ring_buffers_ + id * ring_per_instance;
  cudaMemset(d_ring, 0, ring_per_instance * sizeof(float));

  // Zero ring positions for this instance
  int* d_ring_pos = reinterpret_cast<int*>(d_layer_io_) + id * layer_io_size_;
  cudaMemset(d_ring_pos, 0, layer_io_size_ * sizeof(int));

  // Re-run prewarm to fill ring buffers with bias-derived state
  if (instance_active_[id])
    prewarmInstance(id);
#endif
}

void GpuNamEngine::prewarmInstance(InstanceId id) {
#if GPU_NAM_HAS_CUDA
  if (prewarm_samples_ <= 0) return;

  float zero_in = 0.0f;
  float dummy_out;
  for (int i = 0; i < prewarm_samples_; ++i)
    process(id, &zero_in, &dummy_out, 1);
#endif
}

} // namespace FairyTapes::GpuNam
