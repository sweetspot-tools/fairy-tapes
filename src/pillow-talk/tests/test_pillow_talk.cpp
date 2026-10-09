/**
 * Fairy Tapes · Pillow Talk — Unit tests for params and pure helpers.
 * Run via CTest; build target: pillow_talk_tests.
 */

#include "params.h"
#include "dsp/ducker.h"
#include "dsp/tape_echo.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>

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
  const float g = runSine(d, 1000.0, 0.f, 200.0 + 3 * 300.0, ph);
  ASSERT_TRUE(g > 0.95f);
}

void test_ducker_hold_timing_is_level_independent() {
  for (float amp : {0.05f, 1.0f}) {
    auto d = makeDucker();
    double ph = 0;
    runSine(d, 1000.0, amp, 300.0, ph);
    const float duck = (float)std::pow(10.0, -30.0 / 20.0);
    const float early = runSine(d, 1000.0, 0.f, 180.0, ph);  // H - 20 ms
    ASSERT_TRUE(early < duck * 1.05f);
    const float late = runSine(d, 1000.0, 0.f, 60.0, ph);    // H + 40 ms
    ASSERT_TRUE(late > duck * 1.3f);
  }
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

// ---- TapeEcho --------------------------------------------------------------
using Buf = std::vector<float>;

DSP::TapeEcho makeEcho(float timeMs = 100.f, float feedback = 0.f, float toneHz = 12000.f,
                       float saturation = 0.f) {
  DSP::TapeEcho e;
  e.prepare(kSr, 512);
  e.setParams(timeMs, feedback, toneHz, saturation, 0.f);
  return e;
}

// Processes a mono input (copied to both channels) in blocks; returns left output.
Buf runEcho(DSP::TapeEcho& e, const Buf& in, const Buf& gain, int block = 512) {
  Buf l(in), r(in);
  for (size_t i = 0; i < in.size(); i += block) {
    const int n = (int)std::min<size_t>(block, in.size() - i);
    e.process(l.data() + i, r.data() + i, gain.data() + i, n);
  }
  return l;
}

Buf impulse(size_t n) { Buf b(n, 0.f); b[0] = 1.f; return b; }

Buf whiteNoise(size_t n, uint32_t seed = 1u) {
  Buf b(n);
  for (auto& v : b) {
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    v = (float)((seed >> 8) * (1.0 / 16777216.0) * 2.0 - 1.0) * 0.5f;
  }
  return b;
}

size_t argmaxAbs(const Buf& b, size_t from, size_t to) {
  size_t k = from;
  for (size_t i = from; i < to; ++i) if (std::fabs(b[i]) > std::fabs(b[k])) k = i;
  return k;
}

void test_echo_first_repeat_at_time() {
  auto e = makeEcho(100.f);
  const Buf out = runEcho(e, impulse(9000), Buf(9000, 1.f));
  const long k = (long)argmaxAbs(out, 0, out.size());
  ASSERT_TRUE(std::labs(k - 4800) <= 2);
}

void test_echo_feedback_decays() {
  auto e = makeEcho(100.f, 0.5f);
  const Buf out = runEcho(e, impulse(4800 * 6), Buf(4800 * 6, 1.f));
  float prev = 0.f;
  for (int rep = 1; rep <= 5; ++rep) {
    const size_t c = 4800 * rep;
    const float p = std::fabs(out[argmaxAbs(out, c - 200, c + 200)]);
    ASSERT_TRUE(p > 1e-4f);
    if (rep > 1) {
      const float ratio = p / prev;
      ASSERT_TRUE(ratio > 0.3f && ratio < 0.6f);
    }
    prev = p;
  }
}

void test_echo_zero_send_gain_is_silent() {
  auto e = makeEcho(100.f, 0.5f);
  const Buf out = runEcho(e, whiteNoise(48000), Buf(48000, 0.f));
  for (float v : out) ASSERT_TRUE(std::fabs(v) < 1e-6f);
}

void test_echo_send_gain_not_applied_to_tail() {
  const size_t n = 4800 * 3;
  auto ref = makeEcho(100.f, 0.5f);
  const Buf expected = runEcho(ref, impulse(n), Buf(n, 1.f));

  // Same impulse at gain 1, then loud noise at gain 0: tail must be untouched.
  Buf in = whiteNoise(n);
  in[0] = 1.f;
  Buf gain(n, 0.f);
  gain[0] = 1.f;
  auto e = makeEcho(100.f, 0.5f);
  const Buf out = runEcho(e, in, gain);
  ASSERT_TRUE(std::fabs(out[argmaxAbs(out, 4600, 5000)]) > 0.3f);
  for (size_t i = 0; i < n; ++i) ASSERT_TRUE(std::fabs(out[i] - expected[i]) < 1e-6f);
}

double energyAbove6k(const Buf& b, size_t from) {
  // 8th-order-ish high-pass at 6 kHz: four cascaded 2nd-order Butterworth sections.
  double e = 0.0;
  const double w = 2.0 * kPi * 6000.0 / kSr, c = std::cos(w), al = std::sin(w) / (2.0 * 0.7071);
  const double a0 = 1.0 + al;
  const double b0 = (1.0 + c) / 2.0 / a0, b1 = -(1.0 + c) / a0, a1 = -2.0 * c / a0, a2 = (1.0 - al) / a0;
  double z[4][2] = {};
  for (size_t i = 0; i < b.size(); ++i) {
    double x = b[i];
    for (auto& s : z) {
      const double y = b0 * x + s[0];
      s[0] = b1 * x - a1 * y + s[1];
      s[1] = b0 * x - a2 * y;
      x = y;
    }
    if (i >= from) e += x * x;
  }
  return e;
}

void test_echo_tone_darkens() {
  const Buf noise = whiteNoise(48000);
  auto bright = makeEcho(100.f, 0.3f, 12000.f);
  auto dark = makeEcho(100.f, 0.3f, 800.f);
  const double eb = energyAbove6k(runEcho(bright, noise, Buf(48000, 1.f)), 4800);
  const double ed = energyAbove6k(runEcho(dark, noise, Buf(48000, 1.f)), 4800);
  ASSERT_TRUE(eb > 0.0);
  ASSERT_TRUE(ed < eb * 0.1);
}

void test_echo_max_feedback_is_bounded() {
  auto e = makeEcho(100.f, 0.95f, 12000.f, 1.0f);
  const size_t n = (size_t)kSr * 10;
  Buf in(n);
  for (size_t i = 0; i < n; ++i) in[i] = (float)std::sin(2.0 * kPi * 220.0 * i / kSr);
  const Buf out = runEcho(e, in, Buf(n, 1.f));
  for (float v : out) {
    ASSERT_TRUE(!std::isnan(v));
    ASSERT_TRUE(std::fabs(v) < 4.0f);
  }
}

void test_echo_silence_stays_silent() {
  // An impulse with heavy feedback, then 60 s of zeros: the tail must decay to
  // exact zeros without ever producing subnormal values.
  auto e = makeEcho(100.f, 0.9f, 12000.f, 0.5f);
  const size_t n = (size_t)kSr * 60;
  const Buf out = runEcho(e, impulse(n), Buf(n, 1.f));
  for (float v : out) ASSERT_TRUE(std::fpclassify(v) != FP_SUBNORMAL);
  for (size_t i = n - (size_t)kSr * 5; i < n; ++i) ASSERT_TRUE(out[i] == 0.f);
}

void test_echo_zero_and_one_sample_blocks() {
  auto e = makeEcho(100.f, 0.5f);
  float l = 0.25f, r = -0.5f, g = 1.f;
  e.process(&l, &r, &g, 0);
  ASSERT_TRUE(l == 0.25f && r == -0.5f);

  const size_t n = 4800 * 3;
  const Buf in = whiteNoise(n);
  auto big = makeEcho(100.f, 0.5f);
  auto one = makeEcho(100.f, 0.5f);
  const Buf a = runEcho(big, in, Buf(n, 1.f), 512);
  const Buf b = runEcho(one, in, Buf(n, 1.f), 1);
  for (size_t i = 0; i < n; ++i) ASSERT_TRUE(a[i] == b[i]);
}

void test_echo_time_change_has_no_click() {
  auto e = makeEcho(100.f, 0.3f);
  const size_t n = (size_t)kSr * 2, half = n / 2;
  Buf l(n), r(n);
  const Buf g(n, 1.f);
  for (size_t i = 0; i < n; ++i) l[i] = r[i] = (float)(0.5 * std::sin(2.0 * kPi * 440.0 * i / kSr));
  bool changed = false;
  for (size_t i = 0; i < n; i += 256) {
    if (i <= half && half < i + 256) {
      e.setParams(400.f, 0.3f, 12000.f, 0.f, 0.f);
      changed = true;
    }
    e.process(l.data() + i, r.data() + i, g.data() + i, 256);
  }
  ASSERT_TRUE(changed);
  float maxDiff = 0.f;
  for (size_t i = 1; i < n; ++i) maxDiff = std::max(maxDiff, std::fabs(l[i] - l[i - 1]));
  ASSERT_TRUE(maxDiff < 0.5f);
}

void test_echo_reprepare_at_new_rate() {
  auto e = makeEcho(100.f, 0.5f);
  runEcho(e, whiteNoise(9600), Buf(9600, 1.f));
  e.prepare(96000.0, 512);
  e.setParams(100.f, 0.f, 12000.f, 0.f, 0.f);
  const Buf out = runEcho(e, impulse(12000), Buf(12000, 1.f));
  const long k = (long)argmaxAbs(out, 0, out.size());
  ASSERT_TRUE(std::labs(k - 9600) <= 4);
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
  RUN_TEST(test_ducker_hold_timing_is_level_independent);
  RUN_TEST(test_ducker_full_depth_is_mute);
  RUN_TEST(test_ducker_below_threshold_ignored);
  RUN_TEST(test_ducker_ignores_sub_80hz_rumble);
  RUN_TEST(test_echo_first_repeat_at_time);
  RUN_TEST(test_echo_feedback_decays);
  RUN_TEST(test_echo_zero_send_gain_is_silent);
  RUN_TEST(test_echo_send_gain_not_applied_to_tail);
  RUN_TEST(test_echo_tone_darkens);
  RUN_TEST(test_echo_max_feedback_is_bounded);
  RUN_TEST(test_echo_silence_stays_silent);
  RUN_TEST(test_echo_zero_and_one_sample_blocks);
  RUN_TEST(test_echo_time_change_has_no_click);
  RUN_TEST(test_echo_reprepare_at_new_rate);

  std::fprintf(stdout, "\n%d passed, %d failed.\n", g_passed, g_failed);
  if (g_failed > 0) {
    std::fprintf(stderr, "%d test(s) FAILED.\n", g_failed);
    return EXIT_FAILURE;
  }
  std::fprintf(stdout, "All tests passed.\n");
  return EXIT_SUCCESS;
}
