/**
 * Fairy Tapes · gpu_nam — Test suite for GPU WaveNet inference.
 *
 * These tests verify that the GPU WaveNet kernel produces output matching
 * the reference CPU implementation (NeuralAmpModelerCore) within a tight
 * tolerance.
 *
 * To run:
 *   cd shared/gpu_nam/build && cmake .. && make && ctest -V
 *
 * Requirements:
 *   - NVIDIA GPU with compute capability ≥ 7.5
 *   - A .nam test model (set via GPU_NAM_TEST_MODEL env var, or auto-skipped)
 */

#include "gpu_nam_common.h"
#include "gpu_nam_model.h"
#include "gpu_nam_engine.h"
#include "gpu_nam_manager.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

// NAM CPU reference
#ifndef NAM_SAMPLE_FLOAT
#define NAM_SAMPLE_FLOAT
#endif
#include "NAM/get_dsp.h"

using namespace FairyTapes::GpuNam;

// -----------------------------------------------------------------------
// Minimal test framework
// -----------------------------------------------------------------------

static int g_passed = 0;
static int g_failed = 0;
static int g_skipped = 0;

#define ASSERT_TRUE(expr) \
  do { if (!(expr)) { \
    std::cerr << "  FAIL: " #expr " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
    return false; \
  }} while (0)

#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))

#define RUN_TEST(fn, name) do { \
  std::cout << "[TEST] " << name << " ... "; \
  bool ok = fn(); \
  if (ok) { std::cout << "PASS\n"; g_passed++; } \
  else { std::cout << "FAIL\n"; g_failed++; } \
} while (0)

#define SKIP_TEST(name, reason) do { \
  std::cout << "[SKIP] " << name << " — " << reason << "\n"; \
  g_skipped++; \
} while (0)

// -----------------------------------------------------------------------
// Test: model loading
// -----------------------------------------------------------------------

static std::string getTestModelPath() {
  const char* env = std::getenv("GPU_NAM_TEST_MODEL");
  return env ? std::string(env) : "";
}

static bool test_model_load_missing_file() {
  GpuNamModel model;
  auto r = model.load("/nonexistent/path.nam");
  ASSERT_TRUE(r == GpuNamResult::InvalidParameter);
  ASSERT_TRUE(!model.isLoaded());
  return true;
}

static bool test_model_load_valid(const std::string& path) {
  GpuNamModel model;
  auto r = model.load(path);
  if (r == GpuNamResult::ModelNotSupported) {
    std::cout << "(model not supported on GPU — OK) ";
    return true; // not a failure, just unsupported architecture variant
  }
  ASSERT_TRUE(r == GpuNamResult::Ok);
  ASSERT_TRUE(model.isLoaded());
  ASSERT_TRUE(model.deviceWeights() != nullptr);
  ASSERT_TRUE(!model.hostWeights().empty());
  ASSERT_TRUE(model.desc().num_layer_arrays > 0);
  ASSERT_TRUE(model.desc().total_weight_count == static_cast<int>(model.hostWeights().size()));
  return true;
}

// -----------------------------------------------------------------------
// Test: engine init + register/unregister
// -----------------------------------------------------------------------

static bool test_engine_lifecycle(const std::string& path) {
  GpuNamModel model;
  auto r = model.load(path);
  if (r != GpuNamResult::Ok) {
    std::cout << "(skip — model not loadable on GPU) ";
    return true;
  }

  GpuNamEngine engine;
  r = engine.init(model, 48000.0, 512);
  ASSERT_TRUE(r == GpuNamResult::Ok);
  ASSERT_TRUE(engine.isInitialised());

  InstanceId id;
  r = engine.registerInstance(id);
  ASSERT_TRUE(r == GpuNamResult::Ok);
  ASSERT_TRUE(id >= 0);
  ASSERT_TRUE(engine.numInstances() == 1);

  engine.unregisterInstance(id);
  ASSERT_TRUE(engine.numInstances() == 0);

  engine.shutdown();
  ASSERT_TRUE(!engine.isInitialised());
  return true;
}

// -----------------------------------------------------------------------
// Test: GPU vs CPU inference comparison
// -----------------------------------------------------------------------

static bool test_gpu_vs_cpu(const std::string& path) {
  GpuNamModel gpu_model;
  auto r = gpu_model.load(path);
  if (r != GpuNamResult::Ok) {
    std::cout << "(skip — model not loadable on GPU) ";
    return true;
  }

  const int block_size = 128;
  const double sample_rate = 48000.0;

  GpuNamEngine engine;
  r = engine.init(gpu_model, sample_rate, block_size);
  ASSERT_TRUE(r == GpuNamResult::Ok);

  InstanceId gpu_id;
  r = engine.registerInstance(gpu_id);
  ASSERT_TRUE(r == GpuNamResult::Ok);
  // registerInstance() now runs prewarm automatically

  auto cpu_dsp = nam::get_dsp(std::filesystem::path(path));
  ASSERT_TRUE(cpu_dsp != nullptr);
  cpu_dsp->Reset(sample_rate, block_size); // CPU prewarm happens here

  // Generate test signal: impulse + sine sweep
  std::vector<float> input(block_size);
  for (int i = 0; i < block_size; ++i) {
    float t = static_cast<float>(i) / static_cast<float>(sample_rate);
    input[i] = 0.5f * std::sin(2.0f * 3.14159265f * 440.0f * t);
    if (i == 0) input[i] = 1.0f;
  }

  // Single pass — both CPU and GPU start from equivalent prewarmed state
  std::vector<float> gpu_out(block_size, 0.0f);
  r = engine.process(gpu_id, input.data(), gpu_out.data(), block_size);
  ASSERT_TRUE(r == GpuNamResult::Ok);

  std::vector<float> cpu_out(block_size, 0.0f);
  float* in_ptrs[1] = { input.data() };
  float* out_ptrs[1] = { cpu_out.data() };
  cpu_dsp->process(in_ptrs, out_ptrs, block_size);

  float max_abs_err = 0.0f;
  bool any_nonzero = false;

  for (int i = 0; i < block_size; ++i) {
    float abs_err = std::fabs(gpu_out[i] - cpu_out[i]);
    if (abs_err > max_abs_err) max_abs_err = abs_err;
    if (std::fabs(gpu_out[i]) > 1e-9f) any_nonzero = true;
  }

  std::cout << "(max_abs_err=" << max_abs_err
            << ", prewarm=" << engine.prewarmSamples() << ") ";

  ASSERT_TRUE(any_nonzero);
  // With matching prewarm, GPU/CPU should agree within float epsilon
  // accumulation. Tolerance: 1e-5 (generous for deep WaveNets).
  ASSERT_TRUE(max_abs_err < 1e-5f);

  engine.unregisterInstance(gpu_id);
  engine.shutdown();
  return true;
}

// -----------------------------------------------------------------------
// Test: manager singleton
// -----------------------------------------------------------------------

static bool test_manager_singleton() {
  auto& mgr1 = GpuNamManager::instance();
  auto& mgr2 = GpuNamManager::instance();
  ASSERT_TRUE(&mgr1 == &mgr2);
  // isAvailable() should return true if we have CUDA
  #if GPU_NAM_HAS_CUDA
  // We can only assert this if we actually have a GPU
  std::cout << "(cuda_available=" << mgr1.isAvailable() << ") ";
  #endif
  return true;
}

// -----------------------------------------------------------------------
// Test: manager load/unload/process cycle
// -----------------------------------------------------------------------

static bool test_manager_full_cycle(const std::string& path) {
  auto& mgr = GpuNamManager::instance();
  if (!mgr.isAvailable()) {
    std::cout << "(skip — CUDA not available) ";
    return true;
  }

  ModelHandle mh;
  auto r = mgr.loadModel(path, 48000.0, 256, mh);
  if (r == GpuNamResult::ModelNotSupported) {
    std::cout << "(skip — model not supported on GPU) ";
    return true;
  }
  ASSERT_TRUE(r == GpuNamResult::Ok);
  ASSERT_TRUE(mh >= 0);

  InstanceId id;
  r = mgr.registerInstance(mh, id);
  ASSERT_TRUE(r == GpuNamResult::Ok);

  // Process a small block
  const int N = 64;
  std::vector<float> in(N, 0.1f), out(N, 0.0f);
  r = mgr.process(mh, id, in.data(), out.data(), N);
  ASSERT_TRUE(r == GpuNamResult::Ok);

  mgr.unregisterInstance(mh, id);
  mgr.unloadModel(mh);
  return true;
}

// -----------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------

int main() {
  std::cout << "=== gpu_nam test suite ===\n\n";

  std::string model_path = getTestModelPath();
  bool have_model = !model_path.empty() && std::filesystem::exists(model_path);

  // Always-run tests
  RUN_TEST(test_model_load_missing_file, "model_load_missing_file");
  RUN_TEST(test_manager_singleton, "manager_singleton");

  // Tests requiring a .nam model
  if (have_model) {
    RUN_TEST([&]{ return test_model_load_valid(model_path); }, "model_load_valid");
    RUN_TEST([&]{ return test_engine_lifecycle(model_path); }, "engine_lifecycle");
    RUN_TEST([&]{ return test_gpu_vs_cpu(model_path); }, "gpu_vs_cpu");
    RUN_TEST([&]{ return test_manager_full_cycle(model_path); }, "manager_full_cycle");
  } else {
    SKIP_TEST("model_load_valid", "set GPU_NAM_TEST_MODEL=/path/to/model.nam");
    SKIP_TEST("engine_lifecycle", "set GPU_NAM_TEST_MODEL=/path/to/model.nam");
    SKIP_TEST("gpu_vs_cpu", "set GPU_NAM_TEST_MODEL=/path/to/model.nam");
    SKIP_TEST("manager_full_cycle", "set GPU_NAM_TEST_MODEL=/path/to/model.nam");
  }

  std::cout << "\n=== Results: " << g_passed << " passed, "
            << g_failed << " failed, " << g_skipped << " skipped ===\n";
  return g_failed > 0 ? 1 : 0;
}
