/**
 * Fairy Tapes · gpu_nam — Singleton GPU NAM Manager.
 *
 * GpuNamManager is the single point of contact for all plugin instances in a
 * DAW process. It owns one GpuNamEngine per loaded model and routes process()
 * calls to the correct engine.
 *
 * Phase 1: each process() call runs synchronously (one instance at a time).
 * Phase 2 (planned): batched async processing with double-buffering.
 *
 * Usage from NamDrive:
 *   auto& mgr = GpuNamManager::instance();
 *   if (mgr.isAvailable()) {
 *       auto handle = mgr.loadModel(path);
 *       mgr.process(handle, instanceId, in, out, n);
 *   }
 */

#pragma once

#include "gpu_nam_common.h"
#include "gpu_nam_engine.h"
#include "gpu_nam_model.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace FairyTapes::GpuNam {

/// Handle to a loaded model inside the manager.
using ModelHandle = int32_t;
static constexpr ModelHandle kInvalidModel = -1;

class GpuNamManager {
public:
  /// Access the process-wide singleton.
  static GpuNamManager& instance();

  /// True if CUDA is available and at least one CUDA device is present.
  bool isAvailable() const;

  /// Load a .nam model (or return an existing handle if already loaded).
  /// Thread-safe (serialised internally).
  /// @param path       Path to .nam file.
  /// @param sampleRate Current audio sample rate.
  /// @param maxBlock   Maximum expected block size.
  /// @param outHandle  Receives the model handle on success.
  GpuNamResult loadModel(const std::filesystem::path& path,
                         double sampleRate, int maxBlock,
                         ModelHandle& outHandle);

  /// Unload a model. All instances referencing it should be unregistered first.
  void unloadModel(ModelHandle handle);

  /// Register a plugin instance against a loaded model.
  GpuNamResult registerInstance(ModelHandle model, InstanceId& outId);

  /// Unregister a plugin instance.
  void unregisterInstance(ModelHandle model, InstanceId id);

  /// Run inference for one instance (Phase 1 — synchronous).
  GpuNamResult process(ModelHandle model, InstanceId id,
                       const float* inMono, float* outMono, int numSamples);

  /// Reset instance state (e.g. on transport stop or bypass toggle).
  void resetInstance(ModelHandle model, InstanceId id);

  /// Expected sample rate for a loaded model (-1 if unknown).
  double modelSampleRate(ModelHandle model) const;

private:
  GpuNamManager();
  ~GpuNamManager();

  GpuNamManager(const GpuNamManager&) = delete;
  GpuNamManager& operator=(const GpuNamManager&) = delete;

  struct ModelSlot {
    std::unique_ptr<GpuNamModel> model;
    std::unique_ptr<GpuNamEngine> engine;
    std::string canonical_path;
    int ref_count = 0;
  };

  mutable std::mutex mutex_;
  std::vector<ModelSlot> slots_;
  bool cuda_available_ = false;

  /// Map canonical path → slot index for deduplication.
  std::unordered_map<std::string, ModelHandle> path_to_slot_;
};

} // namespace FairyTapes::GpuNam
