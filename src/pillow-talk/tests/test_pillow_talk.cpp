/**
 * Fairy Tapes · Pillow Talk — Unit tests for params and pure helpers.
 * Run via CTest; build target: pillow_talk_tests.
 */

#include "params.h"
#include "dsp/ducker.h"
#include "dsp/tape_echo.h"
#include "engine.h"
#include "state.h"
#include "fairy_ui/knob_math.h"
#include "ui/editor_size.h"
#include "ui/history.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <vector>

// Counts heap allocations while g_countAllocs is set (no-allocation-in-process checks).
static std::atomic<bool> g_countAllocs{false};
static std::atomic<int> g_allocs{0};
void* operator new(std::size_t n) {
  if (g_countAllocs) ++g_allocs;
  if (void* p = std::malloc(n ? n : 1)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

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


// ---- State -----------------------------------------------------------------
void test_state_roundtrip() {
  double a[kNumParams], b[kNumParams];
  for (int i = 0; i < (int)kNumParams; ++i) { a[i] = (i + 1) / 17.0; b[i] = -1.0; }
  std::vector<uint8_t> bytes;
  serializeState(a, bytes);
  ASSERT_TRUE(bytes.size() == kStateSize);
  ASSERT_TRUE(deserializeState(bytes.data(), bytes.size(), b));
  for (int i = 0; i < (int)kNumParams; ++i) ASSERT_TRUE(a[i] == b[i]);
}

void test_state_rejects_unknown_version_and_truncation() {
  double a[kNumParams], b[kNumParams];
  for (int i = 0; i < (int)kNumParams; ++i) { a[i] = 0.25; b[i] = 0.75; }
  std::vector<uint8_t> bytes;
  serializeState(a, bytes);
  std::vector<uint8_t> future(bytes);
  future[0] = (uint8_t)(kStateVersion + 1);
  ASSERT_TRUE(!deserializeState(future.data(), future.size(), b));
  ASSERT_TRUE(!deserializeState(bytes.data(), bytes.size() - 1, b));
  for (int i = 0; i < (int)kNumParams; ++i) ASSERT_TRUE(b[i] == 0.75);
}

void test_state_rejects_non_finite_values() {
  const double bad[] = {std::nan(""), INFINITY, -INFINITY};
  for (double v : bad) {
    double a[kNumParams], b[kNumParams];
    for (int i = 0; i < (int)kNumParams; ++i) { a[i] = 0.25; b[i] = 0.75; }
    a[kTone] = v;
    std::vector<uint8_t> bytes;
    serializeState(a, bytes);
    ASSERT_TRUE(!deserializeState(bytes.data(), bytes.size(), b));
    for (int i = 0; i < (int)kNumParams; ++i) ASSERT_TRUE(b[i] == 0.75);
  }
}

void test_state_clamps_out_of_range_values() {
  double a[kNumParams], b[kNumParams];
  for (int i = 0; i < (int)kNumParams; ++i) a[i] = 0.5;
  a[kMix] = 5.0;
  a[kFeedback] = -1.0;
  std::vector<uint8_t> bytes;
  serializeState(a, bytes);
  ASSERT_TRUE(deserializeState(bytes.data(), bytes.size(), b));
  ASSERT_TRUE(b[kMix] == 1.0);
  ASSERT_TRUE(b[kFeedback] == 0.0);
  ASSERT_TRUE(b[kTone] == 0.5);
}

// ---- Parameter mapping ------------------------------------------------------
void test_note_index_matches_string_list() {
  for (int k = 0; k <= 3; ++k) ASSERT_TRUE(noteIndexFromNorm(k / 3.0) == k);
  ASSERT_TRUE(noteIndexFromNorm(1.0) == 3);
}

void test_map_params_free_and_sync() {
  double p[kNumParams];
  loadDefaults(p);
  p[kTimeMs] = 0.0;
  p[kHoldMs] = 1.0;
  Settings s = mapParams(p, 90.0);
  ASSERT_APPROX(s.timeMs, 20.0);
  ASSERT_APPROX(s.holdMs, 1000.0);
  p[kTimeSync] = 1.0; p[kTimeNote] = 2.0 / 3.0;   // 1/4
  p[kHoldSync] = 1.0; p[kHoldNote] = 1.0 / 3.0;   // 1/8
  s = mapParams(p, 60.0);
  ASSERT_APPROX(s.timeMs, 1000.0);
  ASSERT_APPROX(s.holdMs, 500.0);
  s = mapParams(p, 0.0);  // no host tempo -> 120 BPM
  ASSERT_APPROX(s.timeMs, 500.0);
  ASSERT_TRUE(!s.bypass);
  p[kBypass] = 1.0;
  ASSERT_TRUE(mapParams(p, 120.0).bypass);
}

// ---- Sidechain routing -------------------------------------------------------
bool settingsFinite(const Settings& s) {
  for (float x : {s.timeMs, s.feedback, s.toneHz, s.saturation, s.wowFlutter, s.mix,
                  s.thresholdDb, s.depthDb, s.holdMs, s.releaseMs})
    if (!std::isfinite(x)) return false;
  return true;
}

void test_map_params_never_returns_non_finite() {
  double p[kNumParams];
  for (double v : {std::nan(""), (double)INFINITY, -(double)INFINITY, 5.0, -1.0}) {
    for (int i = 0; i < (int)kNumParams; ++i) p[i] = v;
    for (double bpm : {120.0, std::nan(""), (double)INFINITY, 0.0})
      ASSERT_TRUE(settingsFinite(mapParams(p, bpm)));
  }
}

void test_detector_falls_back_to_main_input() {
  float a[4] = {}, b[4] = {};
  float* both[2] = {a, b};
  float* nulls[2] = {nullptr, nullptr};
  SidechainInput sc = pickSidechain(false, true, 2, both);
  ASSERT_TRUE(sc.L == nullptr && sc.R == nullptr);       // switch off
  sc = pickSidechain(true, false, 2, both);
  ASSERT_TRUE(sc.L == nullptr && sc.R == nullptr);       // bus inactive
  sc = pickSidechain(true, true, 0, both);
  ASSERT_TRUE(sc.L == nullptr && sc.R == nullptr);       // zero channels
  sc = pickSidechain(true, true, 2, nullptr);
  ASSERT_TRUE(sc.L == nullptr && sc.R == nullptr);       // no buffer array
  sc = pickSidechain(true, true, 2, nulls);
  ASSERT_TRUE(sc.L == nullptr && sc.R == nullptr);       // null channel buffers
  sc = pickSidechain(true, true, 1, both);
  ASSERT_TRUE(sc.L == a && sc.R == a);                   // mono sidechain
  sc = pickSidechain(true, true, 2, both);
  ASSERT_TRUE(sc.L == a && sc.R == b);
}

// ---- Engine (the Processor's signal chain) ----------------------------------
Settings echoSettings(float mix = 1.f) {
  Settings s;
  s.timeMs = 100.f; s.feedback = 0.f; s.toneHz = 12000.f; s.saturation = 0.f; s.wowFlutter = 0.f;
  s.mix = mix; s.thresholdDb = -40.f; s.depthDb = 30.f; s.holdMs = 200.f; s.releaseMs = 300.f;
  s.bypass = false;
  return s;
}

Buf sine(size_t n, double freq, float amp) {
  Buf b(n);
  for (size_t i = 0; i < n; ++i) b[i] = amp * (float)std::sin(2.0 * kPi * freq * i / kSr);
  return b;
}

double energy(const Buf& b, size_t from, size_t to) {
  double e = 0.0;
  for (size_t i = from; i < to; ++i) e += (double)b[i] * b[i];
  return e;
}

// Runs the engine in blocks on separate in/out buffers; returns {L, R}.
std::pair<Buf, Buf> runEngine(Engine& e, const Buf& inL, const Buf& inR, const Buf* sc, int block = 512) {
  Buf outL(inL.size()), outR(inR.size());
  for (size_t i = 0; i < inL.size(); i += block) {
    const int n = (int)std::min<size_t>(block, inL.size() - i);
    const float* s = sc ? sc->data() + i : nullptr;
    e.process(inL.data() + i, inR.data() + i, s, s, outL.data() + i, outR.data() + i, n);
  }
  return {outL, outR};
}

void test_chain_sidechain_ducks_echo() {
  const size_t n = (size_t)kSr / 2;
  const Buf voice = sine(n, 1000.0, 0.5f);
  const Buf quiet(n, 0.f);
  Engine ducked, open;
  ducked.prepare(kSr, 512);
  open.prepare(kSr, 512);
  ducked.setSettings(echoSettings());
  open.setSettings(echoSettings());
  const Buf wetDucked = runEngine(ducked, voice, voice, &voice).first;
  const Buf wetOpen = runEngine(open, voice, voice, &quiet).first;
  const double ed = energy(wetDucked, 4800, n), eo = energy(wetOpen, 4800, n);
  ASSERT_TRUE(eo > 0.0);
  ASSERT_TRUE(ed < eo * 0.01);  // 30 dB of ducking on the send
}

// R7: the ducker gain scales the wet OUTPUT; the phrase is still recorded on tape,
// so its repeats surface in the pause once hold and release have elapsed.
void test_chain_phrase_tail_heard_in_pause() {
  const size_t phrase = (size_t)(kSr * 1.35), n = phrase + (size_t)kSr * 2;
  Buf in(n, 0.f);
  for (size_t i = 0; i < phrase; ++i)
    in[i] = (float)(0.3 * std::sin(2.0 * kPi * 440.0 * i / kSr) *
                    (0.75 + 0.25 * std::sin(2.0 * kPi * 3.0 * i / kSr)));
  double p[kNumParams];
  loadDefaults(p);
  p[kMix] = 1.0;
  p[kWowFlutter] = 0.0;  // identical delay modulation is not needed but keeps runs comparable
  Engine ducked, open;
  ducked.prepare(kSr, 512);
  open.prepare(kSr, 512);
  ducked.setSettings(mapParams(p, 120.0));
  p[kDuckDepth] = 0.0;
  open.setSettings(mapParams(p, 120.0));
  const Buf wd = runEngine(ducked, in, in, nullptr).first;
  const Buf wo = runEngine(open, in, in, nullptr).first;
  const auto db = [](double a, double b) { return 10.0 * std::log10(a / b); };
  // During the phrase (after the first repeat): reduced by about Depth (36 dB).
  const size_t from = (size_t)(kSr * 0.3);
  const double during = db(energy(wd, from, phrase), energy(wo, from, phrase));
  std::fprintf(stdout, "  phrase: wet during %.1f dB", during);
  ASSERT_TRUE(during < -30.0 && during > -42.0);
  // Pause, once Hold (224 ms) and one Release time constant (316 ms) have elapsed:
  // the repeats still on tape are within ~6 dB of the no-ducking baseline.
  const size_t reopen = phrase + (size_t)(kSr * (0.224 + 0.316)), end = phrase + (size_t)kSr;
  const double pause = db(energy(wd, reopen, end), energy(wo, reopen, end));
  const double whole = db(energy(wd, phrase, n), energy(wo, phrase, n));
  const double absPause = db(energy(wd, reopen, end) / (double)(end - reopen),
                             energy(in, 0, phrase) / (double)phrase);
  std::fprintf(stdout, ", pause after reopen %.1f dB (abs %.1f dB re phrase), whole pause %.1f dB\n",
               pause, absPause, whole);
  ASSERT_TRUE(energy(wo, reopen, end) > 0.0);
  ASSERT_TRUE(pause > -6.0);
  ASSERT_TRUE(absPause > -40.0);
}

void test_engine_mix_formula() {
  const size_t n = 9600;
  const Buf in = whiteNoise(n);
  Engine wet, half;
  wet.prepare(kSr, 512);
  half.prepare(kSr, 512);
  wet.setSettings(echoSettings(1.f));
  half.setSettings(echoSettings(0.5f));
  const Buf w = runEngine(wet, in, in, nullptr).first;
  const Buf h = runEngine(half, in, in, nullptr).first;
  for (size_t i = 0; i < n; ++i) ASSERT_TRUE(std::fabs(h[i] - (0.5f * in[i] + 0.5f * w[i])) < 1e-6f);
}

void test_engine_bypass_passes_dry() {
  const size_t n = 9600;
  const Buf in = whiteNoise(n);
  Engine e;
  e.prepare(kSr, 512);
  Settings s = echoSettings(0.7f);
  s.bypass = true;
  e.setSettings(s);
  const auto out = runEngine(e, in, in, nullptr);
  for (size_t i = 0; i < n; ++i) ASSERT_TRUE(out.first[i] == in[i] && out.second[i] == in[i]);

  Engine dry;  // Mix 0 is the dry signal too
  dry.prepare(kSr, 512);
  dry.setSettings(echoSettings(0.f));
  const auto out0 = runEngine(dry, in, in, nullptr);
  for (size_t i = 0; i < n; ++i) ASSERT_TRUE(out0.first[i] == in[i] && out0.second[i] == in[i]);
}

void test_engine_in_place_mono_gives_identical_channels() {
  const size_t n = 4800 * 4;  // long enough for a second-generation echo
  const Buf in = whiteNoise(n);
  Settings fb = echoSettings(0.5f);
  fb.feedback = 0.5f;
  // Independent reference: Ducker on the main input, TapeEcho on separate L/R buffers.
  DSP::Ducker d;
  d.prepare(kSr);
  d.setParams(fb.thresholdDb, fb.depthDb, fb.holdMs, fb.releaseMs);
  auto t = makeEcho(fb.timeMs, fb.feedback);
  Buf g(n), wl(in), wr(in), expected(n);
  for (size_t i = 0; i < n; ++i) g[i] = d.processSample(in[i], in[i]);
  for (size_t i = 0; i < n; i += 512) {
    const int k = (int)std::min<size_t>(512, n - i);
    t.process(wl.data() + i, wr.data() + i, nullptr, k);  // tape input fully open
  }
  for (size_t i = 0; i < n; ++i) expected[i] = 0.5f * in[i] + 0.5f * (wl[i] * g[i]);
  Engine e;
  e.prepare(kSr, 512);
  e.setSettings(fb);
  // Host processes in place: input and output buffers alias, both channels carry the same content.
  Buf L(in), R(in);
  for (size_t i = 0; i < n; i += 512) {
    const int k = (int)std::min<size_t>(512, n - i);
    e.process(L.data() + i, R.data() + i, nullptr, nullptr, L.data() + i, R.data() + i, k);
  }
  for (size_t i = 0; i < n; ++i) ASSERT_TRUE(L[i] == R[i] && std::fabs(L[i] - expected[i]) < 1e-6f);
  // Same input pointer for both channels (mono host), separate outputs.
  Engine m;
  m.prepare(kSr, 512);
  m.setSettings(fb);
  Buf oL(n), oR(n);
  for (size_t i = 0; i < n; i += 512) {
    const int k = (int)std::min<size_t>(512, n - i);
    m.process(in.data() + i, in.data() + i, nullptr, nullptr, oL.data() + i, oR.data() + i, k);
  }
  for (size_t i = 0; i < n; ++i) ASSERT_TRUE(oL[i] == oR[i] && std::fabs(oL[i] - expected[i]) < 1e-6f);
  // One buffer is both inputs and the left output; right output separate.
  Engine s;
  s.prepare(kSr, 512);
  s.setSettings(fb);
  Buf io(in), sR(n);
  for (size_t i = 0; i < n; i += 512) {
    const int k = (int)std::min<size_t>(512, n - i);
    s.process(io.data() + i, io.data() + i, nullptr, nullptr, io.data() + i, sR.data() + i, k);
  }
  for (size_t i = 0; i < n; ++i) ASSERT_TRUE(io[i] == sR[i] && std::fabs(io[i] - expected[i]) < 1e-6f);
}

void test_engine_zero_one_and_oversized_blocks() {
  Engine e;
  e.prepare(kSr, 64);
  e.setSettings(echoSettings(0.5f));
  float l = 0.25f, r = -0.5f;
  e.process(&l, &r, nullptr, nullptr, &l, &r, 0);
  ASSERT_TRUE(l == 0.25f && r == -0.5f);

  const size_t n = 9600;
  const Buf in = whiteNoise(n);
  Engine big, one, over;
  big.prepare(kSr, 512);
  one.prepare(kSr, 512);
  over.prepare(kSr, 64);  // host sends blocks larger than announced
  for (Engine* x : {&big, &one, &over}) x->setSettings(echoSettings(0.5f));
  const Buf a = runEngine(big, in, in, nullptr, 512).first;
  const Buf b = runEngine(one, in, in, nullptr, 1).first;
  const Buf c = runEngine(over, in, in, nullptr, 512).first;
  for (size_t i = 0; i < n; ++i) ASSERT_TRUE(a[i] == b[i] && a[i] == c[i]);
}

void test_engine_silence_stays_silent() {
  Engine e;
  e.prepare(kSr, 512);
  Settings s = echoSettings(0.5f);
  s.feedback = 0.9f; s.saturation = 0.5f; s.wowFlutter = 1.f;
  e.setSettings(s);
  const size_t n = (size_t)kSr * 5;
  const Buf z(n, 0.f);
  const auto out = runEngine(e, z, z, &z);
  for (size_t i = 0; i < n; ++i) ASSERT_TRUE(out.first[i] == 0.f && out.second[i] == 0.f);
}

void test_engine_reprepare_at_new_rate() {
  Engine e;
  e.prepare(kSr, 512);
  e.setSettings(echoSettings(1.f));
  runEngine(e, whiteNoise(9600), whiteNoise(9600), nullptr);
  e.prepare(96000.0, 512);
  e.setSettings(echoSettings(1.f));
  const Buf out = runEngine(e, impulse(12000), impulse(12000), nullptr).first;
  ASSERT_TRUE(std::labs((long)argmaxAbs(out, 0, out.size()) - 9600) <= 4);
}

void test_engine_ignores_non_finite_settings() {
  const size_t n = 9600;
  const Buf in = whiteNoise(n);
  Engine ref, e;
  ref.prepare(kSr, 512);
  e.prepare(kSr, 512);
  ref.setSettings(echoSettings(0.5f));
  e.setSettings(echoSettings(0.5f));
  Settings bad = echoSettings(0.5f);
  bad.timeMs = std::nanf("");
  bad.feedback = INFINITY;
  e.setSettings(bad);
  const Buf a = runEngine(ref, in, in, nullptr).first;
  const Buf b = runEngine(e, in, in, nullptr).first;
  for (size_t i = 0; i < n; ++i) ASSERT_TRUE(std::isfinite(b[i]) && a[i] == b[i]);
}

void test_engine_process_does_not_allocate() {
  Engine e;
  e.prepare(kSr, 512);
  Settings s = echoSettings(0.5f);
  e.setSettings(s);
  const Buf in = whiteNoise(4096);
  Buf outL(4096), outR(4096);
  g_allocs = 0;
  g_countAllocs = true;
  for (int i = 0; i < 4096; i += 512) {
    s.timeMs = 100.f + (float)i;  // parameter changes between blocks
    e.setSettings(s);
    e.process(in.data() + i, in.data() + i, in.data() + i, in.data() + i, outL.data() + i, outR.data() + i, 512);
  }
  e.process(in.data(), in.data(), nullptr, nullptr, outL.data(), outR.data(), 4096);  // oversized block
  g_countAllocs = false;
  ASSERT_TRUE(g_allocs == 0);
}

// ---- Task 2: display values --------------------------------------------------------
void test_display_silence_is_floor_and_unity_send() {
  Engine e;
  e.prepare(kSr, 512);
  e.setSettings(echoSettings());
  const Buf z(4800, 0.f);
  runEngine(e, z, z, nullptr);
  const auto d = e.lastDisplay();
  ASSERT_TRUE(d.voiceDb <= -60.f);
  ASSERT_APPROX(d.sendGain, 1.0);
  ASSERT_TRUE(d.echoDb <= -60.f);
}

void test_display_voice_follows_signal() {
  Engine e;
  e.prepare(kSr, 512);
  e.setSettings(echoSettings());
  const float amp = (float)std::pow(10.0, -12.0 / 20.0);
  const Buf s = sine(9600, 1000.0, amp);
  runEngine(e, s, s, nullptr);
  // Voice shows the detector envelope (rectified, smoothed): a sine reads its mean
  // rectified level, 20*log10(2/pi) = -3.9 dB below its peak.
  const float expected = -12.f + 20.f * std::log10(2.f / (float)kPi);
  ASSERT_TRUE(std::fabs(e.lastDisplay().voiceDb - expected) <= 1.5f);
}

void test_display_voice_ignores_out_of_phase() {
  Engine e;
  e.prepare(kSr, 512);
  e.setSettings(echoSettings());
  const float amp = (float)std::pow(10.0, -12.0 / 20.0);
  const Buf l = sine(9600, 1000.0, amp);
  Buf r(l.size());
  for (size_t i = 0; i < l.size(); ++i) r[i] = -l[i];
  runEngine(e, l, r, nullptr);
  ASSERT_TRUE(e.lastDisplay().voiceDb <= -100.f);  // the mono detector sum cancels
}

void test_display_voice_bass_reads_lower() {
  const float amp = (float)std::pow(10.0, -12.0 / 20.0);
  auto voiceOf = [&](double freq) {
    Engine e;
    e.prepare(kSr, 512);
    e.setSettings(echoSettings());
    const Buf s = sine(9600, freq, amp);
    runEngine(e, s, s, nullptr);
    return e.lastDisplay().voiceDb;
  };
  ASSERT_TRUE(voiceOf(40.0) <= voiceOf(1000.0) - 20.f);  // 120 Hz high-pass in the detector
}

void test_display_voice_consistent_with_threshold() {
  auto run = [](double peakDb) {
    Engine e;
    e.prepare(kSr, 512);
    Settings st = echoSettings();
    st.thresholdDb = -35.f;
    e.setSettings(st);
    const Buf s = sine(9600, 1000.0, (float)std::pow(10.0, peakDb / 20.0));
    runEngine(e, s, s, nullptr);
    return e.lastDisplay();
  };
  const auto above = run(-29.0);  // displays about -33 dB
  ASSERT_TRUE(above.voiceDb > -35.f);
  ASSERT_TRUE(above.sendGain < 0.5f);
  const auto below = run(-34.0);  // displays about -38 dB: louder peak than the threshold, yet no ducking
  ASSERT_TRUE(below.voiceDb < -35.f);
  ASSERT_TRUE(below.sendGain > 0.99f);
}

void test_display_send_drops_above_threshold() {
  Engine e;
  e.prepare(kSr, 512);
  e.setSettings(echoSettings());
  const Buf s = sine((size_t)kSr / 4, 1000.0, 0.5f);
  runEngine(e, s, s, nullptr);
  ASSERT_TRUE(e.lastDisplay().sendGain < 0.1f);
}

void test_display_echo_appears_after_phrase() {
  Engine e;
  e.prepare(kSr, 256);
  Settings st = echoSettings();
  st.feedback = 0.7f;
  e.setSettings(st);
  const size_t phrase = (size_t)(kSr * 0.5), n = phrase + (size_t)(kSr * 2.0);
  Buf in(n, 0.f);
  const Buf s = sine(phrase, 440.0, 0.3f);
  std::copy(s.begin(), s.end(), in.begin());
  Buf out(256);
  float best = -120.f;
  for (size_t i = 0; i + 256 <= n; i += 256) {
    e.process(in.data() + i, in.data() + i, nullptr, nullptr, out.data(), out.data(), 256);
    // after Hold (200 ms) + Release (300 ms) the gate has reopened onto the repeats
    if (i >= phrase + (size_t)(kSr * 0.6)) best = std::max(best, e.lastDisplay().echoDb);
  }
  ASSERT_TRUE(best > -50.f);
}

// Max echoDb over a phrase and its repeats (gate reopened), for the given settings.
float echoAfterPhrase(Settings st) {
  Engine e;
  e.prepare(kSr, 256);
  st.feedback = 0.7f;
  e.setSettings(st);
  const size_t phrase = (size_t)(kSr * 0.5), n = phrase + (size_t)(kSr * 2.0);
  Buf in(n, 0.f);
  const Buf s = sine(phrase, 440.0, 0.5f);
  std::copy(s.begin(), s.end(), in.begin());
  Buf out(256);
  float best = -120.f;
  for (size_t i = 0; i + 256 <= n; i += 256) {
    e.process(in.data() + i, in.data() + i, nullptr, nullptr, out.data(), out.data(), 256);
    if (i >= phrase + (size_t)(kSr * 0.6)) best = std::max(best, e.lastDisplay().echoDb);
  }
  return best;
}

void test_display_echo_silent_with_mix_zero() {
  ASSERT_TRUE(echoAfterPhrase(echoSettings(0.f)) <= -100.f);
}

void test_display_echo_silent_with_bypass() {
  Settings st = echoSettings(1.f);
  st.bypass = true;
  ASSERT_TRUE(echoAfterPhrase(st) <= -100.f);
}

void test_display_echo_follows_mix() {
  const float full = echoAfterPhrase(echoSettings(1.f));
  const float half = echoAfterPhrase(echoSettings(0.5f));
  ASSERT_TRUE(full > -50.f);
  ASSERT_TRUE(std::fabs((full - half) - 6.02f) <= 0.5f);
}

// ---- Editor size (content scale) ------------------------------------------------------
void test_editor_size_scales_and_rounds() {
  using namespace FairyTapes::PillowTalk::Ui;
  auto is = [](PixelSize p, int w, int h) { return p.width == w && p.height == h; };
  ASSERT_TRUE(is(scaledEditorSize(1.0), 1000, 628));
  ASSERT_TRUE(is(scaledEditorSize(2.0), 2000, 1256));
  ASSERT_TRUE(is(scaledEditorSize(1.5), 1500, 942));
  ASSERT_TRUE(is(scaledEditorSize(1.25), 1250, 785));
  ASSERT_TRUE(is(scaledEditorSize(1.75), 1750, 1099));
  ASSERT_TRUE(is(scaledEditorSize(1.1), 1100, 691));        // 690.8 rounds up
  ASSERT_TRUE(is(scaledEditorSize(1.0 / 3.0), 333, 209));   // 333.33 and 209.33 round down
}

void test_editor_size_sanitizes_factor() {
  using namespace FairyTapes::PillowTalk::Ui;
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  ASSERT_APPROX(sanitizeContentScale(0.0), 1.0);
  ASSERT_APPROX(sanitizeContentScale(-2.0), 1.0);
  ASSERT_APPROX(sanitizeContentScale(nan), 1.0);
  ASSERT_APPROX(sanitizeContentScale(inf), 1.0);
  ASSERT_APPROX(sanitizeContentScale(1e9), kMaxContentScale);
  ASSERT_APPROX(sanitizeContentScale(2.0), 2.0);
  ASSERT_TRUE(scaledEditorSize(nan).width == 1000 && scaledEditorSize(-1.0).height == 628);
}

void test_display_ids_do_not_collide() {
  ASSERT_TRUE(kMeterBase >= (Steinberg::Vst::ParamID)kNumParams);
  ASSERT_TRUE(kDispVoice == kMeterBase);
  ASSERT_TRUE(kDispSend == kMeterBase + 1 && kDispEcho == kMeterBase + 2);
  ASSERT_TRUE(kNumDisplayIDs == kMeterBase + 10);
}

void test_display_not_in_state() {
  ASSERT_TRUE((int)kNumParams == 16);
  ASSERT_TRUE(kStateSize == sizeof(int32_t) + sizeof(double) * 16);
  double v[kNumParams];
  loadDefaults(v);
  std::vector<uint8_t> bytes;
  serializeState(v, bytes);
  ASSERT_TRUE(bytes.size() == kStateSize);
}

void test_display_write_rate_limit() {
  DisplayRateLimiter lim;
  lim.prepare(48000.0);
  int writes = 0;
  long lastAt = -1000000, at = 0;
  long minGap = 1000000;
  for (int b = 0; b < 2000; ++b) {
    if (lim.advance(64)) {
      if (writes > 0) minGap = std::min(minGap, at - lastAt);
      lastAt = at;
      ++writes;
    }
    at += 64;
  }
  ASSERT_TRUE(writes >= 1);
  ASSERT_TRUE(minGap >= (long)(0.016 * 48000.0));
  // first block writes; zero-sample blocks never do harm
  DisplayRateLimiter l3;
  l3.prepare(44100.0);
  ASSERT_TRUE(l3.advance(64));
  ASSERT_TRUE(!l3.advance(64));
}

void test_display_echo_is_measured_after_ducking() {
  Engine e;
  e.prepare(kSr, 256);
  Settings st = echoSettings();
  st.feedback = 0.95f;
  e.setSettings(st);
  const size_t phrase = (size_t)(kSr * 1.0), n = phrase + (size_t)(kSr * 2.0);
  Buf in(n, 0.f);
  const Buf s = sine(phrase, 440.0, 0.3f);
  std::copy(s.begin(), s.end(), in.begin());
  Buf out(256);
  float during = -120.f, after = -120.f;
  for (size_t i = 0; i + 256 <= n; i += 256) {
    e.process(in.data() + i, in.data() + i, nullptr, nullptr, out.data(), out.data(), 256);
    const float v = e.lastDisplay().echoDb;
    if (i >= (size_t)(kSr * 0.3) && i + 256 <= phrase) during = std::max(during, v);
    if (i >= phrase + (size_t)(kSr * 0.6)) after = std::max(after, v);
  }
  ASSERT_TRUE(after > -50.f);
  ASSERT_TRUE(during <= after - 15.f);
}

void test_display_accumulator_holds_extremes_between_writes() {
  DisplayAccumulator acc;
  DisplayRateLimiter lim;
  lim.prepare(48000.0);
  int writes = 0;
  float published = -999.f, publishedSend = 2.f;
  // 12 blocks of 64 between writes; a burst + gain dip only in the 2nd block after a write.
  ASSERT_TRUE(lim.advance(64));  // first write, nothing accumulated yet
  acc.take();
  for (int b = 1; b <= 12; ++b) {
    DisplayValues d;
    d.voiceDb = d.echoDb = (b == 2) ? -6.f : -90.f;
    d.sendGain = (b == 2) ? 0.05f : 1.f;
    acc.add(d);
    if (lim.advance(64)) {
      const DisplayValues p = acc.take();
      published = p.voiceDb;
      publishedSend = p.sendGain;
      ++writes;
      ASSERT_TRUE(b == 12);
    }
  }
  ASSERT_TRUE(writes == 1);
  ASSERT_APPROX(published, -6.0);
  ASSERT_APPROX(publishedSend, 0.05);
  const DisplayValues r = acc.take();  // reset after publishing
  ASSERT_TRUE(r.voiceDb <= -120.f && r.echoDb <= -120.f && r.sendGain == 1.f);
}

// ---- Task 1 (editor v1): LEVEL block data ---------------------------------------------
void test_levels_silence_is_floor() {
  Engine e;
  e.prepare(kSr, 512);
  e.setSettings(echoSettings());
  const Buf z(4800, 0.f);
  runEngine(e, z, z, nullptr);
  const auto l = e.lastLevels();
  ASSERT_TRUE(l.inL <= -100.f && l.inR <= -100.f && l.outL <= -100.f && l.outR <= -100.f);
  ASSERT_TRUE(!l.clipIn && !l.clipOut);
}

void test_levels_in_out_peaks_follow_signal() {
  Engine e;
  e.prepare(kSr, 512);
  e.setSettings(echoSettings(0.f));
  const Buf l = sine(4800, 1000.0, (float)std::pow(10.0, -12.0 / 20.0));
  const Buf r = sine(4800, 1000.0, (float)std::pow(10.0, -24.0 / 20.0));
  runEngine(e, l, r, nullptr);
  const auto lv = e.lastLevels();
  ASSERT_TRUE(std::fabs(lv.inL + 12.f) <= 1.5f);
  ASSERT_TRUE(std::fabs(lv.inR + 24.f) <= 1.5f);
  ASSERT_TRUE(lv.inL > lv.inR + 6.f);
  ASSERT_TRUE(std::fabs(lv.outL + 12.f) <= 1.5f);
  ASSERT_TRUE(std::fabs(lv.outR + 24.f) <= 1.5f);
}

void test_levels_out_after_mix_and_bypass() {
  const float amp = (float)std::pow(10.0, -12.0 / 20.0);
  // Mix 0: output follows the input and carries no echo once the input stops.
  Engine e;
  e.prepare(kSr, 512);
  Settings st = echoSettings(0.f);
  st.feedback = 0.7f;
  e.setSettings(st);
  const Buf s = sine(4800, 440.0, amp);
  runEngine(e, s, s, nullptr);
  ASSERT_TRUE(std::fabs(e.lastLevels().outL - e.lastLevels().inL) <= 0.5f);
  const Buf z((size_t)kSr, 0.f);
  float outMax = -120.f;
  for (size_t i = 0; i + 512 <= z.size(); i += 512) {
    Buf o(512);
    e.process(z.data(), z.data(), nullptr, nullptr, o.data(), o.data(), 512);
    outMax = std::max(outMax, e.lastLevels().outL);
  }
  ASSERT_TRUE(outMax <= -100.f);
  // Mix 1 would show the echo: the same input after Mix 1 reads on the output (sanity of the test).
  Engine w;
  w.prepare(kSr, 512);
  w.setSettings(echoSettings(1.f));
  runEngine(w, s, s, nullptr);
  float echoMax = -120.f;
  for (size_t i = 0; i + 512 <= z.size(); i += 512) {
    Buf o(512);
    w.process(z.data(), z.data(), nullptr, nullptr, o.data(), o.data(), 512);
    echoMax = std::max(echoMax, w.lastLevels().outL);
  }
  ASSERT_TRUE(echoMax > -50.f);
  // Bypass: once the mix has ramped to 0 the output follows the input.
  Engine b;
  b.prepare(kSr, 512);
  Settings bs = echoSettings(1.f);
  b.setSettings(bs);
  runEngine(b, s, s, nullptr);
  bs.bypass = true;
  b.setSettings(bs);
  const Buf long_ = sine(9600, 440.0, amp);
  runEngine(b, long_, long_, nullptr);
  ASSERT_TRUE(std::fabs(b.lastLevels().outL - b.lastLevels().inL) <= 0.5f);
}

void test_clip_at_exactly_one_and_hold_2s() {
  Engine e;
  e.prepare(kSr, 512);
  e.setSettings(echoSettings(0.f));
  Buf x(512, 0.f);
  x[10] = 0.9999f;
  Buf o(512);
  e.process(x.data(), x.data(), nullptr, nullptr, o.data(), o.data(), 512);
  ASSERT_TRUE(!e.lastLevels().clipIn && !e.lastLevels().clipOut);
  x[10] = 1.0f;
  e.process(x.data(), x.data(), nullptr, nullptr, o.data(), o.data(), 512);
  ASSERT_TRUE(e.lastLevels().clipIn && e.lastLevels().clipOut);
  const Buf z(512, 0.f);
  auto runFor = [&](double seconds) {
    for (int i = 0; i < (int)(seconds * kSr / 512); ++i)
      e.process(z.data(), z.data(), nullptr, nullptr, o.data(), o.data(), 512);
  };
  runFor(1.8);
  ASSERT_TRUE(e.lastLevels().clipIn && e.lastLevels().clipOut);
  runFor(0.4);
  ASSERT_TRUE(!e.lastLevels().clipIn && !e.lastLevels().clipOut);
  // Negative full scale clips too, on the right channel only.
  Buf n(512, 0.f);
  n[3] = -1.0f;
  e.process(z.data(), n.data(), nullptr, nullptr, o.data(), o.data(), 512);
  ASSERT_TRUE(e.lastLevels().clipIn);
}

void test_clip_clear() {
  Engine e;
  e.prepare(kSr, 512);
  e.setSettings(echoSettings(0.f));
  Buf x(512, 0.f);
  x[0] = 1.5f;
  Buf o(512);
  e.process(x.data(), x.data(), nullptr, nullptr, o.data(), o.data(), 512);
  ASSERT_TRUE(e.lastLevels().clipIn && e.lastLevels().clipOut);
  e.clearClips();
  ASSERT_TRUE(!e.lastLevels().clipIn && !e.lastLevels().clipOut);
  const Buf z(512, 0.f);
  e.process(z.data(), z.data(), nullptr, nullptr, o.data(), o.data(), 512);
  ASSERT_TRUE(!e.lastLevels().clipIn && !e.lastLevels().clipOut);
}

void test_levels_process_does_not_allocate() {
  Engine e;
  e.prepare(kSr, 512);
  e.setSettings(echoSettings());
  const Buf in = sine(4096, 440.0, 1.2f);
  Buf o(4096);
  g_allocs = 0;
  g_countAllocs = true;
  for (int i = 0; i < 4096; i += 512) {
    e.process(in.data() + i, in.data() + i, nullptr, nullptr, o.data() + i, o.data() + i, 512);
    (void)e.lastLevels();
  }
  e.clearClips();
  g_countAllocs = false;
  ASSERT_TRUE(g_allocs == 0);
}

void test_level_ids_do_not_collide_and_not_in_state() {
  ASSERT_TRUE(kDispInL == kMeterBase + 3 && kDispInR == kMeterBase + 4);
  ASSERT_TRUE(kDispOutL == kMeterBase + 5 && kDispOutR == kMeterBase + 6);
  ASSERT_TRUE(kDispClipIn == kMeterBase + 7 && kDispClipOut == kMeterBase + 8);
  ASSERT_TRUE(kClearClips == kMeterBase + 9);
  ASSERT_TRUE(kClearClips < kNumDisplayIDs && kNumDisplayIDs == kMeterBase + 10);
  ASSERT_TRUE(kDispInL >= (Steinberg::Vst::ParamID)kNumParams);
  ASSERT_TRUE((int)kNumParams == 16 && kStateSize == sizeof(int32_t) + sizeof(double) * 16);
}

void test_levels_accumulator_holds_peaks() {
  LevelAccumulator acc;
  LevelValues a;
  a.inL = -30.f; a.inR = -40.f; a.outL = -50.f; a.outR = -60.f;
  LevelValues b;
  b.inL = -6.f; b.inR = -90.f; b.outL = -80.f; b.outR = -3.f; b.clipIn = true;
  LevelValues c = a;
  acc.add(a);
  acc.add(b);
  acc.add(c);
  const LevelValues r = acc.take();
  ASSERT_APPROX(r.inL, -6.0);
  ASSERT_APPROX(r.inR, -40.0);
  ASSERT_APPROX(r.outL, -50.0);
  ASSERT_APPROX(r.outR, -3.0);
  ASSERT_TRUE(r.clipIn && !r.clipOut);
  const LevelValues z = acc.take();  // reset after taking
  ASSERT_TRUE(z.inL <= -120.f && z.outR <= -120.f && !z.clipIn && !z.clipOut);
}

void test_history_order_before_full() {
  FairyTapes::PillowTalk::Ui::History h(5);
  ASSERT_TRUE(h.size() == 0);
  for (int i = 0; i < 3; ++i) h.push(0.1f * (i + 1), 0.2f * (i + 1), 0.05f * (i + 1));
  ASSERT_TRUE(h.size() == 3);
  for (int i = 0; i < 3; ++i) {
    ASSERT_APPROX(h.voice(i), 0.1 * (i + 1));
    ASSERT_APPROX(h.send(i), 0.2 * (i + 1));
    ASSERT_APPROX(h.echo(i), 0.05 * (i + 1));
  }
}

void test_history_wraps_oldest_first() {
  const int cap = 7, k = 4;
  FairyTapes::PillowTalk::Ui::History h(cap);
  for (int i = 0; i < cap + k; ++i) h.push(i / 100.f, 0.5f, (i + 1) / 100.f);
  ASSERT_TRUE(h.size() == cap);
  ASSERT_APPROX(h.voice(0), k / 100.0);
  ASSERT_APPROX(h.echo(0), (k + 1) / 100.0);
  ASSERT_APPROX(h.voice(cap - 1), (cap + k - 1) / 100.0);
  for (int i = 1; i < cap; ++i) ASSERT_TRUE(h.voice(i) > h.voice(i - 1));
}

void test_history_zero_capacity_is_safe() {
  for (int cap : {0, -3}) {
    FairyTapes::PillowTalk::Ui::History h(cap);
    h.push(0.5f, 0.5f, 0.5f);
    ASSERT_TRUE(h.size() == 0);
    ASSERT_APPROX(h.voice(0), 0.0);
    ASSERT_APPROX(h.send(0), 1.0);
    ASSERT_APPROX(h.echo(0), 0.0);
  }
}

void test_history_out_of_range_accessors_are_neutral() {
  FairyTapes::PillowTalk::Ui::History h(4);
  h.push(0.7f, 0.3f, 0.6f);
  for (int i : {-1, 1, 4, 1000, -100000}) {
    ASSERT_APPROX(h.voice(i), 0.0);
    ASSERT_APPROX(h.send(i), 1.0);
    ASSERT_APPROX(h.echo(i), 0.0);
  }
  ASSERT_APPROX(h.voice(0), 0.7);
}

void test_history_no_allocation_after_construction() {
  FairyTapes::PillowTalk::Ui::History h(200);
  g_allocs = 0;
  g_countAllocs = true;
  for (int i = 0; i < 1000; ++i) h.push(0.5f, 0.5f, 0.5f);
  float acc = 0;
  for (int i = 0; i < h.size(); ++i) acc += h.voice(i) + h.send(i) + h.echo(i);
  g_countAllocs = false;
  ASSERT_TRUE(acc > 0.f);
  ASSERT_TRUE(g_allocs == 0);
}

void test_history_sanitizes_non_finite_and_clamps() {
  const float nan = std::nanf(""), inf = INFINITY;
  FairyTapes::PillowTalk::Ui::History h(8);
  h.push(nan, nan, nan);
  h.push(inf, inf, -inf);
  h.push(-2.f, -2.f, -2.f);
  h.push(3.f, 3.f, 3.f);
  ASSERT_TRUE(h.size() == 4);
  ASSERT_APPROX(h.voice(0), 0.0); ASSERT_APPROX(h.send(0), 1.0); ASSERT_APPROX(h.echo(0), 0.0);
  ASSERT_APPROX(h.voice(1), 0.0); ASSERT_APPROX(h.send(1), 1.0); ASSERT_APPROX(h.echo(1), 0.0);
  ASSERT_APPROX(h.voice(2), 0.0); ASSERT_APPROX(h.send(2), 0.0); ASSERT_APPROX(h.echo(2), 0.0);
  ASSERT_APPROX(h.voice(3), 1.0); ASSERT_APPROX(h.send(3), 1.0); ASSERT_APPROX(h.echo(3), 1.0);
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
  RUN_TEST(test_state_roundtrip);
  RUN_TEST(test_state_rejects_unknown_version_and_truncation);
  RUN_TEST(test_state_rejects_non_finite_values);
  RUN_TEST(test_state_clamps_out_of_range_values);
  RUN_TEST(test_map_params_never_returns_non_finite);
  RUN_TEST(test_engine_ignores_non_finite_settings);
  RUN_TEST(test_note_index_matches_string_list);
  RUN_TEST(test_map_params_free_and_sync);
  RUN_TEST(test_detector_falls_back_to_main_input);
  RUN_TEST(test_chain_sidechain_ducks_echo);
  RUN_TEST(test_chain_phrase_tail_heard_in_pause);
  RUN_TEST(test_engine_mix_formula);
  RUN_TEST(test_engine_bypass_passes_dry);
  RUN_TEST(test_engine_in_place_mono_gives_identical_channels);
  RUN_TEST(test_engine_zero_one_and_oversized_blocks);
  RUN_TEST(test_engine_silence_stays_silent);
  RUN_TEST(test_engine_reprepare_at_new_rate);
  RUN_TEST(test_engine_process_does_not_allocate);
  RUN_TEST(test_display_silence_is_floor_and_unity_send);
  RUN_TEST(test_display_voice_follows_signal);
  RUN_TEST(test_display_voice_ignores_out_of_phase);
  RUN_TEST(test_display_voice_bass_reads_lower);
  RUN_TEST(test_display_voice_consistent_with_threshold);
  RUN_TEST(test_display_echo_silent_with_mix_zero);
  RUN_TEST(test_display_echo_silent_with_bypass);
  RUN_TEST(test_display_echo_follows_mix);
  RUN_TEST(test_editor_size_scales_and_rounds);
  RUN_TEST(test_editor_size_sanitizes_factor);
  RUN_TEST(test_display_send_drops_above_threshold);
  RUN_TEST(test_display_echo_appears_after_phrase);
  RUN_TEST(test_display_ids_do_not_collide);
  RUN_TEST(test_display_not_in_state);
  RUN_TEST(test_display_write_rate_limit);
  RUN_TEST(test_display_echo_is_measured_after_ducking);
  RUN_TEST(test_display_accumulator_holds_extremes_between_writes);
  RUN_TEST(test_levels_silence_is_floor);
  RUN_TEST(test_levels_in_out_peaks_follow_signal);
  RUN_TEST(test_levels_out_after_mix_and_bypass);
  RUN_TEST(test_clip_at_exactly_one_and_hold_2s);
  RUN_TEST(test_clip_clear);
  RUN_TEST(test_levels_process_does_not_allocate);
  RUN_TEST(test_level_ids_do_not_collide_and_not_in_state);
  RUN_TEST(test_levels_accumulator_holds_peaks);
  RUN_TEST(test_history_order_before_full);
  RUN_TEST(test_history_wraps_oldest_first);
  RUN_TEST(test_history_zero_capacity_is_safe);
  RUN_TEST(test_history_out_of_range_accessors_are_neutral);
  RUN_TEST(test_history_no_allocation_after_construction);
  RUN_TEST(test_history_sanitizes_non_finite_and_clamps);

  std::fprintf(stdout, "\n%d passed, %d failed.\n", g_passed, g_failed);
  if (g_failed > 0) {
    std::fprintf(stderr, "%d test(s) FAILED.\n", g_failed);
    return EXIT_FAILURE;
  }
  std::fprintf(stdout, "All tests passed.\n");
  return EXIT_SUCCESS;
}
