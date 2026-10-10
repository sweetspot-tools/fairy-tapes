/**
 * Fairy Tapes · shared/nam_drive — asynchronous model loading tests.
 * Standalone (no framework): each check prints on failure; exit code 1 if any failed.
 */

#include "nam_drive.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#ifndef NAM_DRIVE_FIXTURES
#error NAM_DRIVE_FIXTURES must point to the tests/fixtures directory
#endif

using namespace FairyTapes::Shared::DSP;
namespace fs = std::filesystem;

namespace {
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

const fs::path kFixture = fs::path(NAM_DRIVE_FIXTURES) / "linear_unity.nam";

bool waitFor(const NamDrive& d, NamDrive::LoadState want, int ms = 5000) {
  const auto t0 = std::chrono::steady_clock::now();
  while (d.loadState() != want) {
    if (std::chrono::steady_clock::now() - t0 > std::chrono::milliseconds(ms)) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return true;
}

// The audio thread adopts a ready model at the start of the next block.
void pump(NamDrive& d) { std::vector<float> l(64, 0.f), r(64, 0.f); d.process(l.data(), r.data(), 64); }

void test_requestLoad_reaches_Loaded() {
  NamDrive d; d.setSampleRate(48000.0);
  CHECK(d.loadState() == NamDrive::LoadState::Idle);
  d.requestLoad(kFixture);
  const bool loaded = waitFor(d, NamDrive::LoadState::Loaded);
  if (!loaded) std::fprintf(stderr, "  load error: %s\n", d.lastError().c_str());
  CHECK(loaded);
  CHECK(d.loadedModelName() == "linear_unity.nam");
  CHECK(d.lastError().empty());
  pump(d);
  CHECK(d.isModelLoaded());
}

void test_requestLoad_missing_file_fails_and_passes_through() {
  NamDrive d; d.setSampleRate(48000.0);
  d.requestLoad(kFixture); waitFor(d, NamDrive::LoadState::Loaded); pump(d);
  CHECK(d.isModelLoaded());
  d.requestLoad(fs::path(NAM_DRIVE_FIXTURES) / "does_not_exist.nam");
  CHECK(waitFor(d, NamDrive::LoadState::Failed));
  CHECK(!d.lastError().empty());
  CHECK(d.loadedModelName().empty());
  pump(d);
  CHECK(!d.isModelLoaded());                       // a failed load unloads: passthrough
  std::vector<float> l{0.1f, -0.2f, 0.3f}, r{0.1f, -0.2f, 0.3f};
  d.process(l.data(), r.data(), 3);
  CHECK(l[0] == 0.1f && l[1] == -0.2f && l[2] == 0.3f);
}

void test_requestLoad_garbage_file_fails() {
  const fs::path p = fs::temp_directory_path() / "nam_drive_garbage.nam";
  { std::ofstream f(p); f << "this is not a model"; }
  NamDrive d; d.setSampleRate(48000.0);
  d.requestLoad(p);
  CHECK(waitFor(d, NamDrive::LoadState::Failed));
  CHECK(!d.lastError().empty());
  fs::remove(p);
}

void test_last_request_wins() {
  NamDrive d; d.setSampleRate(48000.0);
  d.requestLoad(kFixture);
  d.requestLoad(fs::path(NAM_DRIVE_FIXTURES) / "does_not_exist.nam");
  CHECK(waitFor(d, NamDrive::LoadState::Failed));
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  CHECK(d.loadState() == NamDrive::LoadState::Failed);   // the first (valid) result must not overwrite it
  pump(d);
  CHECK(!d.isModelLoaded());
}

void test_process_during_load_is_finite() {
  NamDrive d; d.setSampleRate(48000.0); d.setDrive(0.5f);
  std::atomic<bool> stop{false}; std::atomic<bool> bad{false};
  std::thread audio([&] {
    std::vector<float> l(128), r(128);
    unsigned seed = 1;
    while (!stop.load()) {
      for (int i = 0; i < 128; ++i) { seed = seed * 1664525u + 1013904223u; float v = ((seed >> 8) & 0xFFFF) / 32768.0f - 1.0f; l[i] = r[i] = 0.5f * v; }
      d.process(l.data(), r.data(), 128);
      for (int i = 0; i < 128; ++i) if (!std::isfinite(l[i]) || std::fabs(l[i]) > 10.f) bad = true;
    }
  });
  for (int k = 0; k < 20; ++k) { d.requestLoad(kFixture); std::this_thread::sleep_for(std::chrono::milliseconds(3)); }
  waitFor(d, NamDrive::LoadState::Loaded);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  stop = true; audio.join();
  CHECK(!bad.load());
  CHECK(d.isModelLoaded());
}
} // namespace

int main() {
  std::printf("nam_drive async tests\n");
  test_requestLoad_reaches_Loaded();
  test_requestLoad_missing_file_fails_and_passes_through();
  test_requestLoad_garbage_file_fails();
  test_last_request_wins();
  test_process_during_load_is_finite();
  std::printf(g_failed ? "FAILED (%d)\n" : "All nam_drive async tests passed\n", g_failed);
  return g_failed ? 1 : 0;
}
