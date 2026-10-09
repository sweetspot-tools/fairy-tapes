/**
 * Fairy Tapes · Pillow Talk — Unit tests for params and pure helpers.
 * Run via CTest; build target: pillow_talk_tests.
 */

#include "params.h"
#include "dsp/ducker.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

constexpr double kEps = 1e-6;
int g_failed = 0;
int g_passed = 0;

bool approx(double a, double b, double eps = kEps) {
  return std::fabs(a - b) <= eps;
}

#define ASSERT_TRUE(expr) do { \
  if (!(expr)) { \
    std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); \
    ++g_failed; return; \
  } \
} while(0)

#define ASSERT_APPROX(a, b) do { \
  if (!approx((a), (b))) { \
    std::fprintf(stderr, "  FAIL [%s:%d]: %s ~= %s (got %.9f vs %.9f)\n", \
                 __FILE__, __LINE__, #a, #b, (double)(a), (double)(b)); \
    ++g_failed; return; \
  } \
} while(0)

#define RUN_TEST(fn) do { \
  fn(); \
  ++g_passed; \
} while(0)

using namespace FairyTapes::PillowTalk;

void test_noteToMs_120bpm() {
  ASSERT_APPROX(noteToMs(0, 120.0), 125.0);
  ASSERT_APPROX(noteToMs(1, 120.0), 250.0);
  ASSERT_APPROX(noteToMs(2, 120.0), 500.0);
  ASSERT_APPROX(noteToMs(3, 120.0), 1000.0);
}

void test_noteToMs_bpm_zero_uses_120() {
  ASSERT_APPROX(noteToMs(2, 0.0), 500.0);
  ASSERT_APPROX(noteToMs(1, -10.0), 250.0);
}

void test_ranges_boundaries() {
  ASSERT_APPROX(timeMsFromNorm(0.0), 20.0);
  ASSERT_APPROX(timeMsFromNorm(1.0), 1200.0);
  ASSERT_APPROX(holdMsFromNorm(0.0), 50.0);
  ASSERT_APPROX(holdMsFromNorm(1.0), 1000.0);
  ASSERT_APPROX(releaseMsFromNorm(0.0), 50.0);
  ASSERT_APPROX(releaseMsFromNorm(1.0), 2000.0);
  ASSERT_APPROX(feedbackFromNorm(0.0), 0.0);
  ASSERT_APPROX(feedbackFromNorm(1.0), 0.95);
  ASSERT_APPROX(toneHzFromNorm(0.0), 800.0);
  ASSERT_APPROX(toneHzFromNorm(1.0), 12000.0);
  ASSERT_APPROX(thresholdDbFromNorm(0.0), -60.0);
  ASSERT_APPROX(thresholdDbFromNorm(1.0), 0.0);
  ASSERT_APPROX(duckDepthDbFromNorm(0.0), 0.0);
}

void test_duckDepth_full_is_mute_constant() {
  ASSERT_TRUE(duckDepthDbFromNorm(1.0) == kDuckMuteDb);
}

void test_param_enum_count() {
  ASSERT_TRUE(kNumParams == 16);
}

void test_feedback_never_exceeds_095() {
  for (int i = 0; i <= 100; ++i)
    ASSERT_TRUE(feedbackFromNorm(i / 100.0) <= 0.95 + 1e-12);
}

// ---- Ducker ----------------------------------------------------------------
constexpr double kSr = 48000.0;
constexpr double kPi = 3.14159265358979323846;

DSP::Ducker makeDucker(float depthDb = 30.f) {
  DSP::Ducker d;
  d.prepare(kSr);
  d.setParams(-40.f, depthDb, 200.f, 300.f);
  return d;
}

// Runs `ms` of sine (amp 0 = silence); returns last gain, tracks min gain.
float runSine(DSP::Ducker& d, double freq, float amp, double ms, double& phase, float* minGain = nullptr) {
  const int n = (int)(kSr * ms / 1000.0);
  float g = 1.f;
  for (int i = 0; i < n; ++i) {
    const float x = amp * (float)std::sin(phase);
    phase += 2.0 * kPi * freq / kSr;
    g = d.processSample(x, x);
    if (minGain && g < *minGain) *minGain = g;
  }
  return g;
}

void test_ducker_silence_gain_is_unity() {
  auto d = makeDucker();
  for (int i = 0; i < 4800; ++i) ASSERT_TRUE(d.processSample(0.f, 0.f) == 1.f);
}

void test_ducker_burst_ducks_to_depth() {
  auto d = makeDucker();
  double ph = 0;
  const float g = runSine(d, 1000.0, 0.5f, 300.0, ph);
  ASSERT_TRUE(std::fabs(g - std::pow(10.0, -30.0 / 20.0)) < 0.02);
}

void test_ducker_holds_after_burst() {
  auto d = makeDucker();
  double ph = 0;
  runSine(d, 1000.0, 0.5f, 300.0, ph);
  const float g = runSine(d, 1000.0, 0.f, 100.0, ph);
  ASSERT_TRUE(g < 0.1f);
}

void test_ducker_reopens_after_hold_and_release() {
  auto d = makeDucker();
  double ph = 0;
  runSine(d, 1000.0, 0.5f, 300.0, ph);
  const float g = runSine(d, 1000.0, 0.f, 200.0 + 4 * 300.0, ph);
  ASSERT_TRUE(g > 0.95f);
}

void test_ducker_full_depth_is_mute() {
  auto d = makeDucker((float)kDuckMuteDb);
  double ph = 0;
  const float g = runSine(d, 1000.0, 0.5f, 300.0, ph);
  ASSERT_TRUE(g < 1e-4f);
}

void test_ducker_below_threshold_ignored() {
  auto d = makeDucker();
  double ph = 0;
  float mn = 1.f;
  runSine(d, 1000.0, 0.001f, 300.0, ph, &mn);
  ASSERT_TRUE(mn > 0.999f);
}

void test_ducker_ignores_sub_80hz_rumble() {
  auto d = makeDucker();
  float mn = 1.f;
  // 50 ms raised-cosine fade-in: a hard onset is a broadband transient, not rumble.
  const int n = (int)(kSr * 0.5), fade = (int)(kSr * 0.05);
  for (int i = 0; i < n; ++i) {
    const double env = i < fade ? 0.5 * (1.0 - std::cos(kPi * i / fade)) : 1.0;
    const float x = (float)(0.5 * env * std::sin(2.0 * kPi * 40.0 * i / kSr));
    mn = std::min(mn, d.processSample(x, x));
  }
  ASSERT_TRUE(mn > std::pow(10.0, -3.0 / 20.0));
}

} // namespace

int main() {
  std::fprintf(stdout, "Running Pillow Talk tests...\n\n");

  RUN_TEST(test_noteToMs_120bpm);
  RUN_TEST(test_noteToMs_bpm_zero_uses_120);
  RUN_TEST(test_ranges_boundaries);
  RUN_TEST(test_duckDepth_full_is_mute_constant);
  RUN_TEST(test_param_enum_count);
  RUN_TEST(test_feedback_never_exceeds_095);
  RUN_TEST(test_ducker_silence_gain_is_unity);
  RUN_TEST(test_ducker_burst_ducks_to_depth);
  RUN_TEST(test_ducker_holds_after_burst);
  RUN_TEST(test_ducker_reopens_after_hold_and_release);
  RUN_TEST(test_ducker_full_depth_is_mute);
  RUN_TEST(test_ducker_below_threshold_ignored);
  RUN_TEST(test_ducker_ignores_sub_80hz_rumble);

  std::fprintf(stdout, "\n%d passed, %d failed.\n", g_passed, g_failed);
  if (g_failed > 0) {
    std::fprintf(stderr, "%d test(s) FAILED.\n", g_failed);
    return EXIT_FAILURE;
  }
  std::fprintf(stdout, "All tests passed.\n");
  return EXIT_SUCCESS;
}
