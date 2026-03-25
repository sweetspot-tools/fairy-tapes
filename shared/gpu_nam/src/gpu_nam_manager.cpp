/**
 * Fairy Tapes · gpu_nam — GpuNamManager singleton implementation.
 *
 * The manager is the single entry point for all plugin instances in a DAW
 * process. It owns one (model + engine) pair per loaded .nam model and
 * routes process() calls accordingly.
 *
 * Model deduplication: if two plugin instances load the same .nam file
 * (by canonical path), they share the same GPU weights.
 */

#include "gpu_nam_manager.h"

#include <algorithm>
#include <filesystem>
#include <iostream>

#if GPU_NAM_HAS_CUDA
#include <cuda_runtime.h>
#endif

namespace FairyTapes::GpuNam {

// -----------------------------------------------------------------------
// Singleton
// -----------------------------------------------------------------------

GpuNamManager& GpuNamManager::instance() {
  static GpuNamManager mgr;
  return mgr;
}

GpuNamManager::GpuNamManager() {
#if GPU_NAM_HAS_CUDA
  int device_count = 0;
  cudaError_t err = cudaGetDeviceCount(&device_count);
  cuda_available_ = (err == cudaSuccess && device_count > 0);
  if (cuda_available_) {
    cudaDeviceProp prop;
    cudaGetDeviceProperties(&prop, 0);
    std::cout << "[gpu_nam] CUDA available: " << prop.name
              << " (compute " << prop.major << "." << prop.minor
              << ", " << (prop.totalGlobalMem >> 20) << " MB)\n";
  } else {
    std::cout << "[gpu_nam] no CUDA device found — GPU NAM disabled\n";
  }
#else
  cuda_available_ = false;
#endif
}

GpuNamManager::~GpuNamManager() {
  std::lock_guard<std::mutex> lock(mutex_);
  slots_.clear();
  path_to_slot_.clear();
}

bool GpuNamManager::isAvailable() const { return cuda_available_; }

GpuNamResult GpuNamManager::loadModel(const std::filesystem::path& path,
                                      double sampleRate, int maxBlock,
                                      ModelHandle& outHandle) {
  if (!cuda_available_) return GpuNamResult::CudaNotAvailable;

  std::lock_guard<std::mutex> lock(mutex_);

  // Canonical path for deduplication
  std::string canon;
  try { canon = std::filesystem::canonical(path).string(); }
  catch (...) { canon = path.string(); }

  // Check if already loaded
  auto it = path_to_slot_.find(canon);
  if (it != path_to_slot_.end()) {
    outHandle = it->second;
    slots_[outHandle].ref_count++;
    return GpuNamResult::Ok;
  }

  // New model — load
  auto model  = std::make_unique<GpuNamModel>();
  auto result = model->load(path);
  if (result != GpuNamResult::Ok) {
    outHandle = kInvalidModel;
    return result;
  }

  auto engine = std::make_unique<GpuNamEngine>();
  result = engine->init(*model, sampleRate, maxBlock);
  if (result != GpuNamResult::Ok) {
    outHandle = kInvalidModel;
    return result;
  }

  // Find a free slot or append
  ModelHandle handle = kInvalidModel;
  for (size_t i = 0; i < slots_.size(); ++i) {
    if (!slots_[i].model) {
      handle = static_cast<ModelHandle>(i);
      break;
    }
  }
  if (handle == kInvalidModel) {
    handle = static_cast<ModelHandle>(slots_.size());
    slots_.emplace_back();
  }

  auto& slot = slots_[handle];
  slot.model = std::move(model);
  slot.engine = std::move(engine);
  slot.canonical_path = canon;
  slot.ref_count = 1;

  path_to_slot_[canon] = handle;
  outHandle = handle;
  return GpuNamResult::Ok;
}

void GpuNamManager::unloadModel(ModelHandle handle) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (handle < 0 || handle >= static_cast<int>(slots_.size())) return;

  auto& slot = slots_[handle];
  if (!slot.model) return;

  slot.ref_count--;
  if (slot.ref_count <= 0) {
    path_to_slot_.erase(slot.canonical_path);
    slot.engine->shutdown();
    slot.engine.reset();
    slot.model->unload();
    slot.model.reset();
    slot.canonical_path.clear();
    slot.ref_count = 0;
  }
}

GpuNamResult GpuNamManager::registerInstance(ModelHandle model, InstanceId& outId) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (model < 0 || model >= static_cast<int>(slots_.size())) return GpuNamResult::InvalidParameter;
  if (!slots_[model].engine) return GpuNamResult::NotInitialized;
  return slots_[model].engine->registerInstance(outId);
}

void GpuNamManager::unregisterInstance(ModelHandle model, InstanceId id) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (model < 0 || model >= static_cast<int>(slots_.size())) return;
  if (slots_[model].engine) slots_[model].engine->unregisterInstance(id);
}

GpuNamResult GpuNamManager::process(ModelHandle model, InstanceId id,
                                    const float* inMono, float* outMono, int numSamples) {
  // No mutex here — process() must be fast (audio thread).
  // Engine::process() is safe for different instance IDs from different threads.
  if (model < 0 || model >= static_cast<int>(slots_.size())) return GpuNamResult::InvalidParameter;
  auto* engine = slots_[model].engine.get();
  if (!engine) return GpuNamResult::NotInitialized;
  return engine->process(id, inMono, outMono, numSamples);
}

void GpuNamManager::resetInstance(ModelHandle model, InstanceId id) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (model < 0 || model >= static_cast<int>(slots_.size())) return;
  if (slots_[model].engine) slots_[model].engine->resetInstance(id);
}

double GpuNamManager::modelSampleRate(ModelHandle model) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (model < 0 || model >= static_cast<int>(slots_.size())) return -1.0;
  if (!slots_[model].model) return -1.0;
  return slots_[model].model->expectedSampleRate();
}

} // namespace FairyTapes::GpuNam
