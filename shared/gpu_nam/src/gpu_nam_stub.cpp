/**
 * Fairy Tapes · gpu_nam — CPU-only stub.
 *
 * Compiled only when CUDA is not available. Defines GpuNamModel and GpuNamEngine
 * so that gpu_nam_manager.cpp links; every operation reports CudaNotAvailable and
 * callers fall back to the CPU path.
 */

#include "gpu_nam_engine.h"
#include "gpu_nam_model.h"

namespace FairyTapes::GpuNam {

GpuNamModel::GpuNamModel() = default;
GpuNamModel::~GpuNamModel() = default;
GpuNamModel::GpuNamModel(GpuNamModel&&) noexcept = default;
GpuNamModel& GpuNamModel::operator=(GpuNamModel&&) noexcept = default;

GpuNamResult GpuNamModel::load(const std::filesystem::path&) { return GpuNamResult::CudaNotAvailable; }
void GpuNamModel::unload() {}

GpuNamEngine::GpuNamEngine() = default;
GpuNamEngine::~GpuNamEngine() = default;

GpuNamResult GpuNamEngine::init(const GpuNamModel&, double, int) { return GpuNamResult::CudaNotAvailable; }
void GpuNamEngine::shutdown() {}
GpuNamResult GpuNamEngine::registerInstance(InstanceId&) { return GpuNamResult::CudaNotAvailable; }
void GpuNamEngine::unregisterInstance(InstanceId) {}
GpuNamResult GpuNamEngine::process(InstanceId, const float*, float*, int) { return GpuNamResult::CudaNotAvailable; }
void GpuNamEngine::resetInstance(InstanceId) {}

} // namespace FairyTapes::GpuNam
