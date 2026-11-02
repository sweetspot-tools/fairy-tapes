/**
 * Fairy Tapes · shared/nam_drive — asynchronous model loading tests.
 * Standalone (no framework): each check prints on failure; exit code 1 if any failed.
 */

#include "nam_drive.h"

#include <algorithm>
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

void test_requestLoad_empty_path_unloads_without_error() {
  NamDrive d; d.setSampleRate(48000.0);
  d.requestLoad(kFixture); waitFor(d, NamDrive::LoadState::Loaded); pump(d);
  CHECK(d.isModelLoaded());
  d.requestLoad(fs::path());                       // "No model"
  CHECK(waitFor(d, NamDrive::LoadState::Idle));
  CHECK(d.loadedModelName().empty());
  CHECK(d.lastError().empty());
  pump(d);
  CHECK(!d.isModelLoaded());
}

void test_load_callback_fires_when_a_request_completes() {
  NamDrive d; d.setSampleRate(48000.0);
  std::atomic<int> calls{0};
  d.setLoadCallback([&] { ++calls; });
  d.requestLoad(kFixture);
  CHECK(waitFor(d, NamDrive::LoadState::Loaded));
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(calls.load() == 1);
  d.requestLoad(fs::path(NAM_DRIVE_FIXTURES) / "does_not_exist.nam");
  CHECK(waitFor(d, NamDrive::LoadState::Failed));
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(calls.load() == 2);                        // failures are reported too
  d.setLoadCallback(nullptr);                      // cleared: no more calls
  d.requestLoad(kFixture);
  CHECK(waitFor(d, NamDrive::LoadState::Loaded));
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(calls.load() == 2);
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

// Auto-compensation must hold the output at the level of the signal *before* the Drive gain: turning Drive changes
// the timbre, not the loudness. With the linear unity fixture the model adds nothing, so any level change is the
// compensation's fault.
void test_compensation_matches_undriven_input_level() {
  for (float drive : {0.0f, 0.5f, 1.0f}) {
    NamDrive d; d.setSampleRate(48000.0); d.setDrive(drive);
    d.requestLoad(kFixture); waitFor(d, NamDrive::LoadState::Loaded); pump(d);
    constexpr int N = 512;
    constexpr float kAmp = 0.25f;
    std::vector<float> l(N), r(N);
    double outRms = 0.0;
    for (int b = 0; b < 60; ++b) {
      for (int i = 0; i < N; ++i) {
        const double t = static_cast<double>(b * N + i) / 48000.0;
        l[i] = r[i] = kAmp * static_cast<float>(std::sin(2.0 * 3.14159265358979 * 440.0 * t));
      }
      d.process(l.data(), r.data(), N);
      if (b == 59) {
        for (int i = 0; i < N; ++i) outRms += static_cast<double>(l[i]) * l[i];
        outRms = std::sqrt(outRms / N);
      }
    }
    const double db = 20.0 * std::log10(outRms / (kAmp / std::sqrt(2.0)));
    if (std::fabs(db) > 0.5) std::fprintf(stderr, "  drive %.1f: output %+.2f dB vs input\n", drive, db);
    CHECK(std::fabs(db) <= 0.5);
  }
}
// ---- Stereo (2026-11-02): dual mono, one model instance per channel ----

const fs::path kDelay3 = fs::path(NAM_DRIVE_FIXTURES) / "linear_delay3.nam";   // y[n] = x[n-3]: has memory
const fs::path kAvg2 = fs::path(NAM_DRIVE_FIXTURES) / "linear_avg2.nam";       // y[n] = (x[n] + x[n-1]) / 2: a gentle low pass

constexpr double kPi = 3.14159265358979;
// A sine with a period of 16 samples: every 64-sample block holds 4 whole periods, so its RMS is the same whatever the phase.
float p16(int n, double phase) { return 0.5f * static_cast<float>(std::sin(2.0 * kPi * n / 16.0 + phase)); }

double maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
  double m = 0;
  for (size_t i = 0; i < a.size() && i < b.size(); ++i) m = std::max(m, (double)std::fabs(a[i] - b[i]));
  return m;
}
double blockRmsOf(const std::vector<float>& a) {
  double s = 0;
  for (float v : a) s += (double)v * v;
  return a.empty() ? 0.0 : std::sqrt(s / a.size());
}

bool loadSync(NamDrive& d, const fs::path& p) {
  d.setSampleRate(48000.0);
  const bool ok = d.loadModel(p);
  if (!ok) std::fprintf(stderr, "  load error: %s\n", d.lastError().c_str());
  return ok;
}

// The processor calls setSampleRate before every block: with an unchanged rate that must not reset the model (it used to
// call nam::DSP::Reset, which wipes the history and prewarms, every block).
void test_same_rate_keeps_model_state() {
  NamDrive d; CHECK(loadSync(d, kDelay3));
  std::vector<float> l(64, 0.f), r(64, 0.f);
  l[63] = r[63] = 0.5f;
  d.setSampleRate(48000.0);
  d.process(l.data(), r.data(), 64);
  std::fill(l.begin(), l.end(), 0.f); std::fill(r.begin(), r.end(), 0.f);
  d.setSampleRate(48000.0);
  d.process(l.data(), r.data(), 64);
  CHECK(l[2] != 0.f);                          // the impulse comes out 3 samples later, in the next block
  CHECK(l[0] == 0.f && l[1] == 0.f && l[3] == 0.f);
}

struct StereoRun {
  NamDrive dual, refL, refR;   // the stereo drive and two single-instance mono runs (each channel on both sides)
  double worst = 0.0;          // largest difference from the mono runs
  int n = 0;
  void block(const std::vector<float>& inL, const std::vector<float>& inR) {
    std::vector<float> l(inL), r(inR), al(inL), ar(inL), bl(inR), br(inR);
    for (NamDrive* d : {&dual, &refL, &refR}) d->setSampleRate(48000.0);
    dual.process(l.data(), r.data(), (int)l.size());
    refL.process(al.data(), ar.data(), (int)al.size());
    refR.process(bl.data(), br.data(), (int)bl.size());
    for (size_t i = 0; i < l.size(); ++i)
      worst = std::max({worst, (double)std::fabs(l[i] - al[i]), (double)std::fabs(r[i] - bl[i])});
    lastL = l; lastR = r;
    n += (int)l.size();
  }
  std::vector<float> lastL, lastR;
};

std::vector<float> sineBlock(int start, double phase, int len = 64) {
  std::vector<float> b(len);
  for (int i = 0; i < len; ++i) b[i] = p16(start + i, phase);
  return b;
}

void test_different_channels_stay_apart_and_match_mono_runs() {
  StereoRun s;
  for (NamDrive* d : {&s.dual, &s.refL, &s.refR}) { CHECK(loadSync(*d, kDelay3)); d->setDrive(0.5f); }
  for (int b = 0; b < 50; ++b) s.block(sineBlock(s.n, 0.0), sineBlock(s.n, kPi / 2));
  if (s.worst > 1e-5) std::fprintf(stderr, "  stereo vs mono runs: %g\n", s.worst);
  CHECK(s.worst <= 1e-5);
  CHECK(maxAbsDiff(s.lastL, s.lastR) > 0.1);                 // L and R stay different (no mono sum)
  CHECK(s.dual.instanceSamples(1) > 0);
  // a side-only signal (L = -R) used to cancel in the mono sum
  StereoRun side;
  for (NamDrive* d : {&side.dual, &side.refL, &side.refR}) CHECK(loadSync(*d, kDelay3));
  for (int b = 0; b < 20; ++b) { auto x = sineBlock(side.n, 0.0); auto y = x; for (auto& v : y) v = -v; side.block(x, y); }
  CHECK(side.worst <= 1e-5);
  CHECK(blockRmsOf(side.lastL) > 0.3);
}

void test_identical_channels_run_one_instance() {
  NamDrive d; CHECK(loadSync(d, kDelay3)); d.setDrive(0.3f);
  NamDrive ref; CHECK(loadSync(ref, kDelay3)); ref.setDrive(0.3f);
  int n = 0;
  bool same = true, matches = true;
  for (int b = 0; b < 400; ++b) {   // 25600 samples, past the point where the R instance would stop anyway
    auto l = sineBlock(n, 0.3), r = l, rl = l, rr = l;
    d.setSampleRate(48000.0);
    d.process(l.data(), r.data(), 64);
    ref.process(rl.data(), rr.data(), 64);
    for (int i = 0; i < 64; ++i) { same &= (l[i] == r[i]); matches &= std::fabs(l[i] - rl[i]) < 1e-6; }
    n += 64;
  }
  CHECK(same);                         // identical in, identical out
  CHECK(matches);
  CHECK(d.instanceSamples(0) == 400u * 64u);
  CHECK(d.instanceSamples(1) == 0u);   // a mono source costs one instance, as before
}

// Blocks that switch between identical and different channels: every block still matches the two mono runs, so the
// skipped R instance is brought back in step (no click), and the R instance stops on a long identical stretch.
void test_switching_identical_and_different_is_seamless() {
  StereoRun s;
  for (NamDrive* d : {&s.dual, &s.refL, &s.refR}) CHECK(loadSync(*d, kDelay3));
  auto same = [&](int blocks) { for (int b = 0; b < blocks; ++b) { auto x = sineBlock(s.n, 0.0); s.block(x, x); } };
  auto apart = [&](int blocks) { for (int b = 0; b < blocks; ++b) s.block(sineBlock(s.n, 0.0), sineBlock(s.n, 1.0)); };
  auto silent = [&](int blocks) { for (int b = 0; b < blocks; ++b) { std::vector<float> z(64, 0.f); s.block(z, z); } };

  same(10);                                                   // a fresh model starts with R skipped
  CHECK(s.dual.instanceSamples(1) == 0u);
  apart(10);                                                  // R comes back: caught up on the 640 skipped samples
  CHECK(s.dual.instanceSamples(1) == 640u + 640u);
  same(400);                                                  // a long identical stretch: R stops after kMonoEntrySamples
  const auto rAfterSame = s.dual.instanceSamples(1);
  CHECK(rAfterSame >= 640u + 640u + (unsigned)NamDrive::kMonoEntrySamples - 64u);
  CHECK(rAfterSame <= 640u + 640u + (unsigned)NamDrive::kMonoEntrySamples + 64u);
  apart(1);                                                   // back again: the catch-up is bounded
  CHECK(s.dual.instanceSamples(1) - rAfterSame <= (unsigned)NamDrive::kHistory + 64u);
  apart(9);
  const auto rBeforeSilence = s.dual.instanceSamples(1);
  silent(400);                                                // digital silence is not "mono": R keeps running, warm
  CHECK(s.dual.instanceSamples(1) == rBeforeSilence + 400u * 64u);
  apart(5);
  CHECK(s.dual.instanceSamples(1) == rBeforeSilence + 405u * 64u);   // no catch-up needed
  // The compensation is linked; on the block where R jumps phase its output RMS differs from L's for a moment, so the
  // linked gain differs from a mono run's by a few 1e-3 for a while (one scalar per block, opposite on L and R). A stale R
  // instance would be off by the signal itself in the first samples after the switch (0.24 before the catch-up existed).
  if (s.worst > 1e-2) std::fprintf(stderr, "  switching vs mono runs: %g\n", s.worst);
  CHECK(s.worst <= 1e-2);
}

// The level compensation is linked (one gain for both channels, as today for a mono source): it keeps the loudness of the
// stage but not a level change the model makes between the channels. With the two-tap average the 12 kHz R channel comes out
// 3 dB lower than the 375 Hz L channel; a per-channel compensation would bring both back to their input level.
void test_compensation_is_linked() {
  NamDrive d; CHECK(loadSync(d, kAvg2));
  constexpr int N = 128;
  double inL = 0, inR = 0, outL = 0, outR = 0;
  for (int b = 0; b < 100; ++b) {
    std::vector<float> l(N), r(N);
    for (int i = 0; i < N; ++i) {
      l[i] = 0.4f * (float)std::sin(2.0 * kPi * (b * N + i) / 128.0);   // 375 Hz
      r[i] = 0.4f * (float)std::sin(2.0 * kPi * (b * N + i) / 4.0 + kPi / 4);   // 12 kHz
    }
    std::vector<float> l0(l), r0(r);
    d.setSampleRate(48000.0);
    d.process(l.data(), r.data(), N);
    if (b >= 80)
      for (int i = 0; i < N; ++i) { inL += l0[i] * l0[i]; inR += r0[i] * r0[i]; outL += l[i] * l[i]; outR += r[i] * r[i]; }
  }
  const double gL = std::sqrt(outL / inL), gR = std::sqrt(outR / inR);
  if (std::fabs(gR / gL - std::sqrt(0.5)) > 0.02) std::fprintf(stderr, "  R/L gain %.3f\n", gR / gL);
  CHECK(std::fabs(gR / gL - std::sqrt(0.5)) < 0.02);
  CHECK(std::fabs(10.0 * std::log10((outL + outR) / (inL + inR))) < 0.5);   // the stage keeps its loudness
}

// requestLoad builds both instances: a model loaded on the worker thread processes the two channels apart.
void test_async_load_builds_both_instances() {
  StereoRun s;
  s.dual.setSampleRate(48000.0);
  s.dual.requestLoad(kDelay3);
  CHECK(waitFor(s.dual, NamDrive::LoadState::Loaded));
  pump(s.dual);
  CHECK(loadSync(s.refL, kDelay3) && loadSync(s.refR, kDelay3));
  // pump ran 64 zeros through the stereo drive; give the references the same
  { std::vector<float> z(64, 0.f), z2(z); s.refL.process(z.data(), z2.data(), 64); }
  { std::vector<float> z(64, 0.f), z2(z); s.refR.process(z.data(), z2.data(), 64); }
  // sine and cosine: the 3 samples the delay holds back carry the same energy on both channels, so the linked compensation
  // equals each mono run's from the first block and the comparison can be exact
  for (int b = 0; b < 30; ++b) s.block(sineBlock(s.n, 0.0), sineBlock(s.n, kPi / 2));
  CHECK(s.worst <= 1e-5);
  CHECK(s.dual.instanceSamples(1) > 0u);
}

// A model replaced while stereo audio runs: always finite.
void test_stereo_process_during_load_is_finite() {
  NamDrive d; d.setSampleRate(48000.0); d.setDrive(0.5f);
  std::atomic<bool> stop{false}; std::atomic<bool> bad{false};
  std::thread audio([&] {
    std::vector<float> l(128), r(128);
    unsigned seed = 7; int n = 0;
    while (!stop.load()) {
      for (int i = 0; i < 128; ++i) { seed = seed * 1664525u + 1013904223u; l[i] = 0.4f * (((seed >> 8) & 0xFFFF) / 32768.0f - 1.0f); r[i] = (n & 256) ? l[i] : p16(n + i, 0.0); }
      d.process(l.data(), r.data(), 128);
      n += 128;
      for (int i = 0; i < 128; ++i) if (!std::isfinite(l[i]) || !std::isfinite(r[i]) || std::fabs(l[i]) > 10.f || std::fabs(r[i]) > 10.f) bad = true;
    }
  });
  for (int k = 0; k < 20; ++k) { d.requestLoad(k & 1 ? kDelay3 : kFixture); std::this_thread::sleep_for(std::chrono::milliseconds(3)); }
  waitFor(d, NamDrive::LoadState::Loaded);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  stop = true; audio.join();
  CHECK(!bad.load());
}

// A host block larger than the internal buffers is processed in pieces, with the model.
void test_long_block_is_processed_in_pieces() {
  NamDrive d; CHECK(loadSync(d, kDelay3));
  const int N = NamDrive::kMaxBlock + 1808;
  std::vector<float> l(N), r(N);
  for (int i = 0; i < N; ++i) { l[i] = p16(i, 0.0); r[i] = p16(i, 1.0); }
  std::vector<float> l0(l);
  d.process(l.data(), r.data(), N);
  double worst = 0;
  for (int i = 3; i < N; ++i) worst = std::max(worst, (double)std::fabs(l[i] - l0[i - 3]));
  CHECK(worst < 0.05);   // the delayed input (the compensation is close to 1 for this model)
}
} // namespace

int main() {
  std::printf("nam_drive async tests\n");
  test_requestLoad_reaches_Loaded();
  test_requestLoad_missing_file_fails_and_passes_through();
  test_requestLoad_garbage_file_fails();
  test_last_request_wins();
  test_requestLoad_empty_path_unloads_without_error();
  test_load_callback_fires_when_a_request_completes();
  test_process_during_load_is_finite();
  test_compensation_matches_undriven_input_level();
  test_same_rate_keeps_model_state();
  test_different_channels_stay_apart_and_match_mono_runs();
  test_identical_channels_run_one_instance();
  test_switching_identical_and_different_is_seamless();
  test_compensation_is_linked();
  test_async_load_builds_both_instances();
  test_stereo_process_during_load_is_finite();
  test_long_block_is_processed_in_pieces();
  std::printf(g_failed ? "FAILED (%d)\n" : "All nam_drive async tests passed\n", g_failed);
  return g_failed ? 1 : 0;
}
