/**
 * Fairy Tapes · Deep Throat — Unit tests for params, mappings and state.
 * Run via CTest; build target: deep_throat_tests.
 */

#include "params.h"
#include "state.h"
#include "dsp/rider.h"
#include "dsp/fft.h"
#include "dsp/spectral_breath_detector.h"
#include "dsp/level_breath_detector.h"
#include "dsp/breath_reducer.h"
#include "dsp/biquad.h"
#include "dsp/corrective_eq.h"
#include "dsp/deesser.h"
#include "dsp/distressor_comp.h"
#include "dsp/tone_eq.h"
#include "mode_presets.h"
#include "engine.h"
#include <algorithm>
#include <atomic>
#include <new>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
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

int g_failed = 0;
int g_passed = 0;

bool approx(double a, double b, double eps) { return std::fabs(a - b) <= eps; }

#define ASSERT_TRUE(expr) do { \
  if (!(expr)) { \
    std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); \
    ++g_failed; return; \
  } \
} while(0)

#define ASSERT_NEAR(a, b, eps) do { \
  if (!approx((a), (b), (eps))) { \
    std::fprintf(stderr, "  FAIL [%s:%d]: %s ~= %s (got %.9f vs %.9f)\n", \
                 __FILE__, __LINE__, #a, #b, (double)(a), (double)(b)); \
    ++g_failed; return; \
  } \
} while(0)

#define RUN_TEST(fn) do { \
  fn(); \
  ++g_passed; \
} while(0)

using namespace FairyTapes::DeepThroat;

void test_param_enum_count() {
  ASSERT_TRUE(kNumParams == 25);
  ASSERT_TRUE(kBypass == 0);
  ASSERT_TRUE(kEqAmount == 24);
}

void test_deess_freq_range() {
  ASSERT_NEAR(deEssFreqHzFromNorm(0.0), 4000.0, 1e-6);
  ASSERT_NEAR(deEssFreqHzFromNorm(1.0), 10000.0, 1e-6);
  ASSERT_NEAR(deEssFreqHzFromNorm(0.53), 6500.0, 50.0);
  ASSERT_NEAR(deEssFreqHzFromNorm(kDefaultDeEssFreq), 6500.0, 50.0);
}

void test_comp_choice_steps() {
  ASSERT_TRUE(compRatioChoiceFromNorm(0.0) == 0);   // AUTO
  ASSERT_TRUE(compRatioChoiceFromNorm(1.0) == 7);   // 20:1
  ASSERT_TRUE(compRatioChoiceFromNorm(1.0 / 7.0) == 1);
  ASSERT_TRUE(compRatioFromChoice(0) == 0.0);
  ASSERT_TRUE(compRatioFromChoice(1) == 2.0);
  ASSERT_TRUE(compRatioFromChoice(7) == 20.0);
  ASSERT_TRUE(compDistChoiceFromNorm(0.0) == 0);    // AUTO
  ASSERT_TRUE(compDistChoiceFromNorm(1.0) == 3);    // Dist 3
  ASSERT_TRUE(compDistChoiceFromNorm(1.0 / 3.0) == 1);  // OFF
  ASSERT_TRUE(compHpfChoiceFromNorm(0.0) == 0);     // OFF
  ASSERT_TRUE(compHpfChoiceFromNorm(0.5) == 1);     // 80 Hz
  ASSERT_TRUE(compHpfChoiceFromNorm(1.0) == 2);     // 150 Hz
  ASSERT_TRUE(compHpfHzFromChoice(0) == 0.0);
  ASSERT_TRUE(compHpfHzFromChoice(1) == 80.0);
  ASSERT_TRUE(compHpfHzFromChoice(2) == 150.0);
}

void test_mode_from_norm_steps() {
  ASSERT_TRUE(modeFromNorm(0.0) == Mode::Speak);
  ASSERT_TRUE(modeFromNorm(0.5) == Mode::Sing);
  ASSERT_TRUE(modeFromNorm(1.0) == Mode::Puke);
  ASSERT_TRUE(modeFromNorm(0.2) == Mode::Speak);
  ASSERT_TRUE(modeFromNorm(0.8) == Mode::Puke);
}

// The DSP must hear the choice the host displays: the SDK's StringListParameter shows entry
// min(count - 1, floor(n * count)) for a normalized value n (Parameter::toPlain).
void test_choice_mapping_matches_sdk_display() {
  int bad = 0;
  for (int k = 0; k <= 1000; ++k) {
    const double n = k / 1000.0;
    auto sdk = [n](int count) { return std::min(count - 1, (int)std::floor(n * count)); };
    if ((int)modeFromNorm(n) != sdk(kModeCount)) ++bad;
    if (compRatioChoiceFromNorm(n) != sdk(kCompRatioCount)) ++bad;
    if (compDistChoiceFromNorm(n) != sdk(kCompDistCount)) ++bad;
    if (compHpfChoiceFromNorm(n) != sdk(kCompHpfCount)) ++bad;
  }
  ASSERT_TRUE(bad == 0);
  ASSERT_TRUE(modeFromNorm(0.3) == Mode::Speak);
  ASSERT_TRUE(compRatioChoiceFromNorm(-1.0) == 0 && compRatioChoiceFromNorm(2.0) == 7);
}

void test_io_gain_ranges() {
  ASSERT_NEAR(inputDbFromNorm(0.0), -12.0, 1e-9);
  ASSERT_NEAR(inputDbFromNorm(0.5), 0.0, 1e-9);
  ASSERT_NEAR(inputDbFromNorm(1.0), 12.0, 1e-9);
  ASSERT_NEAR(outputDbFromNorm(0.0), -12.0, 1e-9);
  ASSERT_NEAR(outputDbFromNorm(1.0), 12.0, 1e-9);
  ASSERT_NEAR(riderTargetDbFromNorm(0.0), -30.0, 1e-9);
  ASSERT_NEAR(riderTargetDbFromNorm(1.0), -10.0, 1e-9);
}

void test_defaults() {
  double d[kNumParams];
  loadDefaults(d);
  ASSERT_TRUE(d[kBypass] == 0.0);
  ASSERT_TRUE(d[kMode] == 0.0);
  ASSERT_TRUE(d[kRiderIn] == 1.0 && d[kBreathIn] == 1.0 && d[kEqIn] == 1.0);
  ASSERT_TRUE(d[kDeEssIn] == 1.0 && d[kCompIn] == 1.0 && d[kToneIn] == 1.0);
  ASSERT_TRUE(d[kDeEssListen] == 0.0 && d[kBreathListen] == 0.0);
  ASSERT_TRUE(d[kCompMix] == 1.0 && d[kCompRatio] == 0.0 && d[kCompDist] == 0.0);
  ASSERT_TRUE(d[kCompHpf] == 0.5 && d[kEqAmount] == 0.5);
  ASSERT_TRUE(d[kInput] == 0.5 && d[kOutput] == 0.5);
}

void test_state_roundtrip() {
  double in[kNumParams];
  for (int i = 0; i < (int)kNumParams; ++i) in[i] = (i + 1) / 26.0;
  std::vector<uint8_t> bytes;
  serializeState(in, bytes);
  ASSERT_TRUE(bytes.size() == kStateSize);
  double out[kNumParams] = {};
  ASSERT_TRUE(deserializeState(bytes.data(), bytes.size(), out));
  for (int i = 0; i < (int)kNumParams; ++i) ASSERT_TRUE(out[i] == in[i]);
}

void test_state_rejects_bad_version_and_nan() {
  double in[kNumParams];
  loadDefaults(in);
  std::vector<uint8_t> bytes;
  serializeState(in, bytes);
  double out[kNumParams];
  loadDefaults(out);
  out[kTone] = 0.123;

  std::vector<uint8_t> bad = bytes;
  bad[0] = 99;  // unknown version
  ASSERT_TRUE(!deserializeState(bad.data(), bad.size(), out));
  ASSERT_TRUE(!deserializeState(bytes.data(), bytes.size() - 1, out));  // short
  ASSERT_TRUE(!deserializeState(nullptr, 0, out));

  double nan_in[kNumParams];
  loadDefaults(nan_in);
  nan_in[kTone] = std::numeric_limits<double>::quiet_NaN();
  serializeState(nan_in, bad);
  ASSERT_TRUE(!deserializeState(bad.data(), bad.size(), out));
  ASSERT_TRUE(out[kTone] == 0.123);  // untouched on failure

  double wild[kNumParams];
  loadDefaults(wild);
  wild[kInput] = 5.0;
  wild[kOutput] = -3.0;
  serializeState(wild, bad);
  ASSERT_TRUE(deserializeState(bad.data(), bad.size(), out));
  ASSERT_TRUE(out[kInput] == 1.0 && out[kOutput] == 0.0);  // clamped
}

float toDb(float lin) { return 20.0f * std::log10(lin); }

float runRider(DSP::Rider& r, float levelDb, bool freeze, int samples) {
  float g = 1.0f;
  for (int i = 0; i < samples; ++i) g = r.processFrame(levelDb, freeze);
  return g;
}

void test_rider_raises_quiet_to_target() {
  DSP::Rider r;
  r.prepare(48000.0);
  r.setTargetDb(-20.0f);
  r.setMaxRangeDb(6.0f);
  r.setSlewDbPerSec(8.0f);
  const float g = runRider(r, -30.0f, false, 3 * 48000);
  ASSERT_NEAR(toDb(g), 6.0, 0.5);  // error 10 dB, clamped by the range
}

void test_rider_lowers_loud() {
  DSP::Rider r;
  r.prepare(48000.0);
  r.setTargetDb(-20.0f);
  r.setMaxRangeDb(6.0f);
  r.setSlewDbPerSec(8.0f);
  const float g = runRider(r, -10.0f, false, 3 * 48000);
  ASSERT_NEAR(toDb(g), -6.0, 0.5);
}

void test_rider_slew_limited() {
  for (double sr : {44100.0, 96000.0}) {
    DSP::Rider r;
    r.prepare(sr);
    r.setTargetDb(-20.0f);
    r.setMaxRangeDb(12.0f);
    r.setSlewDbPerSec(4.0f);
    const int n = (int)sr;
    runRider(r, -20.0f, false, n);  // settle at 0 dB
    const float g = runRider(r, -32.0f, false, n);  // level jump: wants +12 dB
    ASSERT_TRUE(toDb(g) <= 4.0f + 0.1f);
    ASSERT_TRUE(toDb(g) >= 3.5f);  // and does move at about the slew rate
  }
}

void test_rider_freeze_holds_gain() {
  DSP::Rider r;
  r.prepare(48000.0);
  r.setTargetDb(-20.0f);
  r.setMaxRangeDb(12.0f);
  r.setSlewDbPerSec(8.0f);
  const float g0 = runRider(r, -26.0f, false, 48000);
  ASSERT_TRUE(toDb(g0) > 1.0f);
  const float g1 = runRider(r, -40.0f, true, 48000);
  ASSERT_TRUE(g1 == g0);
  const float g2 = runRider(r, -5.0f, true, 48000);
  ASSERT_TRUE(g2 == g0);
}

void test_rider_ignores_silence() {
  DSP::Rider r;
  r.prepare(48000.0);
  r.setTargetDb(-20.0f);
  r.setMaxRangeDb(12.0f);
  r.setSlewDbPerSec(8.0f);
  const float g0 = runRider(r, -26.0f, false, 48000);
  const float g1 = runRider(r, -80.0f, false, 48000);  // below -60 dB: like freeze
  ASSERT_TRUE(g1 == g0);
  const float g2 = runRider(r, -120.0f, false, 48000);
  ASSERT_TRUE(g2 == g0);
}

void test_level_meter_sine() {
  const double sr = 48000.0;
  DSP::LevelMeter m;
  m.prepare(sr, 200.0f);
  const double amp = std::pow(10.0, -20.0 / 20.0);
  float db = 0.0f;
  for (int i = 0; i < (int)sr; ++i)
    db = m.process((float)(amp * std::sin(2.0 * 3.14159265358979 * 1000.0 * i / sr)));
  ASSERT_NEAR(db, -23.0, 0.5);
  m.reset();
  for (int i = 0; i < 100; ++i) db = m.process(0.0f);
  ASSERT_TRUE(db <= -119.0f && std::isfinite(db));
}

// ---------------------------------------------------------------------------
// FFT and spectral breath detector
// ---------------------------------------------------------------------------

constexpr double kPi = 3.14159265358979323846;

void test_fft_peak_at_bin() {
  for (int n : {64, 1024, 4096}) {
    const int k = n / 16 + 3;
    std::vector<float> in(n), p(n / 2 + 1, -1.0f);
    for (int i = 0; i < n; ++i) in[i] = (float)std::sin(2.0 * kPi * k * i / n);
    DSP::fftPowerSpectrum(in.data(), n, p.data());
    int arg = 0;
    for (int b = 0; b <= n / 2; ++b) if (p[b] > p[arg]) arg = b;
    ASSERT_TRUE(arg == k);
    const double expected = (n / 2.0) * (n / 2.0);  // |X_k|^2 of a unit sine at an exact bin
    ASSERT_NEAR(p[k] / expected, 1.0, 1e-3);
    for (int b = 0; b <= n / 2; ++b)
      if (b != k) ASSERT_TRUE(p[b] < 1e-6 * p[k]);
  }
}

// Deterministic synthetic test signals (seeded LCG, no std::random).
class SignalBuilder {
public:
  explicit SignalBuilder(double sr) : sr_(sr) {}

  // Naive sawtooth at the given RMS level (dBFS).
  void saw(double hz, double rmsDb, double sec) {
    const double amp = std::pow(10.0, rmsDb / 20.0) * std::sqrt(3.0);  // saw RMS = A / sqrt(3)
    const int n = (int)(sec * sr_);
    for (int i = 0; i < n; ++i) {
      x.push_back((float)(amp * (2.0 * phase_ - 1.0)));
      phase_ += hz / sr_;
      if (phase_ >= 1.0) phase_ -= 1.0;
    }
  }

  // White noise high-passed at 2 kHz (4th-order Butterworth), normalized to the RMS level.
  void hpNoise(double rmsDb, double sec) {
    const int n = (int)(sec * sr_);
    std::vector<double> seg(n);
    Biquad a = Biquad::highpass(2000.0, 0.5411961, sr_);
    Biquad b = Biquad::highpass(2000.0, 1.3065630, sr_);
    // Warm the filters on noise first so the segment has no start-up transient.
    for (int i = 0; i < 4096; ++i) b.run(a.run(uniform()));
    double sum = 0.0;
    for (int i = 0; i < n; ++i) { seg[i] = b.run(a.run(uniform())); sum += seg[i] * seg[i]; }
    const double g = std::pow(10.0, rmsDb / 20.0) / std::sqrt(sum / std::max(1, n));
    for (double v : seg) x.push_back((float)(v * g));
  }

  // Wide-band white noise normalized to the RMS level (growl / scream / inhalation stand-in).
  void wideNoise(double rmsDb, double sec) {
    const int n = (int)(sec * sr_);
    std::vector<double> seg(n);
    double sum = 0.0;
    for (int i = 0; i < n; ++i) { seg[i] = uniform(); sum += seg[i] * seg[i]; }
    const double g = std::pow(10.0, rmsDb / 20.0) / std::sqrt(sum / std::max(1, n));
    for (double v : seg) x.push_back((float)(v * g));
  }

  void silence(double sec) { x.insert(x.end(), (size_t)(sec * sr_), 0.0f); }

  std::vector<float> x;

private:
  struct Biquad {
    double b0, b1, b2, a1, a2, z1 = 0.0, z2 = 0.0;
    static Biquad highpass(double f, double q, double sr) {
      const double w = 2.0 * kPi * f / sr, c = std::cos(w), al = std::sin(w) / (2.0 * q);
      const double a0 = 1.0 + al;
      return {(1.0 + c) / 2.0 / a0, -(1.0 + c) / a0, (1.0 + c) / 2.0 / a0,
              -2.0 * c / a0, (1.0 - al) / a0};
    }
    double run(double in) {
      const double out = b0 * in + z1;
      z1 = b1 * in - a1 * out + z2;
      z2 = b2 * in - a2 * out;
      return out;
    }
  };
  double uniform() {  // [-1, 1)
    seed_ = seed_ * 1664525u + 1013904223u;
    return (double)(seed_ >> 8) / 8388608.0 - 1.0;
  }
  double sr_;
  double phase_ = 0.0;
  uint32_t seed_ = 0x5EEDu;
};

// Runs the detector one sample at a time and returns the probability aligned to the input:
// the output at sample t describes input sample t - latencySamples().
std::vector<float> runDetector(DSP::BreathDetector& d, const std::vector<float>& x) {
  const size_t lat = (size_t)d.latencySamples();
  std::vector<float> out;
  out.reserve(x.size());
  for (size_t i = 0; i < x.size() + lat; ++i) {
    const float p = d.process(i < x.size() ? x[i] : 0.0f);
    if (i >= lat) out.push_back(p);
  }
  return out;
}

float maxIn(const std::vector<float>& p, double sr, double t0, double t1) {
  const size_t a = (size_t)std::max(0.0, t0 * sr);
  const size_t b = std::min(p.size(), (size_t)(t1 * sr));
  float m = 0.0f;
  for (size_t i = a; i < b; ++i) m = std::max(m, p[i]);
  return m;
}

bool allInUnitRange(const std::vector<float>& p) {
  for (float v : p) if (!(v >= 0.0f && v <= 1.0f)) return false;
  return true;
}

constexpr double kRates[] = {44100.0, 48000.0, 96000.0};
constexpr double kGuard = 0.01;  // s after a breath ends: frame (hop) resolution only

void test_breath_detects_noise_between_phrases() {
  for (double sr : kRates) {
    SignalBuilder s(sr);
    s.saw(200.0, -18.0, 1.0);
    s.hpNoise(-35.0, 0.3);
    s.saw(200.0, -18.0, 1.0);
    DSP::SpectralBreathDetector d;
    d.prepare(sr);
    const auto p = runDetector(d, s.x);
    ASSERT_TRUE(allInUnitRange(p));
    ASSERT_TRUE(maxIn(p, sr, 1.0, 1.3) > 0.6f);
    ASSERT_TRUE(maxIn(p, sr, 0.0, 1.0 - kGuard) < 0.2f);
    ASSERT_TRUE(maxIn(p, sr, 1.3 + kGuard, 2.3) < 0.2f);
  }
}

void test_breath_ignores_quiet_low_voice() {
  for (double sr : kRates) {
    SignalBuilder s(sr);
    s.saw(200.0, -18.0, 1.0);
    s.saw(90.0, -30.0, 1.5);
    DSP::SpectralBreathDetector d;
    d.prepare(sr);
    const auto p = runDetector(d, s.x);
    ASSERT_TRUE(maxIn(p, sr, 0.0, 2.5) < 0.2f);
  }
}

void test_breath_ignores_short_click() {
  for (double sr : kRates) {
    SignalBuilder s(sr);
    s.saw(200.0, -18.0, 1.0);
    s.silence(0.14);
    s.hpNoise(-35.0, 0.02);
    s.silence(0.14);
    s.saw(200.0, -18.0, 0.5);
    DSP::SpectralBreathDetector d;
    d.prepare(sr);
    const auto p = runDetector(d, s.x);
    ASSERT_TRUE(maxIn(p, sr, 0.0, 1.8) < 0.4f);
  }
}

void test_breath_silence_is_zero() {
  for (double sr : kRates) {
    DSP::SpectralBreathDetector d;
    d.prepare(sr);
    for (int i = 0; i < (int)(2 * sr); ++i) ASSERT_TRUE(d.process(0.0f) == 0.0f);

    // Noise before any voice: the phrase tracker has seen nothing, so no breath yet.
    SignalBuilder s(sr);
    s.hpNoise(-35.0, 0.5);
    s.saw(200.0, -18.0, 0.5);
    s.silence(1.0);
    d.reset();
    const auto p = runDetector(d, s.x);
    ASSERT_TRUE(maxIn(p, sr, 0.0, 1.0) == 0.0f);
    for (size_t i = (size_t)((1.0 + kGuard) * sr); i < p.size(); ++i) ASSERT_TRUE(p[i] == 0.0f);
  }
}

void test_breath_sensitivity() {
  for (double sr : kRates) {
    SignalBuilder s(sr);
    s.saw(200.0, -18.0, 1.0);
    s.hpNoise(-42.0, 0.3);
    s.saw(200.0, -18.0, 1.0);
    DSP::SpectralBreathDetector d;
    d.prepare(sr);
    d.setSensitivity(0.0f);
    ASSERT_TRUE(maxIn(runDetector(d, s.x), sr, 0.0, 2.3) < 0.3f);
    d.reset();
    d.setSensitivity(1.0f);
    ASSERT_TRUE(maxIn(runDetector(d, s.x), sr, 1.0, 1.3) > 0.6f);
  }
}

void test_breath_reset_is_deterministic_and_safe() {
  const double sr = 48000.0;
  SignalBuilder s(sr);
  s.saw(200.0, -18.0, 1.0);
  s.hpNoise(-35.0, 0.3);
  s.saw(200.0, -18.0, 0.5);
  DSP::SpectralBreathDetector d;
  d.prepare(sr);
  ASSERT_TRUE(d.latencySamples() == 1024);
  const auto p1 = runDetector(d, s.x);
  d.reset();
  const auto p2 = runDetector(d, s.x);
  ASSERT_TRUE(p1 == p2);
  // Non-finite input never leaks into the output.
  d.reset();
  for (int i = 0; i < 4096; ++i) {
    const float v = d.process(i % 7 == 0 ? std::numeric_limits<float>::quiet_NaN()
                                         : std::numeric_limits<float>::infinity());
    ASSERT_TRUE(std::isfinite(v) && v >= 0.0f && v <= 1.0f);
  }
  DSP::SpectralBreathDetector d44, d96;
  d44.prepare(44100.0);
  d96.prepare(96000.0);
  ASSERT_TRUE(d44.latencySamples() == 1024 && d96.latencySamples() == 2048);
}

// ---------------------------------------------------------------------------
// Level-based breath detector (Puke)
// ---------------------------------------------------------------------------

void test_puke_breath_after_scream() {
  for (double sr : kRates) {
    SignalBuilder s(sr);
    s.wideNoise(-10.0, 1.0);
    s.wideNoise(-35.0, 0.4);
    s.wideNoise(-10.0, 1.0);
    DSP::LevelBreathDetector d;
    d.prepare(sr);
    ASSERT_TRUE(d.latencySamples() == 0);
    const auto p = runDetector(d, s.x);
    ASSERT_TRUE(allInUnitRange(p));
    ASSERT_TRUE(maxIn(p, sr, 1.0, 1.4) > 0.6f);
    ASSERT_TRUE(maxIn(p, sr, 0.0, 1.0) < 0.1f);
    ASSERT_TRUE(maxIn(p, sr, 1.4 + kGuard, 2.4) < 0.1f);
  }
}

void test_puke_growl_not_breath() {
  for (double sr : kRates) {
    SignalBuilder s(sr);
    s.wideNoise(-12.0, 3.0);
    DSP::LevelBreathDetector d;
    d.prepare(sr);
    ASSERT_TRUE(maxIn(runDetector(d, s.x), sr, 0.0, 3.0) < 0.1f);
  }
}

void test_puke_loud_gasp_not_cut() {
  for (double sr : kRates) {
    SignalBuilder s(sr);
    s.wideNoise(-10.0, 1.0);
    s.wideNoise(-14.0, 0.3);
    s.wideNoise(-10.0, 1.0);
    DSP::LevelBreathDetector d;
    d.prepare(sr);
    ASSERT_TRUE(maxIn(runDetector(d, s.x), sr, 0.0, 2.3) < 0.1f);
  }
}

void test_puke_silence_is_zero() {
  for (double sr : kRates) {
    DSP::LevelBreathDetector d;
    d.prepare(sr);
    for (int i = 0; i < (int)(2 * sr); ++i) ASSERT_TRUE(d.process(0.0f) == 0.0f);
    // Scream, then digital silence: the breath context expires and the output is exactly 0.
    SignalBuilder s(sr);
    s.wideNoise(-10.0, 0.5);
    s.silence(1.0);
    d.reset();
    const auto p = runDetector(d, s.x);
    for (size_t i = (size_t)((0.5 + kGuard) * sr); i < p.size(); ++i) ASSERT_TRUE(p[i] == 0.0f);
    // Non-finite input never leaks into the output.
    d.reset();
    for (int i = 0; i < 4096; ++i) {
      const float v = d.process(i % 5 == 0 ? std::numeric_limits<float>::quiet_NaN() : 0.3f);
      ASSERT_TRUE(std::isfinite(v) && v >= 0.0f && v <= 1.0f);
    }
  }
}

void test_puke_sensitivity() {
  for (double sr : kRates) {
    SignalBuilder s(sr);
    s.wideNoise(-10.0, 1.0);
    s.wideNoise(-32.0, 0.3);  // 22 dB under the scream
    s.wideNoise(-10.0, 1.0);
    DSP::LevelBreathDetector d;
    d.prepare(sr);
    d.setSensitivity(0.0f);
    ASSERT_TRUE(maxIn(runDetector(d, s.x), sr, 0.0, 2.3) < 0.3f);
    d.reset();
    d.setSensitivity(1.0f);
    ASSERT_TRUE(maxIn(runDetector(d, s.x), sr, 1.0, 1.3) > 0.6f);
  }
}

float minIn(const std::vector<float>& p, double sr, double t0, double t1) {
  const size_t a = (size_t)std::max(0.0, t0 * sr);
  const size_t b = std::min(p.size(), (size_t)(t1 * sr));
  float m = 1.0f;
  for (size_t i = a; i < b; ++i) m = std::min(m, p[i]);
  return m;
}

// Long breaths (longer than contextMs): the voice context is needed to enter a breath, not to
// stay in one, so the probability holds to the last breath sample and drops right after it.
// Fails if the context check runs on every sample (released ~300 ms into the breath).
void test_puke_long_breath_held_to_end() {
  for (double len : {0.5, 0.6}) {
    for (double sr : kRates) {
      SignalBuilder s(sr);
      s.wideNoise(-10.0, 1.0);
      s.wideNoise(-35.0, len);
      s.wideNoise(-10.0, 1.0);
      DSP::LevelBreathDetector d;
      d.prepare(sr);
      const auto p = runDetector(d, s.x);
      const double b1 = 1.0 + len;
      ASSERT_TRUE(minIn(p, sr, 1.15, b1) > 0.6f);
      ASSERT_TRUE(maxIn(p, sr, 0.0, 1.0) < 0.1f);
      ASSERT_TRUE(maxIn(p, sr, b1 + 0.003, b1 + 1.0) < 0.05f);
    }
  }
}

// A quiet passage after a scream is a breath for at most maxDurationMs (1.5 s); it does not
// re-enter on the same passage, and the next real breath after voice is detected again.
// The passage is 40 dB under the scream so the phrase-tracker decay alone would hold it ~1.7 s.
// After ~2 s the decayed phrase level is within 15 dB of the passage (it counts as "voice" for
// the context); the passage then drops a further 13 dB, which would re-enter a breath if the
// tracker decay could unlock the detector.
void test_puke_breath_duration_cap() {
  for (double sr : kRates) {
    SignalBuilder s(sr);
    s.wideNoise(-5.0, 1.0);
    s.wideNoise(-45.0, 2.2);   // 1.0 .. 3.2 s
    s.wideNoise(-58.0, 0.8);   // 3.2 .. 4.0 s: still the same quiet passage
    s.wideNoise(-5.0, 1.0);    // 4.0 .. 5.0 s
    s.wideNoise(-30.0, 0.3);   // 5.0 .. 5.3 s: a normal breath
    s.wideNoise(-5.0, 1.0);
    DSP::LevelBreathDetector d;
    d.prepare(sr);
    ASSERT_TRUE(d.tuning().maxDurationMs == 1500.0f);
    const auto p = runDetector(d, s.x);
    ASSERT_TRUE(minIn(p, sr, 1.15, 2.45) > 0.6f);
    // The cap counts from the start of the candidate run (~45 ms after the drop: level meter).
    ASSERT_TRUE(maxIn(p, sr, 2.56, 4.0) < 0.05f);
    ASSERT_TRUE(maxIn(p, sr, 4.0, 5.0) < 0.05f);
    ASSERT_TRUE(minIn(p, sr, 5.16, 5.3) > 0.6f);
  }
}

// ---- Corrective EQ ----------------------------------------------------------------------

// Runs one channel of the EQ over x and returns the output.
std::vector<float> runEq(DSP::CorrectiveEq& eq, const std::vector<float>& x) {
  std::vector<float> y(x.size());
  for (size_t i = 0; i < x.size(); ++i) y[i] = eq.process(0, x[i]);
  return y;
}

std::vector<float> sineVec(double sr, double hz, double amp, double sec) {
  std::vector<float> x((size_t)(sec * sr));
  for (size_t i = 0; i < x.size(); ++i) x[i] = (float)(amp * std::sin(2.0 * kPi * hz * (double)i / sr));
  return x;
}

double rmsRange(const std::vector<float>& x, size_t a, size_t b) {
  double s = 0.0;
  for (size_t i = a; i < b; ++i) s += (double)x[i] * x[i];
  return std::sqrt(s / (double)(b - a));
}

// Gain in dB of the EQ on a steady sine (measured over 0.5..0.9 s, after the settle).
double eqGainDb(Mode mode, float amount, double hz, double sr = 48000.0) {
  DSP::CorrectiveEq eq;
  eq.prepare(sr, 1);
  eq.setMode(mode);
  eq.setAmount(amount);
  const auto x = sineVec(sr, hz, 0.1, 1.0);
  const auto y = runEq(eq, x);
  const size_t a = (size_t)(0.5 * sr);
  // Whole cycles only: use the last 0.4 s (an integer number of cycles for the test tones).
  const size_t b = (size_t)(0.9 * sr);
  return 20.0 * std::log10(rmsRange(y, a, b) / rmsRange(x, a, b));
}

void test_eq_hpf_attenuates_rumble() {
  // 12 dB/oct at 80 Hz: ~17 dB at 30 Hz (controller ruling: >= 15 dB).
  ASSERT_TRUE(eqGainDb(Mode::Speak, 0.5f, 30.0) <= -15.0);
}

void test_eq_mud_cut_at_300() {
  ASSERT_NEAR(eqGainDb(Mode::Speak, 0.5f, 300.0), -2.0, 0.5);
  ASSERT_NEAR(eqGainDb(Mode::Puke, 0.5f, 300.0), -4.0, 0.5);
}

void test_eq_passband_flat() {
  for (double sr : kRates) {
    ASSERT_NEAR(eqGainDb(Mode::Speak, 0.5f, 800.0, sr), 0.0, 0.5);
    // 1 kHz: a steady sine is itself a resonance the notch will chase, so measure the
    // static response before the first analysis can act (10..30 ms).
    DSP::CorrectiveEq eq;
    eq.prepare(sr, 1);
    const auto x = sineVec(sr, 1000.0, 0.1, 0.03);
    const auto y = runEq(eq, x);
    const size_t a = (size_t)(0.010 * sr), b = (size_t)(0.030 * sr);
    ASSERT_NEAR(20.0 * std::log10(rmsRange(y, a, b) / rmsRange(x, a, b)), 0.0, 0.5);
  }
}

void test_eq_amount_scales_mud_and_hpf() {
  ASSERT_NEAR(eqGainDb(Mode::Speak, 0.0f, 300.0), 0.0, 0.5);
  ASSERT_TRUE(eqGainDb(Mode::Speak, 0.0f, 40.0) > eqGainDb(Mode::Speak, 0.5f, 40.0));
  ASSERT_NEAR(eqGainDb(Mode::Speak, 1.0f, 300.0), -4.0, 0.5);
}

// White noise (seeded LCG) at the given RMS.
std::vector<float> noiseVec(size_t n, double rms, uint32_t seed = 0x1234u) {
  std::vector<float> x(n);
  for (auto& v : x) {
    seed = seed * 1664525u + 1013904223u;
    v = (float)(((double)(seed >> 8) / 8388608.0 - 1.0) * rms * std::sqrt(3.0));
  }
  return x;
}

// Magnitude of the DFT of x[a, b) at hz (Goertzel-free direct sum).
double toneMag(const std::vector<float>& x, size_t a, size_t b, double hz, double sr) {
  double re = 0.0, im = 0.0;
  for (size_t i = a; i < b; ++i) {
    const double ph = 2.0 * kPi * hz * (double)i / sr;
    re += x[i] * std::cos(ph);
    im += x[i] * std::sin(ph);
  }
  return std::sqrt(re * re + im * im);
}

// Energy (sqrt of summed squared DFT magnitudes on a 20 Hz grid) in a 300 Hz band around hz.
double bandRms(const std::vector<float>& x, size_t a, size_t b, double hz, double sr) {
  double e = 0.0;
  for (double f = hz - 150.0; f <= hz + 150.0; f += 20.0) {
    const double m = toneMag(x, a, b, f, sr);
    e += m * m;
  }
  return std::sqrt(e);
}

void test_eq_notch_cuts_resonance() {
  const double sr = 48000.0;
  auto x = noiseVec((size_t)(1.5 * sr), 0.01);
  // Sine 20 dB above the noise RMS.
  const double amp = 0.01 * 10.0 * std::sqrt(2.0);
  for (size_t i = 0; i < x.size(); ++i) x[i] += (float)(amp * std::sin(2.0 * kPi * 3000.0 * (double)i / sr));
  DSP::CorrectiveEq eq;
  eq.prepare(sr, 1);
  const auto y = runEq(eq, x);
  const size_t a = (size_t)(1.0 * sr), b = (size_t)(1.5 * sr);
  const double dropDb = 20.0 * std::log10(toneMag(x, a, b, 3000.0, sr) / toneMag(y, a, b, 3000.0, sr));
  ASSERT_TRUE(dropDb >= 4.0);
}

void test_eq_notch_idle_on_clean_signal() {
  for (double sr : kRates) {
    const auto x = noiseVec((size_t)(2.5 * sr), 0.1);
    DSP::CorrectiveEq eq;
    eq.prepare(sr, 1);
    const auto y = runEq(eq, x);
    const size_t a = (size_t)(1.5 * sr), b = x.size();
    ASSERT_NEAR(20.0 * std::log10(rmsRange(y, a, b) / rmsRange(x, a, b)), 0.0, 0.5);
    // A wrongly engaged notch is narrow: check the band level at several centres in 1-6 kHz.
    for (double hz : {1200.0, 1800.0, 2500.0, 3000.0, 3700.0, 4500.0, 5300.0}) {
      const double dB = 20.0 * std::log10(bandRms(y, a, b, hz, sr) / bandRms(x, a, b, hz, sr));
      ASSERT_NEAR(dB, 0.0, 0.5);
    }
  }
}

// Noise + a sine 20 dB above it; with loudBump: a sine 6 dB above the noise under a much
// louder broad (non-prominent) bump at 1.3 kHz.
std::vector<float> resonanceSignal(double sr, double sec, double hz, bool loudBump) {
  const double sineDb = loudBump ? 6.0 : 20.0;
  auto x = noiseVec((size_t)(sec * sr), 0.01);
  if (loudBump) {
    DSP::Biquad bump;
    bump.setPeaking(sr, 1300.0, 0.7, 40.0);
    for (auto& v : x) v = bump.process(v);
  }
  const double amp = 0.01 * std::pow(10.0, sineDb / 20.0) * std::sqrt(2.0);
  for (size_t i = 0; i < x.size(); ++i) x[i] += (float)(amp * std::sin(2.0 * kPi * hz * (double)i / sr));
  return x;
}

double notchDropDb(const std::vector<float>& x, DSP::CorrectiveEq& eq, double hz, double sr) {
  const auto y = runEq(eq, x);
  const size_t a = (size_t)(1.0 * sr), b = x.size();
  return 20.0 * std::log10(toneMag(x, a, b, hz, sr) / toneMag(y, a, b, hz, sr));
}

void test_eq_notch_finds_prominent_peak_under_loud_band() {
  const double sr = 48000.0;
  const auto x = resonanceSignal(sr, 1.5, 4000.0, true);
  DSP::CorrectiveEq eq;
  eq.prepare(sr, 1);
  ASSERT_TRUE(notchDropDb(x, eq, 4000.0, sr) >= 4.0);
}

void test_eq_amount_zero_notch_off() {
  const double sr = 48000.0;
  const auto x = resonanceSignal(sr, 1.5, 3000.0, false);
  DSP::CorrectiveEq eq;
  eq.prepare(sr, 1);
  eq.setAmount(0.0f);
  ASSERT_TRUE(notchDropDb(x, eq, 3000.0, sr) < 0.5);
}

// Synthetic vowel: harmonics 1/k shaped by a formant near 3 kHz, with vibrato (+-3 %, 5.5 Hz).
std::vector<float> vowel(double sr, double f0, double sec) {
  std::vector<float> x((size_t)(sec * sr));
  const double vibHz = 5.5, vibDepth = 0.03;
  for (size_t i = 0; i < x.size(); ++i) {
    const double t = (double)i / sr;
    // Integral of f0 * (1 + d sin(2 pi v t)).
    const double ph = 2.0 * kPi * f0 * (t + vibDepth / (2.0 * kPi * vibHz) * (1.0 - std::cos(2.0 * kPi * vibHz * t)));
    double v = 0.0;
    for (int k = 1; k * f0 * (1.0 + vibDepth) < 0.45 * sr; ++k) {
      const double f = k * f0, d = (f - 3000.0) / 500.0;
      v += (1.0 / k) * (1.0 + 4.0 * std::exp(-d * d)) * std::sin(k * ph);
    }
    x[i] = (float)(0.05 * v);
  }
  return x;
}

void test_eq_notch_ignores_vowel_harmonics() {
  const double sr = 48000.0;
  for (double f0 : {300.0, 400.0}) {
    const auto x = vowel(sr, f0, 2.0);
    DSP::CorrectiveEq eq;
    eq.prepare(sr, 1);
    const auto y = runEq(eq, x);
    // Worst 250 Hz band cut over 1-6 kHz in the last second (Hann-windowed 8192 blocks).
    const int n = 8192;
    const size_t a = x.size() - (size_t)sr;
    std::vector<double> pin(n / 2 + 1, 0.0), pout(n / 2 + 1, 0.0);
    std::vector<float> wi(n), wo(n), spec(n / 2 + 1);
    for (size_t off = a; off + n <= x.size(); off += n / 2) {
      for (int i = 0; i < n; ++i) {
        const float w = (float)(0.5 - 0.5 * std::cos(2.0 * kPi * i / n));
        wi[i] = x[off + i] * w;
        wo[i] = y[off + i] * w;
      }
      DSP::fftPowerSpectrum(wi.data(), n, spec.data());
      for (int k = 0; k <= n / 2; ++k) pin[k] += spec[k];
      DSP::fftPowerSpectrum(wo.data(), n, spec.data());
      for (int k = 0; k <= n / 2; ++k) pout[k] += spec[k];
    }
    const double binHz = sr / n;
    double worst = 0.0;
    for (double lo = 1000.0; lo < 6000.0; lo += 250.0) {
      double ei = 0.0, eo = 0.0;
      for (int k = (int)(lo / binHz); k < (int)((lo + 250.0) / binHz); ++k) { ei += pin[k]; eo += pout[k]; }
      worst = std::max(worst, 10.0 * std::log10(ei / eo));
    }
    ASSERT_TRUE(worst < 1.5);
  }
}

void test_eq_ignores_out_of_range_channel() {
  DSP::CorrectiveEq eq;
  eq.prepare(48000.0, 1);
  ASSERT_TRUE(eq.process(3, 0.5f) == 0.5f);
  ASSERT_TRUE(eq.process(-1, 0.25f) == 0.25f);
}

void test_biquad_high_shelf_gain() {
  const double sr = 48000.0;
  for (double g : {6.0, -6.0}) {
    DSP::Biquad bq;
    bq.setHighShelf(sr, 4000.0, g);
    const auto x = sineVec(sr, 16000.0, 0.1, 0.2);
    std::vector<float> y(x.size());
    for (size_t i = 0; i < x.size(); ++i) y[i] = bq.process(x[i]);
    const size_t a = x.size() / 2;
    ASSERT_NEAR(20.0 * std::log10(rmsRange(y, a, x.size()) / rmsRange(x, a, x.size())), g, 0.5);
    DSP::Biquad lo;
    lo.setHighShelf(sr, 4000.0, g);
    const auto xl = sineVec(sr, 200.0, 0.1, 0.2);
    std::vector<float> yl(xl.size());
    for (size_t i = 0; i < xl.size(); ++i) yl[i] = lo.process(xl[i]);
    ASSERT_NEAR(20.0 * std::log10(rmsRange(yl, a, xl.size()) / rmsRange(xl, a, xl.size())), 0.0, 0.5);
  }
}

void test_eq_stereo_channels_identical() {
  const double sr = 48000.0;
  auto x = noiseVec((size_t)(1.0 * sr), 0.01);
  for (size_t i = 0; i < x.size(); ++i) x[i] += (float)(0.05 * std::sin(2.0 * kPi * 3000.0 * (double)i / sr));
  DSP::CorrectiveEq eq;
  eq.prepare(sr, 2);
  std::vector<float> l(x.size()), r(x.size());
  for (size_t i = 0; i < x.size(); ++i) { l[i] = eq.process(0, x[i]); r[i] = eq.process(1, x[i]); }
  for (size_t i = 0; i < x.size(); ++i) ASSERT_TRUE(l[i] == r[i]);
}

void test_eq_amount_change_is_smooth() {
  const double sr = 48000.0;
  DSP::CorrectiveEq eq;
  eq.prepare(sr, 1);
  const auto x = sineVec(sr, 300.0, 0.1, 1.0);
  std::vector<float> y(x.size());
  for (size_t i = 0; i < x.size(); ++i) {
    if (i == x.size() / 2) { eq.setAmount(1.0f); eq.setMode(Mode::Puke); }
    y[i] = eq.process(0, x[i]);
  }
  double maxStep = 0.0;
  for (size_t i = x.size() / 2; i < x.size() / 2 + 4800; ++i)
    maxStep = std::max(maxStep, (double)std::fabs(y[i] - y[i - 1]));
  // A 300 Hz sine of amplitude 0.1 has a max sample step of ~0.0039; allow some headroom.
  ASSERT_TRUE(maxStep < 0.006);
}


// ---- De-esser -------------------------------------------------------------------------------

constexpr double kDsPi = 3.14159265358979323846;

// Runs a stereo sine through the de-esser; returns the output RMS drop in dB over the last
// 100 ms (0 = unchanged), relative to the same sine's input RMS.
double deesserDropDb(double sr, double sineHz, double sineDb, float freqHz, float amount,
                     Mode mode, double seconds = 0.5) {
  DSP::DeEsser d;
  d.prepare(sr, 2);
  d.setMode(mode);
  d.setFreqHz(freqHz);
  d.setAmount(amount);
  d.setListen(false);
  const double amp = std::pow(10.0, sineDb / 20.0);
  const long n = (long)(seconds * sr), tail = (long)(0.1 * sr);
  double si = 0.0, so = 0.0;
  for (long i = 0; i < n; ++i) {
    float f[2];
    f[0] = f[1] = (float)(amp * std::sin(2.0 * kDsPi * sineHz * i / sr));
    const float in = f[0];
    d.processFrame(f);
    if (i >= n - tail) { si += (double)in * in; so += (double)f[0] * f[0]; }
  }
  return 10.0 * std::log10(si / std::max(so, 1e-30));
}

void test_biquad_low_pass() {
  const double sr = 48000.0;
  DSP::Biquad b;
  b.setLowPass(sr, 1000.0, 0.70710678);
  auto gain = [&](double hz) {
    b.reset();
    double s = 0.0;
    const int n = 48000;
    for (int i = 0; i < n; ++i) {
      const float y = b.process((float)std::sin(2.0 * kDsPi * hz * i / sr));
      if (i >= n / 2) s += (double)y * y;
    }
    return 10.0 * std::log10(2.0 * s / (n / 2));
  };
  ASSERT_NEAR(gain(100.0), 0.0, 0.1);
  ASSERT_NEAR(gain(1000.0), -3.0, 0.3);
  ASSERT_TRUE(gain(8000.0) < -30.0);
}

void test_deesser_freq_moves_detection() {
  for (double sr : {44100.0, 48000.0, 96000.0}) {
    ASSERT_TRUE(deesserDropDb(sr, 8000.0, -10.0, 8000.0f, 1.0f, Mode::Speak) >= 6.0);
    {
      const double off = deesserDropDb(sr, 8000.0, -10.0, 4000.0f, 1.0f, Mode::Speak);
      ASSERT_TRUE(off < 3.0 && off > -0.5);
    }
  }
}

void test_deesser_reduces_sibilance() {
  for (double sr : {44100.0, 48000.0, 96000.0})
    ASSERT_TRUE(deesserDropDb(sr, 7000.0, -10.0, 6500.0f, 1.0f, Mode::Speak, 0.1) >= 6.0);
}

void test_deesser_leaves_low_band() {
  for (double sr : {44100.0, 48000.0, 96000.0})
    ASSERT_TRUE(std::fabs(deesserDropDb(sr, 500.0, -10.0, 6500.0f, 1.0f, Mode::Speak)) < 0.5);
}

void test_deesser_amount_zero_is_transparent() {
  DSP::DeEsser d;
  d.prepare(48000.0, 2);
  d.setAmount(0.0f);
  d.setFreqHz(6500.0f);
  uint32_t seed = 12345u;
  double se = 0.0, si = 0.0;
  for (int i = 0; i < 48000; ++i) {
    float f[2];
    for (int c = 0; c < 2; ++c) {
      seed = seed * 1664525u + 1013904223u;
      f[c] = ((seed >> 8) / 8388608.0f - 1.0f) * 0.3f;
    }
    const float in0 = f[0], in1 = f[1];
    d.processFrame(f);
    se += (double)(f[0] - in0) * (f[0] - in0) + (double)(f[1] - in1) * (f[1] - in1);
    si += (double)in0 * in0 + (double)in1 * in1;
  }
  ASSERT_TRUE(se / si < 1e-12);  // ~1e-6 amplitude: float rounding only
}

void test_deesser_puke_nearly_off() {
  {
    const double d = deesserDropDb(48000.0, 7000.0, -10.0, 6500.0f, 1.0f, Mode::Puke);
    ASSERT_TRUE(d < 3.0 && d > -0.5);
  }
}

void test_deesser_listen_outputs_removed_band() {
  auto rms = [](double hz) {
    DSP::DeEsser d;
    d.prepare(48000.0, 2);
    d.setFreqHz(6500.0f);
    d.setAmount(1.0f);
    d.setListen(true);
    const double amp = std::pow(10.0, -10.0 / 20.0);
    double s = 0.0;
    for (int i = 0; i < 24000; ++i) {
      float f[2];
      f[0] = f[1] = (float)(amp * std::sin(2.0 * kDsPi * hz * i / 48000.0));
      d.processFrame(f);
      if (i >= 12000) s += (double)f[0] * f[0];
    }
    return 10.0 * std::log10(std::max(s / 12000.0, 1e-30));
  };
  ASSERT_TRUE(rms(7000.0) > -30.0);
  ASSERT_TRUE(rms(500.0) < -40.0);
}

void test_deesser_finite_at_extreme_rates() {
  for (double sr : {22050.0, 192000.0}) {
    DSP::DeEsser d;
    d.prepare(sr, 2);
    d.setFreqHz(10000.0f);
    d.setAmount(1.0f);
    bool ok = true;
    for (int i = 0; i < (int)(sr / 10); ++i) {
      float f[2] = {(float)(0.3 * std::sin(0.2 * i)), (float)(0.3 * std::sin(0.2 * i))};
      d.processFrame(f);
      ok = ok && std::isfinite(f[0]) && std::isfinite(f[1]) && std::fabs(f[0]) < 2.0f;
    }
    ASSERT_TRUE(ok);
  }
}

void test_deesser_gain_is_stereo_linked() {
  // R is exactly half of L, same frequency. Linear high band => out_R == 0.5 * out_L only if
  // both channels get the same gain (independent detectors would reduce R less).
  DSP::DeEsser d;
  d.prepare(48000.0, 2);
  d.setFreqHz(6500.0f);
  d.setAmount(1.0f);
  double maxErr = 0.0, maxDiff = 0.0;
  for (int i = 0; i < 24000; ++i) {
    const float x = (float)(0.316 * std::sin(2.0 * kDsPi * 7000.0 * i / 48000.0));
    float f[2] = {x, 0.5f * x};
    d.processFrame(f);
    if (i >= 12000) {
      maxErr = std::max(maxErr, (double)std::fabs(f[1] - 0.5f * f[0]));
      maxDiff = std::max(maxDiff, (double)std::fabs(f[0] - x));
    }
  }
  ASSERT_TRUE(maxDiff > 0.1);   // reduction really is active
  ASSERT_TRUE(maxErr < 1e-5);
}

// Largest deviation from a pure 7 kHz sinusoid (second-order recurrence residual) of the output
// while Freq is toggled (or not) every 50 ms with reduction active. A smooth gain/coefficient
// glide leaves a small residual; an instant coefficient jump makes a click.
double deesserMaxResidual(bool toggle) {
  const double sr = 48000.0, w = 2.0 * kDsPi * 7000.0 / sr;
  DSP::DeEsser d;
  d.prepare(sr, 2);
  d.setMode(Mode::Speak);
  d.setAmount(1.0f);
  d.setFreqHz(toggle ? 4000.0f : 10000.0f);
  double y1 = 0.0, y2 = 0.0, worst = 0.0;
  for (int i = 0; i < 48000; ++i) {
    if (toggle && i % 2400 == 0) d.setFreqHz((i / 2400) % 2 ? 10000.0f : 4000.0f);
    float f[2];
    f[0] = f[1] = (float)(0.316 * std::sin(w * i));
    d.processFrame(f);
    if (i > 4800) worst = std::max(worst, std::fabs(f[0] - 2.0 * std::cos(w) * y1 + y2));
    y2 = y1;
    y1 = f[0];
  }
  return worst;
}

void test_deesser_freq_change_is_smooth() {
  const double steady = deesserMaxResidual(false);
  const double moving = deesserMaxResidual(true);
  ASSERT_TRUE(steady < 1e-3);
  ASSERT_TRUE(moving < 0.02);
}

// ---- Compressor (distressor-style) ------------------------------------------------------------

struct CompCfg {
  Mode mode = Mode::Speak;
  float amount = 1.0f;
  float mix = 1.0f;
  int ratio = 0;  // AUTO
  int dist = 0;   // AUTO
  int hpf = 1;    // 80 Hz
};

void setupComp(DSP::DistressorComp& c, const CompCfg& k, double sr) {
  c.prepare(sr, 2);
  c.setMode(k.mode);
  c.setAmount(k.amount);
  c.setMix(k.mix);
  c.setRatioChoice(k.ratio);
  c.setDistChoice(k.dist);
  c.setDetectorHpf(k.hpf);
  c.reset();  // start from the settled parameter values
}

struct CompRun {
  std::vector<float> in, out;
  double meanGr = 0.0;  // mean lastGainReductionDb() over the last 0.2 s
};

// Stereo sine (both channels identical) of the given peak amplitude through the compressor.
CompRun runCompSine(const CompCfg& k, double sr, double hz, double peak, double sec = 1.0) {
  DSP::DistressorComp c;
  setupComp(c, k, sr);
  CompRun r;
  r.in = sineVec(sr, hz, peak, sec);
  r.out.resize(r.in.size());
  const size_t tail = (size_t)(0.2 * sr);
  double grSum = 0.0;
  for (size_t i = 0; i < r.in.size(); ++i) {
    float f[2] = {r.in[i], r.in[i]};
    c.processFrame(f);
    r.out[i] = f[0];
    if (i >= r.in.size() - tail) grSum += c.lastGainReductionDb();
  }
  r.meanGr = grSum / (double)tail;
  return r;
}

// Level of the n-th harmonic re the fundamental, in dB, over 0.5..0.9 s (whole cycles of 1 kHz).
double harmDb(const CompRun& r, double sr, double hz, int n) {
  const size_t a = (size_t)(0.5 * sr), b = (size_t)(0.9 * sr);
  return 20.0 * std::log10(std::max(toneMag(r.out, a, b, n * hz, sr), 1e-30) /
                           toneMag(r.out, a, b, hz, sr));
}

// Output/input RMS ratio in dB over 0.5..0.9 s.
double compGainDb(const CompRun& r, double sr) {
  const size_t a = (size_t)(0.5 * sr), b = (size_t)(0.9 * sr);
  return 20.0 * std::log10(rmsRange(r.out, a, b) / rmsRange(r.in, a, b));
}

constexpr double kMinus6 = 0.501187;    // -6 dBFS peak
constexpr double kMinus12 = 0.251189;   // -12 dBFS peak

void test_comp_reduces_peaks() {
  // -6 dBFS against a -30 dBFS threshold at 2:1: static reduction 12 dB.
  for (double sr : kRates) {
    const double gr = runCompSine({Mode::Speak, 1.0f}, sr, 1000.0, kMinus6).meanGr;
    ASSERT_TRUE(gr > 6.0);
    ASSERT_NEAR(gr, 12.0, 1.0);
  }
}

void test_comp_ratio_per_mode() {
  // Static: Speak 2:1 -> 12 dB, Sing 3:1 -> 16 dB, Puke 8:1 -> 21 dB.
  for (double sr : kRates) {
    const double sp = runCompSine({Mode::Speak, 1.0f}, sr, 1000.0, kMinus6).meanGr;
    const double si = runCompSine({Mode::Sing, 1.0f}, sr, 1000.0, kMinus6).meanGr;
    const double pu = runCompSine({Mode::Puke, 1.0f}, sr, 1000.0, kMinus6).meanGr;
    ASSERT_TRUE(pu > si + 2.0);
    ASSERT_TRUE(si > sp + 2.0);
    ASSERT_NEAR(si, 16.0, 1.0);
    ASSERT_NEAR(pu, 21.0, 1.0);
  }
}

void test_comp_ratio_choice_overrides_mode() {
  for (double sr : kRates) {
    const double autoGr = runCompSine({Mode::Speak, 1.0f}, sr, 1000.0, kMinus6).meanGr;
    const double tenGr = runCompSine({Mode::Speak, 1.0f, 1.0f, 6}, sr, 1000.0, kMinus6).meanGr;
    ASSERT_TRUE(tenGr > autoGr + 5.0);
    ASSERT_NEAR(tenGr, 21.6, 1.0);
  }
}

void test_comp_auto_equals_mode_ratio() {
  for (double sr : kRates) {
    const double a = runCompSine({Mode::Sing, 1.0f}, sr, 1000.0, kMinus6).meanGr;
    const double c = runCompSine({Mode::Sing, 1.0f, 1.0f, 2}, sr, 1000.0, kMinus6).meanGr;
    ASSERT_NEAR(a, c, 0.1);
  }
  // The other modes too: Speak AUTO = 2:1, Puke AUTO = 8:1.
  const double sa = runCompSine({Mode::Speak, 1.0f}, 48000.0, 1000.0, kMinus6).meanGr;
  const double sc = runCompSine({Mode::Speak, 1.0f, 1.0f, 1}, 48000.0, 1000.0, kMinus6).meanGr;
  ASSERT_NEAR(sa, sc, 0.1);
  const double pa = runCompSine({Mode::Puke, 1.0f}, 48000.0, 1000.0, kMinus6).meanGr;
  const double pc = runCompSine({Mode::Puke, 1.0f, 1.0f, 5}, 48000.0, 1000.0, kMinus6).meanGr;
  ASSERT_NEAR(pa, pc, 0.1);
}

void test_comp_ratio_20() {
  // Static: 10:1 -> 21.6 dB, 20:1 -> 22.8 dB.
  for (double sr : kRates) {
    const double ten = runCompSine({Mode::Speak, 1.0f, 1.0f, 6}, sr, 1000.0, kMinus6).meanGr;
    const double twenty = runCompSine({Mode::Speak, 1.0f, 1.0f, 7}, sr, 1000.0, kMinus6).meanGr;
    ASSERT_TRUE(twenty > ten + 0.5);
    ASSERT_NEAR(twenty, 22.8, 1.0);
  }
}

// DC step (detector HPF OFF so the detector sees a constant level): 0.5 for 0.5 s, then silence
// for 4 s. attackMs = time for the reduction to reach 63 % of its settled value; releaseMs = time
// from the start of the silence until it falls to 37 % of the value it had there.
void measureCompTimes(double sr, int ratioChoice, double& attackMs, double& releaseMs) {
  DSP::DistressorComp c;
  setupComp(c, {Mode::Speak, 1.0f, 1.0f, ratioChoice, 1, 0}, sr);
  const long on = (long)(0.5 * sr), off = (long)(4.0 * sr);
  std::vector<double> gr((size_t)(on + off));
  for (long i = 0; i < on + off; ++i) {
    float f[2];
    f[0] = f[1] = i < on ? 0.5f : 0.0f;
    c.processFrame(f);
    gr[(size_t)i] = c.lastGainReductionDb();
  }
  const double settled = gr[(size_t)on - 1];
  attackMs = releaseMs = -1.0;
  for (long i = 0; i < on; ++i)
    if (gr[(size_t)i] >= 0.632 * settled) { attackMs = 1000.0 * (i + 1) / sr; break; }
  for (long i = on; i < on + off; ++i)
    if (gr[(size_t)i] <= 0.368 * settled) { releaseMs = 1000.0 * (i - on + 1) / sr; break; }
}

void test_comp_times_scale_with_ratio() {
  // Attack 6/r ms: 2:1 -> 3 ms, 20:1 -> 0.3 ms. Sustained release: 2:1 -> 200 ms, 20:1 -> 2 s.
  for (double sr : kRates) {
    double a2, r2, a20, r20;
    measureCompTimes(sr, 1, a2, r2);
    measureCompTimes(sr, 7, a20, r20);
    ASSERT_TRUE(a20 > 0.0 && a20 < a2);
    ASSERT_TRUE(r20 > r2 && r2 > 0.0);
    ASSERT_NEAR(a2, 3.0, 0.5);
    ASSERT_NEAR(a20, 0.3, 0.12);
    ASSERT_NEAR(r2, 200.0, 40.0);
    ASSERT_NEAR(r20, 2000.0, 300.0);
  }
}

void test_comp_detector_hpf() {
  // 80 Hz 2nd-order Butterworth: 40 Hz is ~12.3 dB down in the detector -> about half the
  // reduction of 1 kHz (24 dB over -> 11.7 dB over). A steeper filter would give ~0.
  for (double sr : kRates) {
    const double lo = runCompSine({Mode::Speak, 1.0f}, sr, 40.0, kMinus6).meanGr;
    const double hi = runCompSine({Mode::Speak, 1.0f}, sr, 1000.0, kMinus6).meanGr;
    ASSERT_TRUE(lo < 0.5 * hi);
    ASSERT_TRUE(lo > 0.3 * hi);
  }
}

void test_comp_detector_hpf_choices() {
  // 100 Hz at -6 dBFS, 2:1: OFF -> 12 dB, 80 Hz (-1.5 dB) -> 11.25 dB, 150 Hz (-7.8 dB) -> 8.1 dB.
  for (double sr : kRates) {
    const double off = runCompSine({Mode::Speak, 1.0f, 1.0f, 0, 0, 0}, sr, 100.0, kMinus6).meanGr;
    const double h80 = runCompSine({Mode::Speak, 1.0f, 1.0f, 0, 0, 1}, sr, 100.0, kMinus6).meanGr;
    const double h150 = runCompSine({Mode::Speak, 1.0f, 1.0f, 0, 0, 2}, sr, 100.0, kMinus6).meanGr;
    ASSERT_TRUE(h150 < h80 - 1.5);
    ASSERT_TRUE(h80 < off - 0.3);
    ASSERT_NEAR(off, 12.0, 1.0);
  }
}

void test_comp_unity_below_threshold() {
  // -50 dBFS against a -18 dBFS threshold (Amount 0.5): no reduction, no makeup.
  const double amp = std::pow(10.0, -50.0 / 20.0);
  for (double sr : kRates) {
    for (Mode m : {Mode::Speak, Mode::Sing}) {
      const auto r = runCompSine({m, 0.5f}, sr, 1000.0, amp);
      ASSERT_NEAR(compGainDb(r, sr), 0.0, 0.5);
    }
  }
}

// Puke AUTO (Dist 3) with no reduction: the wet path is flat, because only the added distortion
// products are band-limited (30 Hz .. 12 kHz), not the signal. Amount 0 -> threshold -6 dBFS,
// makeup max(0, GR_ref - 2) = 0, so a -40 dBFS sine sees unity gain; at Mix 0.5 a filtered wet
// path would also comb against the dry one. Fails with the old LP12k(HP30(x) + ...) structure
// (16 kHz about -10 dB, 40 Hz about -1.5 dB).
void test_comp_puke_no_reduction_is_flat() {
  const double amp = std::pow(10.0, -40.0 / 20.0);
  for (double sr : kRates) {
    for (float mix : {1.0f, 0.5f}) {
      for (double hz : {40.0, 1000.0, 16000.0}) {
        const auto r = runCompSine({Mode::Puke, 0.0f, mix}, sr, hz, amp);
        ASSERT_TRUE(r.meanGr == 0.0);
        ASSERT_NEAR(compGainDb(r, sr), 0.0, 0.5);
      }
    }
  }
}

void test_comp_level_compensation() {
  // -18 dBFS RMS sine (-15 dBFS peak): output within +-3 dB of the input for any Amount.
  const double peak = std::pow(10.0, -18.0 / 20.0) * std::sqrt(2.0);
  for (double sr : kRates) {
    for (Mode m : {Mode::Speak, Mode::Sing, Mode::Puke}) {
      for (float amount : {0.0f, 0.5f, 1.0f}) {
        for (int ratio : {0, 7}) {
          const auto r = runCompSine({m, amount, 1.0f, ratio}, sr, 1000.0, peak);
          ASSERT_NEAR(compGainDb(r, sr), 0.0, 3.0);
        }
      }
    }
  }
}

void test_comp_dist2_adds_even_harmonics() {
  for (double sr : kRates) {
    const auto on = runCompSine({Mode::Sing, 1.0f}, sr, 1000.0, kMinus12);
    const auto off = runCompSine({Mode::Sing, 1.0f, 1.0f, 0, 1}, sr, 1000.0, kMinus12);
    ASSERT_TRUE(harmDb(on, sr, 1000.0, 2) > harmDb(on, sr, 1000.0, 3) + 6.0);
    ASSERT_TRUE(harmDb(on, sr, 1000.0, 2) > harmDb(off, sr, 1000.0, 2) + 20.0);
  }
}

void test_comp_dist3_adds_odd_harmonics() {
  for (double sr : kRates) {
    const auto on = runCompSine({Mode::Puke, 1.0f}, sr, 1000.0, kMinus12);
    const auto off = runCompSine({Mode::Puke, 1.0f, 1.0f, 0, 1}, sr, 1000.0, kMinus12);
    ASSERT_TRUE(harmDb(on, sr, 1000.0, 3) > harmDb(on, sr, 1000.0, 2) + 6.0);
    ASSERT_TRUE(harmDb(on, sr, 1000.0, 3) > harmDb(off, sr, 1000.0, 3) + 10.0);
  }
}

void test_comp_dist_choice_overrides_mode() {
  // Speak AUTO has no distortion; choice Dist 3 adds it.
  for (double sr : kRates) {
    const auto d3 = runCompSine({Mode::Speak, 1.0f, 1.0f, 0, 3}, sr, 1000.0, kMinus12);
    const auto au = runCompSine({Mode::Speak, 1.0f}, sr, 1000.0, kMinus12);
    ASSERT_TRUE(harmDb(d3, sr, 1000.0, 3) > harmDb(d3, sr, 1000.0, 2) + 6.0);
    ASSERT_TRUE(harmDb(d3, sr, 1000.0, 3) > harmDb(au, sr, 1000.0, 3) + 10.0);
  }
}

void test_comp_dist2_level() {
  // Sing 3:1, -12 dBFS against -30 dBFS: 12 dB of reduction -> about 3 % second harmonic.
  for (double sr : kRates) {
    const auto r = runCompSine({Mode::Sing, 1.0f, 1.0f, 0, 2}, sr, 1000.0, kMinus12);
    ASSERT_NEAR(r.meanGr, 12.0, 1.0);
    const double h2 = harmDb(r, sr, 1000.0, 2);
    ASSERT_TRUE(h2 >= -40.0 && h2 <= -26.0);
  }
}

void test_comp_distortion_grows_with_gr() {
  // Same -12 dBFS input; Amount 1 (12 dB) vs Amount 0.29 (threshold -12.96, in the knee: <1 dB).
  for (double sr : kRates) {
    const auto hi = runCompSine({Mode::Sing, 1.0f, 1.0f, 0, 2}, sr, 1000.0, kMinus12);
    const auto lo = runCompSine({Mode::Sing, 0.29f, 1.0f, 0, 2}, sr, 1000.0, kMinus12);
    ASSERT_NEAR(hi.meanGr, 12.0, 1.0);
    ASSERT_TRUE(lo.meanGr <= 1.0 && lo.meanGr >= 0.3);
    ASSERT_TRUE(harmDb(hi, sr, 1000.0, 2) >= harmDb(lo, sr, 1000.0, 2) + 20.0);
  }
}

void test_comp_mix_zero_is_dry() {
  for (double sr : kRates) {
    const auto dry = runCompSine({Mode::Puke, 1.0f, 0.0f}, sr, 1000.0, kMinus6);
    const auto wet = runCompSine({Mode::Puke, 1.0f, 1.0f}, sr, 1000.0, kMinus6);
    double maxErr = 0.0, maxWet = 0.0;
    for (size_t i = 0; i < dry.in.size(); ++i) {
      maxErr = std::max(maxErr, (double)std::fabs(dry.out[i] - dry.in[i]));
      maxWet = std::max(maxWet, (double)std::fabs(wet.out[i] - wet.in[i]));
    }
    ASSERT_TRUE(maxWet > 0.05);  // the wet path really differs
    ASSERT_TRUE(maxErr <= 1e-6);
  }
}

void test_comp_extreme_input_finite() {
  // Full-scale white noise (+-1) for 1 s, with a few NaN/Inf samples injected.
  const double sr = 48000.0;
  auto x = noiseVec((size_t)sr, 1.0 / std::sqrt(3.0));
  const float inf = std::numeric_limits<float>::infinity();
  x[1000] = std::numeric_limits<float>::quiet_NaN();
  x[2000] = inf;
  x[3000] = -inf;
  for (Mode m : {Mode::Speak, Mode::Sing, Mode::Puke}) {
    for (float amount : {0.0f, 1.0f}) {
      for (int dist : {2, 3}) {
        for (int ratio : {0, 7}) {
          DSP::DistressorComp c;
          setupComp(c, {m, amount, 1.0f, ratio, dist}, sr);
          int bad = 0;
          for (size_t i = 0; i < x.size(); ++i) {
            float f[2] = {x[i], -x[i]};
            c.processFrame(f);
            const float g = c.lastGainReductionDb();
            if (!std::isfinite(f[0]) || !std::isfinite(f[1]) || std::fabs(f[0]) > 2.0f ||
                std::fabs(f[1]) > 2.0f || !std::isfinite(g) || g < 0.0f) ++bad;
          }
          ASSERT_TRUE(bad == 0);
        }
      }
    }
  }
}

void test_comp_stereo_linked() {
  // L = 0.5 * R, no distortion: the louder channel (R) drives the detector (max link), both
  // channels get the same gain (out_L == 0.5 * out_R) and the reduction equals that of R alone.
  const double sr = 48000.0;
  DSP::DistressorComp c;
  setupComp(c, {Mode::Speak, 1.0f, 1.0f, 0, 1}, sr);
  const auto x = sineVec(sr, 1000.0, kMinus6, 1.0);
  const size_t tail = (size_t)(0.2 * sr);
  double maxErr = 0.0, maxDiff = 0.0, grSum = 0.0;
  for (size_t i = 0; i < x.size(); ++i) {
    float f[2] = {0.5f * x[i], x[i]};
    c.processFrame(f);
    if (i >= x.size() / 2) {
      maxErr = std::max(maxErr, (double)std::fabs(f[0] - 0.5f * f[1]));
      maxDiff = std::max(maxDiff, (double)std::fabs(f[1] - x[i]));
    }
    if (i >= x.size() - tail) grSum += c.lastGainReductionDb();
  }
  const double mono = runCompSine({Mode::Speak, 1.0f, 1.0f, 0, 1}, sr, 1000.0, kMinus6).meanGr;
  ASSERT_TRUE(maxDiff > 0.05);
  ASSERT_TRUE(maxErr < 1e-6);
  ASSERT_NEAR(grSum / (double)tail, mono, 0.05);
}

void test_comp_soft_knee() {
  // Amount 0.5 (threshold -18 dBFS), 2:1, 6 dB knee: a sine peaking at the threshold gets
  // (1 - 1/2) * 3^2 / 12 = 0.375 dB (a hard knee gives 0), 3 dB below it none, 3 dB above 1.5 dB.
  // The peak detector settles ~8 % below the static value on a sine (attack only near the peaks).
  const double at = std::pow(10.0, -18.0 / 20.0);
  for (double sr : kRates) {
    const double g0 = runCompSine({Mode::Speak, 0.5f}, sr, 1000.0, at).meanGr;
    const double gm = runCompSine({Mode::Speak, 0.5f}, sr, 1000.0, at * 0.70795).meanGr;
    const double gp = runCompSine({Mode::Speak, 0.5f}, sr, 1000.0, at * 1.41254).meanGr;
    ASSERT_NEAR(g0, 0.375, 0.1);
    ASSERT_NEAR(gm, 0.0, 0.02);
    ASSERT_NEAR(gp, 1.5, 0.25);
  }
}

void test_comp_param_changes_are_smooth() {
  // 300 Hz sine (max sample step ~0.004): Amount, Mix and Dist toggled every 50 ms. Smoothed
  // makeup, threshold, mix and distortion crossfade keep the output steps small; an instant
  // makeup jump alone would be a step of ~0.1.
  const double sr = 48000.0;
  DSP::DistressorComp c;
  setupComp(c, {Mode::Sing, 1.0f, 1.0f, 1, 1}, sr);
  const auto x = sineVec(sr, 300.0, 0.1, 1.0);
  float prev = 0.0f;
  double maxStep = 0.0;
  const long period = 2437;  // ~50 ms, not a whole number of cycles: toggles hit any phase
  for (size_t i = 0; i < x.size(); ++i) {
    if (i % (size_t)period == 0) {
      const long k = (long)(i / (size_t)period);
      c.setAmount(k % 2 ? 0.0f : 1.0f);
      c.setMix(k % 3 ? 1.0f : 0.3f);
      c.setDistChoice(k % 2 ? 3 : 1);
    }
    float f[2] = {x[i], x[i]};
    c.processFrame(f);
    if (i > 0) maxStep = std::max(maxStep, (double)std::fabs(f[0] - prev));
    prev = f[0];
  }
  ASSERT_TRUE(maxStep < 0.012);
}

// ---- Tone EQ and mode presets ---------------------------------------------------------------

// Stereo sine through the tone EQ; returns the gain in dB measured over the last 100 ms.
double toneGainDb(double sr, double hz, Mode mode, float tone) {
  DSP::ToneEq eq;
  eq.prepare(sr, 2);
  eq.setMode(mode);
  eq.setTone(tone);
  const long n = (long)(0.6 * sr), tail = (long)(0.1 * sr);
  const double w = 2.0 * kPi * hz / sr;
  double sIn = 0.0, sOut = 0.0;
  for (long i = 0; i < n; ++i) {
    const float x = (float)(0.1 * std::sin(w * (double)i));
    const float yl = eq.process(0, x);
    const float yr = eq.process(1, x);
    if (i >= n - tail) { sIn += (double)x * x; sOut += (double)yl * yl; (void)yr; }
  }
  return 10.0 * std::log10(sOut / sIn);
}

constexpr double kToneRates[3] = {44100.0, 48000.0, 96000.0};

void test_tone_neutral_is_flat() {
  int bad = 0;
  for (double sr : kToneRates)
    for (Mode m : {Mode::Speak, Mode::Sing, Mode::Puke})
      for (double hz : {250.0, 3500.0, 12000.0})
        if (std::fabs(toneGainDb(sr, hz, m, 0.5f)) > 0.3) ++bad;
  ASSERT_TRUE(bad == 0);
}

void test_tone_up_speak() {
  int bad = 0;
  for (double sr : kToneRates) {
    if (std::fabs(toneGainDb(sr, 3500.0, Mode::Speak, 1.0f) - 3.0) > 0.7) ++bad;
    if (std::fabs(toneGainDb(sr, 250.0, Mode::Speak, 1.0f) + 2.0) > 0.7) ++bad;
  }
  ASSERT_TRUE(bad == 0);
}

void test_tone_down_inverts() {
  int bad = 0;
  for (double sr : kToneRates) {
    if (std::fabs(toneGainDb(sr, 3500.0, Mode::Speak, 0.0f) + 3.0) > 0.7) ++bad;
    if (std::fabs(toneGainDb(sr, 250.0, Mode::Speak, 0.0f) - 2.0) > 0.7) ++bad;
    // Linear scaling: half way down (t = -0.5) gives half the presence cut.
    if (std::fabs(toneGainDb(sr, 3500.0, Mode::Speak, 0.25f) + 1.5) > 0.5) ++bad;
  }
  ASSERT_TRUE(bad == 0);
}

void test_tone_air_shelf_gain() {
  // Well above the 12 kHz corner a shelf reaches (nearly) its full gain, and well below it
  // stays flat. Two-sided around the per-mode values {Speak 2, Sing 4, Puke 0}.
  int bad = 0;
  for (double sr : {48000.0, 96000.0}) {
    const double expect[3] = {2.0, 4.0, 0.0};
    const Mode modes[3] = {Mode::Speak, Mode::Sing, Mode::Puke};
    for (int k = 0; k < 3; ++k) {
      const double g = toneGainDb(sr, 20000.0, modes[k], 1.0f);
      if (g > expect[k] + 0.7 || g < expect[k] * 0.75 - 0.3) ++bad;
      if (std::fabs(toneGainDb(sr, 1000.0, modes[k], 1.0f)) > 0.8) ++bad;
    }
  }
  ASSERT_TRUE(bad == 0);
}

void test_tone_mode_curves_differ() {
  int bad = 0;
  for (double sr : kToneRates) {
    const double sing = toneGainDb(sr, 12000.0, Mode::Sing, 1.0f);
    const double speak = toneGainDb(sr, 12000.0, Mode::Speak, 1.0f);
    const double puke = toneGainDb(sr, 12000.0, Mode::Puke, 1.0f);
    if (!(sing > speak + 0.5 && speak > puke + 0.5)) ++bad;
    // Mud cut and presence differ per mode too.
    if (std::fabs(toneGainDb(sr, 250.0, Mode::Puke, 1.0f) + 3.0) > 0.7) ++bad;
    if (std::fabs(toneGainDb(sr, 250.0, Mode::Sing, 1.0f) + 1.5) > 0.7) ++bad;
    if (std::fabs(toneGainDb(sr, 3500.0, Mode::Puke, 1.0f) - 1.0) > 0.7) ++bad;
    if (std::fabs(toneGainDb(sr, 3500.0, Mode::Sing, 1.0f) - 2.0) > 0.7) ++bad;
  }
  ASSERT_TRUE(bad == 0);
}

void test_tone_low_rate_shelf_stays_below_nyquist() {
  // At 22.05 kHz the 12 kHz corner would sit at Nyquist: it is clamped to 0.45 x rate, so the
  // output stays finite, bounded and the shelf still boosts the top of the band.
  DSP::ToneEq eq;
  eq.prepare(22050.0, 1);
  eq.setMode(Mode::Sing);
  eq.setTone(1.0f);
  double peak = 0.0;
  int bad = 0;
  for (long i = 0; i < 22050; ++i) {
    const float y = eq.process(0, (float)(0.1 * std::sin(2.0 * kPi * 10500.0 * (double)i / 22050.0)));
    if (!std::isfinite(y)) ++bad;
    if (i > 11025) peak += (double)y * y;
  }
  ASSERT_TRUE(bad == 0);
  const double g = 10.0 * std::log10(peak / (11024.0 * 0.5 * 0.01));
  ASSERT_TRUE(g > 1.5 && g < 4.7);
}

void test_tone_nonfinite_input_and_tone_are_safe() {
  DSP::ToneEq eq;
  eq.prepare(48000.0, 2);
  eq.setTone(std::numeric_limits<float>::quiet_NaN());
  const float bad[3] = {std::numeric_limits<float>::quiet_NaN(),
                        std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
  int nonFinite = 0;
  for (int i = 0; i < 3000; ++i) {
    const float x = i % 100 < 3 ? bad[i % 3] : 0.1f;
    if (!std::isfinite(eq.process(0, x))) ++nonFinite;
    if (!std::isfinite(eq.process(1, x))) ++nonFinite;
  }
  ASSERT_TRUE(nonFinite == 0);
}

void test_tone_stereo_channels_identical() {
  DSP::ToneEq eq;
  eq.prepare(48000.0, 2);
  eq.setMode(Mode::Sing);
  const auto x = sineVec(48000.0, 700.0, 0.1, 0.5);
  int diff = 0;
  for (size_t i = 0; i < x.size(); ++i) {
    if (i == x.size() / 2) eq.setTone(1.0f);
    if (eq.process(0, x[i]) != eq.process(1, x[i])) ++diff;
  }
  ASSERT_TRUE(diff == 0);
}

// Tone and Mode changed on a running sine: the output must stay a smooth waveform. The step is
// compared with the largest step of the steady-state output just before the change.
void test_tone_changes_are_smooth() {
  const double sr = 48000.0;
  int bad = 0;
  for (double hz : {250.0, 3500.0}) {
    DSP::ToneEq eq;
    eq.prepare(sr, 1);
    eq.setMode(Mode::Speak);
    const auto x = sineVec(sr, hz, 0.3, 1.0);
    std::vector<float> y(x.size());
    for (size_t i = 0; i < x.size(); ++i) {
      if (i == x.size() / 2) { eq.setTone(1.0f); eq.setMode(Mode::Sing); }
      if (i == x.size() / 2 + 12000) { eq.setTone(0.0f); eq.setMode(Mode::Puke); }
      y[i] = eq.process(0, x[i]);
    }
    double before = 0.0, after = 0.0;
    for (size_t i = x.size() / 2 - 4800; i < x.size() / 2; ++i)
      before = std::max(before, (double)std::fabs(y[i] - y[i - 1]));
    for (size_t i = x.size() / 2; i < x.size() / 2 + 24000; ++i)
      after = std::max(after, (double)std::fabs(y[i] - y[i - 1]));
    // The new curve legitimately changes amplitude by up to ~+3 dB (x1.41); an instant jump
    // adds a transient on top. Allow the amplitude change, not a click.
    if (!(after < before * 1.5)) ++bad;
  }
  ASSERT_TRUE(bad == 0);
}

void test_presets_values() {
  ASSERT_NEAR(presetFor(Mode::Speak).riderMaxRangeDb, 6.0, 1e-9);
  ASSERT_NEAR(presetFor(Mode::Speak).riderSlewDbPerSec, 8.0, 1e-9);
  ASSERT_NEAR(presetFor(Mode::Sing).riderMaxRangeDb, 4.0, 1e-9);
  ASSERT_NEAR(presetFor(Mode::Sing).riderSlewDbPerSec, 4.0, 1e-9);
  ASSERT_NEAR(presetFor(Mode::Puke).riderMaxRangeDb, 3.0, 1e-9);
  ASSERT_NEAR(presetFor(Mode::Puke).riderSlewDbPerSec, 2.0, 1e-9);
  ASSERT_TRUE(&presetFor(Mode::Sing) == &kModePresets[1]);
}


// ---- Mode presets (ODR fix) ----

void test_presets_out_of_range_mode_is_clamped() {
  ASSERT_TRUE(&presetFor(static_cast<Mode>(-1)) == &kModePresets[0]);
  ASSERT_TRUE(&presetFor(static_cast<Mode>(7)) == &kModePresets[2]);
  ASSERT_NEAR(presetFor(static_cast<Mode>(99)).riderMaxRangeDb, 3.0, 1e-9);
}

// ---- DistressorComp: huge finite input ----

void test_comp_huge_finite_input_recovers() {
  // Samples near FLT_MAX used to overflow the float detector filter to inf, leaving the
  // reduction (and so the output) NaN until reset().
  const double sr = 48000.0;
  for (Mode m : {Mode::Speak, Mode::Sing, Mode::Puke}) {
    DSP::DistressorComp c;
    c.prepare(sr, 2);
    c.setMode(m);
    c.setAmount(1.0f);
    int bad = 0;
    for (int i = 0; i < 64; ++i) {
      float f[2] = {i % 2 ? 3e38f : -3e38f, i % 3 ? -3e38f : 3e38f};
      c.processFrame(f);
      if (!std::isfinite(f[0]) || !std::isfinite(f[1]) || !std::isfinite(c.lastGainReductionDb()))
        ++bad;
    }
    double peak = 0.0;
    for (int i = 0; i < (int)sr; ++i) {
      const float s = (float)(0.25 * std::sin(2.0 * kPi * 440.0 * i / sr));
      float f[2] = {s, s};
      c.processFrame(f);
      if (!std::isfinite(f[0]) || !std::isfinite(f[1]) || !std::isfinite(c.lastGainReductionDb()))
        ++bad;
      if (i > (int)(0.9 * sr)) peak = std::max(peak, (double)std::fabs(f[0]));
    }
    ASSERT_TRUE(bad == 0);
    ASSERT_TRUE(peak > 0.05 && peak < 2.0);  // recovered to normal compression of the sine
  }
}

// ---- Engine ----

Settings engineDefaults() {
  double v[kNumParams];
  loadDefaults(v);
  return mapParams(v);
}

struct Stereo { std::vector<float> l, r; };

// Runs the engine over the whole signal in blocks, calling setSettings() before every block.
Stereo runEngine(Engine& e, const Settings& s, const std::vector<float>& l,
                 const std::vector<float>& r, int block = 256) {
  Stereo out{std::vector<float>(l.size()), std::vector<float>(l.size())};
  for (size_t i = 0; i < l.size(); i += (size_t)block) {
    const int n = (int)std::min<size_t>((size_t)block, l.size() - i);
    e.setSettings(s);
    e.process(&l[i], &r[i], &out.l[i], &out.r[i], n);
  }
  return out;
}

Stereo runEngine(Engine& e, const Settings& s, const std::vector<float>& x, int block = 256) {
  return runEngine(e, s, x, x, block);
}

Stereo runFresh(double sr, const Settings& s, const std::vector<float>& x, int block = 256) {
  Engine e;
  e.prepare(sr);
  return runEngine(e, s, x, block);
}

// RMS in dB of x[a, b).
double rmsDb(const std::vector<float>& x, size_t a, size_t b) {
  b = std::min(b, x.size());
  double sum = 0.0;
  for (size_t i = a; i < b; ++i) sum += (double)x[i] * x[i];
  return 10.0 * std::log10(sum / (double)std::max<size_t>(1, b - a) + 1e-30);
}

// RMS (dB) of the output over the input-time window [t0, t1) s (shifted by the latency).
double outRmsDb(const std::vector<float>& y, double sr, int latency, double t0, double t1) {
  return rmsDb(y, (size_t)(t0 * sr) + (size_t)latency, (size_t)(t1 * sr) + (size_t)latency);
}

double maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
  double m = 0.0;
  for (size_t i = 0; i < std::min(a.size(), b.size()); ++i)
    m = std::max(m, (double)std::fabs(a[i] - b[i]));
  return m;
}

// Phrase (200 Hz saw, -18 dB RMS) 1 s, breath (high-passed noise) 0.3 s, phrase 1 s.
std::vector<float> phraseBreathPhrase(double sr, double breathDb = -35.0) {
  SignalBuilder s(sr);
  s.saw(200.0, -18.0, 1.0);
  s.hpNoise(breathDb, 0.3);
  s.saw(200.0, -18.0, 1.0);
  return s.x;
}

int expectedLatency(double sr) {
  return (int)std::lround(0.080 * sr) + DSP::nextPow2((int)std::ceil(0.020 * sr));
}

void test_engine_defaults_finite_and_mapped() {
  double v[kNumParams];
  loadDefaults(v);
  Settings s = mapParams(v);
  ASSERT_TRUE(s.mode == Mode::Speak);
  ASSERT_NEAR(s.inputDb, 0.0, 1e-6);
  ASSERT_NEAR(s.outputDb, 0.0, 1e-6);
  ASSERT_NEAR(s.riderTargetDb, -20.0, 1e-5);
  ASSERT_NEAR(s.riderRangeDb, 3.0, 1e-6);  // Speak 6 dB x 0.5
  ASSERT_NEAR(s.breathAmount, 0.5, 1e-6);
  ASSERT_NEAR(s.deEssAmount, 0.5, 1e-6);
  ASSERT_NEAR(s.compAmount, 0.5, 1e-6);
  ASSERT_NEAR(s.tone, 0.5, 1e-6);
  ASSERT_NEAR(s.breathSens, 0.5, 1e-6);
  ASSERT_NEAR(s.deEssFreqHz, deEssFreqHzFromNorm(kDefaultDeEssFreq), 1e-2);
  ASSERT_NEAR(s.compMix, 1.0, 1e-6);
  ASSERT_NEAR(s.eqAmount, 0.5, 1e-6);
  ASSERT_TRUE(s.compRatioChoice == 0 && s.compDistChoice == 0 && s.compHpfChoice == 1);
  ASSERT_TRUE(!s.breathListen && !s.deEssListen && !s.bypass);
  ASSERT_TRUE(s.riderOn && s.breathOn && s.eqOn && s.deEssOn && s.compOn && s.toneOn);

  // Out of range is clamped, non-finite falls back to the default.
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  v[kInput] = 7.0;
  v[kOutput] = -3.0;
  v[kMode] = nan;
  v[kRiderRange] = inf;
  v[kTone] = nan;
  v[kRiderTarget] = 0.25;
  v[kCompRatio] = 1.0;
  v[kCompDist] = 2.0 / 3.0;
  v[kCompHpf] = 0.0;
  v[kDeEssFreq] = 1.0;
  v[kBypass] = 1.0;
  v[kEqIn] = 0.0;
  v[kBreathListen] = 1.0;
  v[kDeEssListen] = 0.7;
  v[kCompMix] = -inf;
  s = mapParams(v);
  ASSERT_NEAR(s.inputDb, 12.0, 1e-6);
  ASSERT_NEAR(s.outputDb, -12.0, 1e-6);
  ASSERT_TRUE(s.mode == Mode::Speak);
  ASSERT_NEAR(s.riderRangeDb, 3.0, 1e-6);
  ASSERT_NEAR(s.tone, 0.5, 1e-6);
  ASSERT_NEAR(s.riderTargetDb, -25.0, 1e-5);
  ASSERT_NEAR(s.deEssFreqHz, 10000.0, 1e-2);
  ASSERT_NEAR(s.compMix, 1.0, 1e-6);
  ASSERT_TRUE(s.compRatioChoice == 7 && s.compDistChoice == 2 && s.compHpfChoice == 0);
  ASSERT_TRUE(s.bypass && !s.eqOn && s.riderOn && s.breathListen && s.deEssListen);

  // Rider range follows the mode preset.
  loadDefaults(v);
  v[kRiderRange] = 1.0;
  v[kMode] = 0.5;
  s = mapParams(v);
  ASSERT_TRUE(s.mode == Mode::Sing);
  ASSERT_NEAR(s.riderRangeDb, 4.0, 1e-6);
  v[kMode] = 1.0;
  v[kRiderRange] = 0.5;
  s = mapParams(v);
  ASSERT_TRUE(s.mode == Mode::Puke);
  ASSERT_NEAR(s.riderRangeDb, 1.5, 1e-6);

  // Every float field is finite for any input.
  int bad = 0;
  for (int k = 0; k < 64; ++k) {
    double w[kNumParams];
    for (int i = 0; i < (int)kNumParams; ++i)
      w[i] = (k + i) % 5 == 0 ? nan : ((k * 7 + i * 3) % 11) / 5.0 - 0.5;
    const Settings t = mapParams(w);
    for (float f : {t.inputDb, t.outputDb, t.riderTargetDb, t.riderRangeDb, t.breathAmount,
                    t.deEssAmount, t.compAmount, t.tone, t.breathSens, t.deEssFreqHz, t.compMix,
                    t.eqAmount})
      if (!std::isfinite(f)) ++bad;
  }
  ASSERT_TRUE(bad == 0);
}

void test_engine_silence_stays_silent() {
  const double sr = 48000.0;
  std::vector<float> zeros(512, 0.0f), l(512), r(512);
  for (Mode m : {Mode::Speak, Mode::Sing, Mode::Puke}) {
    Settings s = engineDefaults();
    s.mode = m;
    Engine e;
    e.prepare(sr);
    long nonZero = 0;
    for (int b = 0; b < (int)(10.0 * sr / 512); ++b) {
      e.setSettings(s);
      e.process(zeros.data(), zeros.data(), l.data(), r.data(), 512);
      for (int i = 0; i < 512; ++i)
        if (l[i] != 0.0f || r[i] != 0.0f) ++nonZero;
    }
    ASSERT_TRUE(nonZero == 0);

    // After a phrase, the decaying tails never leave denormals at the output.
    const auto x = phraseBreathPhrase(sr);
    runEngine(e, s, x);
    long denormals = 0;
    for (int b = 0; b < (int)(5.0 * sr / 512); ++b) {
      e.setSettings(s);
      e.process(zeros.data(), zeros.data(), l.data(), r.data(), 512);
      for (int i = 0; i < 512; ++i)
        if (std::fpclassify(l[i]) == FP_SUBNORMAL || std::fpclassify(r[i]) == FP_SUBNORMAL)
          ++denormals;
    }
    ASSERT_TRUE(denormals == 0);
  }
}

void test_engine_full_scale_finite() {
  const double sr = 48000.0;
  auto l = noiseVec((size_t)(2.0 * sr), 1.0 / std::sqrt(3.0), 0x1111u);  // uniform +-1
  auto r = noiseVec((size_t)(2.0 * sr), 1.0 / std::sqrt(3.0), 0x2222u);
  l[1000] = std::numeric_limits<float>::quiet_NaN();
  r[2000] = std::numeric_limits<float>::infinity();
  l[3000] = -std::numeric_limits<float>::infinity();
  for (Mode m : {Mode::Speak, Mode::Sing, Mode::Puke}) {
    Settings s = engineDefaults();
    s.mode = m;
    Engine e;
    e.prepare(sr);
    const Stereo y = runEngine(e, s, l, r);
    long bad = 0;
    double peak = 0.0;
    for (size_t i = 0; i < y.l.size(); ++i) {
      if (!std::isfinite(y.l[i]) || !std::isfinite(y.r[i]) || std::fabs(y.l[i]) > 4.0f ||
          std::fabs(y.r[i]) > 4.0f)
        ++bad;
      peak = std::max(peak, (double)std::fabs(y.l[i]));
    }
    ASSERT_TRUE(bad == 0);
    ASSERT_TRUE(peak > 0.1);  // the noise does come through
  }
}

void test_engine_zero_and_one_sample_blocks() {
  const double sr = 48000.0;
  auto x = phraseBreathPhrase(sr);
  x.resize(65 * 1500);
  const Settings s = engineDefaults();
  const Stereo ref = runFresh(sr, s, x, 65);

  Engine e;
  e.prepare(sr);
  Stereo y{std::vector<float>(x.size()), std::vector<float>(x.size())};
  const int pattern[3] = {0, 1, 64};
  size_t pos = 0;
  for (int k = 0; pos < x.size(); ++k) {
    const int n = (int)std::min<size_t>((size_t)pattern[k % 3], x.size() - pos);
    e.setSettings(s);
    e.process(&x[pos], &x[pos], &y.l[pos], &y.r[pos], n);
    pos += (size_t)n;
  }
  ASSERT_TRUE(maxAbsDiff(ref.l, y.l) <= 1e-6);
  ASSERT_TRUE(maxAbsDiff(ref.r, y.r) <= 1e-6);
  ASSERT_TRUE(rmsDb(ref.l, (size_t)e.latencySamples(), ref.l.size()) > -40.0);
}

void test_engine_mono_in_mono_out() {
  const double sr = 48000.0;
  const auto x = phraseBreathPhrase(sr);
  for (Mode m : {Mode::Speak, Mode::Sing, Mode::Puke}) {
    Settings s = engineDefaults();
    s.mode = m;
    const Stereo y = runFresh(sr, s, x);
    long mismatch = 0;
    for (size_t i = 0; i < x.size(); ++i)
      if (y.l[i] != y.r[i]) ++mismatch;
    ASSERT_TRUE(mismatch == 0);

    // Mono call (inR / outR null), processed in place: same as the left channel.
    Engine e;
    e.prepare(sr);
    std::vector<float> buf = x;
    for (size_t i = 0; i < buf.size(); i += 256) {
      const int n = (int)std::min<size_t>(256, buf.size() - i);
      e.setSettings(s);
      e.process(&buf[i], nullptr, &buf[i], nullptr, n);
    }
    ASSERT_TRUE(maxAbsDiff(buf, y.l) == 0.0);
    ASSERT_TRUE(rmsDb(buf, (size_t)e.latencySamples(), buf.size()) > -40.0);
  }
}

void test_engine_latency_reported_and_bypass_delay() {
  for (double sr : {44100.0, 48000.0, 96000.0}) {
    Engine e;
    e.prepare(sr);
    const int L = e.latencySamples();
    ASSERT_TRUE(L == expectedLatency(sr));
    ASSERT_TRUE(L == (sr == 44100.0 ? 4552 : (sr == 48000.0 ? 4864 : 9728)));
    auto l = noiseVec((size_t)(0.3 * sr) + (size_t)L, 0.2, 0x3333u);
    auto r = noiseVec(l.size(), 0.2, 0x4444u);
    for (Mode m : {Mode::Speak, Mode::Sing, Mode::Puke}) {
      Settings s = engineDefaults();
      s.mode = m;
      s.bypass = true;
      s.inputDb = 6.0f;   // bypass ignores the gains
      s.outputDb = -6.0f;
      e.reset();
      const Stereo y = runEngine(e, s, l, r, 300);
      ASSERT_TRUE(e.latencySamples() == L);  // the same in every mode
      long mismatch = 0;
      for (size_t i = 0; i < l.size(); ++i) {
        const float el = i >= (size_t)L ? l[i - (size_t)L] : 0.0f;
        const float er = i >= (size_t)L ? r[i - (size_t)L] : 0.0f;
        if (y.l[i] != el || y.r[i] != er) ++mismatch;
      }
      ASSERT_TRUE(mismatch == 0);
    }
  }
}

void test_engine_breath_reduced_end_to_end() {
  const double sr = 48000.0;
  const auto x = phraseBreathPhrase(sr);
  Settings s = engineDefaults();
  s.breathAmount = 0.0f;
  Engine e;
  e.prepare(sr);
  const int L = e.latencySamples();
  const Stereo y0 = runEngine(e, s, x);
  s.breathAmount = 1.0f;
  const Stereo y1 = runFresh(sr, s, x);
  // The whole breath, edges included (the look-ahead covers it from its first sample).
  const double cut = outRmsDb(y0.l, sr, L, 1.0, 1.3) - outRmsDb(y1.l, sr, L, 1.0, 1.3);
  ASSERT_TRUE(cut >= 6.0 && cut <= 13.0);
  // The phrases are untouched.
  ASSERT_NEAR(outRmsDb(y1.l, sr, L, 0.3, 0.95), outRmsDb(y0.l, sr, L, 0.3, 0.95), 0.5);
  ASSERT_NEAR(outRmsDb(y1.l, sr, L, 1.4, 2.2), outRmsDb(y0.l, sr, L, 1.4, 2.2), 0.5);
}

void test_engine_puke_scream_intact() {
  const double sr = 48000.0;
  SignalBuilder b(sr);
  b.wideNoise(-10.0, 3.0);
  Settings s = engineDefaults();
  s.mode = Mode::Puke;
  s.breathAmount = 0.0f;
  Engine e;
  e.prepare(sr);
  const int L = e.latencySamples();
  const Stereo y0 = runEngine(e, s, b.x);
  s.breathAmount = 1.0f;
  const Stereo y1 = runFresh(sr, s, b.x);
  const double d = outRmsDb(y0.l, sr, L, 1.0, 2.9) - outRmsDb(y1.l, sr, L, 1.0, 2.9);
  ASSERT_TRUE(d >= -1.0 && d <= 1.0);
  ASSERT_TRUE(outRmsDb(y1.l, sr, L, 1.0, 2.9) > -25.0);
}

void test_engine_puke_uses_level_detector() {
  // Scream / quiet breath / scream: only the level detector sees this breath (the spectral one
  // needs a voiced phrase first), so it is reduced in Puke and left alone in Speak.
  const double sr = 48000.0;
  SignalBuilder b(sr);
  b.wideNoise(-10.0, 1.0);
  b.wideNoise(-35.0, 0.4);
  b.wideNoise(-10.0, 1.0);
  double cut[2] = {0.0, 0.0};
  bool level[2] = {false, false};
  const Mode modes[2] = {Mode::Puke, Mode::Speak};
  for (int k = 0; k < 2; ++k) {
    Settings s = engineDefaults();
    s.mode = modes[k];
    s.breathAmount = 0.0f;
    Engine e;
    e.prepare(sr);
    const int L = e.latencySamples();
    const Stereo y0 = runEngine(e, s, b.x);
    level[k] = e.usingLevelDetector();
    s.breathAmount = 1.0f;
    const Stereo y1 = runFresh(sr, s, b.x);
    cut[k] = outRmsDb(y0.l, sr, L, 1.1, 1.35) - outRmsDb(y1.l, sr, L, 1.1, 1.35);
  }
  ASSERT_TRUE(cut[0] >= 6.0 && cut[0] <= 13.0);
  ASSERT_TRUE(cut[1] >= -1.0 && cut[1] <= 1.0);
  ASSERT_TRUE(level[0] && !level[1]);
}

// Breath edges in the delayed audio, both detectors: the end of the breath is reduced and the
// phrase after it is free again soon. Cut = output level at Amount 0 minus at Amount 1.
void test_engine_breath_edges_covered() {
  const double sr = 48000.0, b0 = 1.0, b1 = 1.25;
  int bad = 0;
  for (Mode m : {Mode::Speak, Mode::Puke}) {
    SignalBuilder b(sr);
    if (m == Mode::Puke) {
      b.wideNoise(-10.0, b0);
      b.wideNoise(-35.0, b1 - b0);
      b.wideNoise(-10.0, 1.0);
    } else {
      b.saw(200.0, -18.0, b0);
      b.hpNoise(-35.0, b1 - b0);
      b.saw(200.0, -18.0, 1.0);
    }
    Settings s = engineDefaults();
    s.mode = m;
    s.breathAmount = 0.0f;
    Engine e;
    e.prepare(sr);
    const int L = e.latencySamples();
    const Stereo y0 = runEngine(e, s, b.x);
    s.breathAmount = 1.0f;
    const Stereo y1 = runFresh(sr, s, b.x);
    auto cut = [&](double t0, double t1) {
      return outRmsDb(y0.l, sr, L, t0, t1) - outRmsDb(y1.l, sr, L, t0, t1);
    };
    // Every 5 ms of the last 20 ms of the breath is cut by at least 11 dB (full cut: 12 dB).
    double tail = 1e9;
    for (int k = 0; k < 4; ++k) tail = std::min(tail, cut(b1 - 0.02 + 0.005 * k, b1 - 0.015 + 0.005 * k));
    const double after = cut(b1 + 0.03, b1 + 0.13);
    if (!(tail >= 11.0 && tail <= 13.0) || !(after >= -0.5 && after < 1.0)) {
      std::fprintf(stderr, "  mode %d: tail %.2f dB, after %.2f dB\n", (int)m, tail, after);
      ++bad;
    }
  }
  ASSERT_TRUE(bad == 0);
}

// Puke, a 500 ms breath (longer than the level detector's 300 ms voice context): the end of the
// breath is still fully reduced. Fails if the detector releases the breath mid-way.
void test_engine_puke_long_breath_cut_to_end() {
  const double sr = 48000.0, b0 = 1.0, b1 = 1.5;
  SignalBuilder b(sr);
  b.wideNoise(-10.0, b0);
  b.wideNoise(-35.0, b1 - b0);
  b.wideNoise(-10.0, 1.0);
  Settings s = engineDefaults();
  s.mode = Mode::Puke;
  s.breathAmount = 0.0f;
  Engine e;
  e.prepare(sr);
  const int L = e.latencySamples();
  const Stereo y0 = runEngine(e, s, b.x);
  s.breathAmount = 1.0f;
  const Stereo y1 = runFresh(sr, s, b.x);
  const double cut = outRmsDb(y0.l, sr, L, b1 - 0.05, b1) - outRmsDb(y1.l, sr, L, b1 - 0.05, b1);
  ASSERT_TRUE(cut >= 11.0 && cut <= 13.0);
}

void test_engine_mode_switch_no_blowup() {
  const double sr = 48000.0;
  const int n = (int)(3.0 * sr), seg = (int)(0.1 * sr);
  std::vector<float> x((size_t)n), y((size_t)n);
  for (int i = 0; i < n; ++i) x[(size_t)i] = (float)(0.5 * std::sin(2.0 * kPi * 220.0 * i / sr));
  Engine e;
  e.prepare(sr);
  Settings s = engineDefaults();
  const Mode cycle[4] = {Mode::Speak, Mode::Puke, Mode::Sing, Mode::Puke};
  for (int b = 0; b * seg < n; ++b) {
    s.mode = cycle[b % 4];
    const int len = std::min(seg, n - b * seg);
    e.setSettings(s);
    e.process(&x[(size_t)(b * seg)], &x[(size_t)(b * seg)], &y[(size_t)(b * seg)], nullptr, len);
  }
  long bad = 0;
  for (float v : y)
    if (!std::isfinite(v) || std::fabs(v) > 4.0f) ++bad;
  ASSERT_TRUE(bad == 0);
  ASSERT_TRUE(rmsDb(y, (size_t)e.latencySamples(), y.size()) > -30.0);
}

void test_engine_sample_rates() {
  for (double sr : {44100.0, 48000.0, 96000.0, 192000.0}) {
    SignalBuilder b(sr);
    b.saw(200.0, -18.0, 0.5);
    b.hpNoise(-35.0, 0.2);
    b.saw(200.0, -18.0, 0.3);
    for (Mode m : {Mode::Speak, Mode::Puke}) {
      Settings s = engineDefaults();
      s.mode = m;
      Engine e;
      e.prepare(sr);
      ASSERT_TRUE(e.latencySamples() == expectedLatency(sr));
      const Stereo y = runEngine(e, s, b.x, 512);
      long bad = 0;
      for (size_t i = 0; i < y.l.size(); ++i)
        if (!std::isfinite(y.l[i]) || !std::isfinite(y.r[i]) || std::fabs(y.l[i]) > 4.0f) ++bad;
      ASSERT_TRUE(bad == 0);
      const double lvl = rmsDb(y.l, (size_t)e.latencySamples(), y.l.size());
      ASSERT_TRUE(lvl > -30.0 && lvl < -6.0);
    }
  }
}

void setAllIn(Settings& s, bool on) {
  s.riderOn = s.breathOn = s.eqOn = s.deEssOn = s.compOn = s.toneOn = on;
}

void test_engine_all_in_off_is_passthrough() {
  const double sr = 48000.0;
  auto x = phraseBreathPhrase(sr);
  const auto nz = noiseVec(x.size(), 0.05, 0x5555u);
  for (size_t i = 0; i < x.size(); ++i) x[i] += nz[i];
  for (Mode m : {Mode::Speak, Mode::Puke}) {
    Settings s = engineDefaults();
    s.mode = m;
    s.compAmount = 1.0f;
    s.tone = 1.0f;
    setAllIn(s, false);
    Engine e;
    e.prepare(sr);
    const size_t L = (size_t)e.latencySamples();
    const Stereo y = runEngine(e, s, x);
    long bad = 0;
    for (size_t i = 0; i < x.size(); ++i)
      if (std::fabs(y.l[i] - (i >= L ? x[i - L] : 0.0f)) > 1e-4f) ++bad;
    ASSERT_TRUE(bad == 0);
    // Sanity: with the modules in, the output is not the input.
    setAllIn(s, true);
    const Stereo yOn = runFresh(sr, s, x);
    ASSERT_TRUE(maxAbsDiff(yOn.l, y.l) > 1e-2);
  }
}

void test_engine_breath_in_off_disables_reduction() {
  const double sr = 48000.0;
  const auto x = phraseBreathPhrase(sr);
  Settings s = engineDefaults();
  s.breathAmount = 0.0f;
  Engine e;
  e.prepare(sr);
  const int L = e.latencySamples();
  const double ref = outRmsDb(runEngine(e, s, x).l, sr, L, 1.0, 1.3);
  s.breathAmount = 1.0f;
  s.breathOn = false;
  const double off = outRmsDb(runFresh(sr, s, x).l, sr, L, 1.0, 1.3);
  s.breathOn = true;
  const double on = outRmsDb(runFresh(sr, s, x).l, sr, L, 1.0, 1.3);
  ASSERT_NEAR(off, ref, 0.5);
  ASSERT_TRUE(ref - on >= 6.0);
}

// Smooth synthetic voice: 200 Hz with two harmonics (small natural sample-to-sample steps),
// a breath between two phrases and a sibilant burst.
std::vector<float> smoothVoice(double sr) {
  std::vector<float> x;
  auto phrase = [&](double sec) {
    const int n = (int)(sec * sr);
    for (int i = 0; i < n; ++i) {
      const double t = (double)i / sr;
      x.push_back((float)(0.25 * std::sin(2.0 * kPi * 200.0 * t) +
                          0.12 * std::sin(2.0 * kPi * 400.0 * t) +
                          0.06 * std::sin(2.0 * kPi * 600.0 * t)));
    }
  };
  SignalBuilder b(sr);
  b.hpNoise(-35.0, 0.3);
  SignalBuilder sib(sr);
  sib.hpNoise(-28.0, 0.15);
  phrase(0.6);
  x.insert(x.end(), b.x.begin(), b.x.end());
  phrase(0.4);
  x.insert(x.end(), sib.x.begin(), sib.x.end());
  phrase(0.4);
  return x;
}

void test_engine_in_toggle_no_click() {
  const double sr = 48000.0;
  const auto x = smoothVoice(sr);
  Settings base = engineDefaults();
  base.riderTargetDb = -10.0f;
  base.riderRangeDb = 6.0f;
  base.breathAmount = 1.0f;
  base.eqAmount = 1.0f;
  base.deEssAmount = 1.0f;
  base.compAmount = 1.0f;
  base.tone = 1.0f;
  const int seg = (int)(0.05 * sr);
  int bad = 0;
  for (int m = 0; m < 6; ++m) {
    auto setIn = [m](Settings& s, bool on) {
      bool* f[6] = {&s.riderOn, &s.breathOn, &s.eqOn, &s.deEssOn, &s.compOn, &s.toneOn};
      *f[m] = on;
    };
    Settings on = base, off = base;
    setIn(off, false);
    const Stereo yOn = runFresh(sr, on, x, seg);
    const Stereo yOff = runFresh(sr, off, x, seg);
    Engine e;
    e.prepare(sr);
    std::vector<float> y(x.size());
    for (size_t i = 0, k = 0; i < x.size(); i += (size_t)seg, ++k) {
      const int n = (int)std::min<size_t>((size_t)seg, x.size() - i);
      e.setSettings(k % 2 ? off : on);
      e.process(&x[i], &x[i], &y[i], nullptr, n);
    }
    const size_t L = (size_t)e.latencySamples();
    double maxStep = 0.0, maxExcess = 0.0;
    for (size_t i = 1; i < y.size(); ++i) {
      const double st = std::fabs(y[i] - y[i - 1]);
      maxStep = std::max(maxStep, st);
      // Inside the harmonic phrases (input time, 20 ms away from the noise segments) the steady
      // outputs have small, smooth steps, so a switch click stands out.
      const double t = ((double)i - (double)L) / sr;
      const bool phrase = (t > 0.02 && t < 0.58) || (t > 0.92 && t < 1.28) ||
                          (t > 1.47 && t < 1.83);
      if (!phrase) continue;
      const double ref = std::max(std::fabs(yOn.l[i] - yOn.l[i - 1]),
                                  std::fabs(yOff.l[i] - yOff.l[i - 1]));
      maxExcess = std::max(maxExcess, st - ref);
    }
    // Brief: no step above 0.5. Stricter: inside the phrases toggling adds no step beyond what
    // the steady on / off outputs have at the same sample (a hard switch adds |on - off|).
    if (!(maxStep < 0.5) || !(maxExcess < 0.01)) {
      std::fprintf(stderr, "  module %d: maxStep %.4f excess %.4f\n", m, maxStep, maxExcess);
      ++bad;
    }
  }
  ASSERT_TRUE(bad == 0);
}

void test_engine_breath_listen_outputs_removed() {
  const double sr = 48000.0;
  const auto x = phraseBreathPhrase(sr);
  Settings s = engineDefaults();
  s.breathAmount = 1.0f;
  s.breathListen = true;
  Engine e;
  e.prepare(sr);
  const int L = e.latencySamples();
  const Stereo y = runEngine(e, s, x);
  ASSERT_TRUE(outRmsDb(y.l, sr, L, 1.02, 1.28) > -50.0);
  ASSERT_TRUE(outRmsDb(y.l, sr, L, 1.02, 1.28) < -35.0);
  ASSERT_TRUE(outRmsDb(y.l, sr, L, 0.3, 0.95) < -40.0);
  ASSERT_TRUE(outRmsDb(y.l, sr, L, 1.45, 2.2) < -40.0);

  // The stages after the reducer are not heard; Breath Listen wins over De-ess Listen.
  Settings t = s;
  t.eqAmount = 1.0f;
  t.deEssAmount = 1.0f;
  t.compAmount = 1.0f;
  t.compMix = 0.3f;
  t.tone = 0.0f;
  t.deEssListen = true;
  ASSERT_TRUE(maxAbsDiff(runFresh(sr, t, x).l, y.l) <= 1e-6);
}

void test_engine_deess_listen_outputs_removed_band() {
  const double sr = 48000.0;
  SignalBuilder b(sr);
  b.saw(200.0, -18.0, 0.6);
  b.hpNoise(-12.0, 0.3);  // sibilant burst
  b.saw(200.0, -18.0, 0.6);
  Settings s = engineDefaults();
  s.deEssAmount = 1.0f;
  s.deEssListen = true;
  Engine e;
  e.prepare(sr);
  const int L = e.latencySamples();
  const Stereo y = runEngine(e, s, b.x);
  ASSERT_TRUE(outRmsDb(y.l, sr, L, 0.65, 0.85) > -40.0);
  ASSERT_TRUE(outRmsDb(y.l, sr, L, 0.65, 0.85) < -10.0);
  ASSERT_TRUE(outRmsDb(y.l, sr, L, 0.1, 0.55) < -50.0);

  // Comp and Tone (after the de-esser) are not heard.
  Settings t = s;
  t.compAmount = 1.0f;
  t.compMix = 0.4f;
  t.tone = 1.0f;
  ASSERT_TRUE(maxAbsDiff(runFresh(sr, t, b.x).l, y.l) <= 1e-6);
  // Nothing is removed at Amount 0.
  t.deEssAmount = 0.0f;
  ASSERT_TRUE(outRmsDb(runFresh(sr, t, b.x).l, sr, L, 0.65, 0.85) < -80.0);
}

void test_engine_comp_mix_zero_bypasses_comp() {
  const double sr = 48000.0;
  const auto x = phraseBreathPhrase(sr);
  Settings s = engineDefaults();
  s.compAmount = 1.0f;
  s.compMix = 0.0f;
  const Stereo mix0 = runFresh(sr, s, x);
  Settings off = s;
  off.compOn = false;
  off.compMix = 1.0f;
  const Stereo inOff = runFresh(sr, off, x);
  ASSERT_TRUE(maxAbsDiff(mix0.l, inOff.l) <= 1e-5);
  ASSERT_TRUE(maxAbsDiff(mix0.r, inOff.r) <= 1e-5);
  s.compMix = 1.0f;
  ASSERT_TRUE(maxAbsDiff(runFresh(sr, s, x).l, inOff.l) > 1e-2);
}

void test_engine_gains_are_smoothed() {
  const double sr = 48000.0;
  const int n = (int)(1.0 * sr);
  std::vector<float> x((size_t)n), y((size_t)n);
  for (int i = 0; i < n; ++i) x[(size_t)i] = (float)(0.25 * std::sin(2.0 * kPi * 1000.0 * i / sr));
  Settings s = engineDefaults();
  setAllIn(s, false);
  s.inputDb = -12.0f;
  s.outputDb = 0.0f;
  Engine e;
  e.prepare(sr);
  const int L = e.latencySamples();
  for (int i = 0; i < n; i += 256) {
    if (i >= L + 4800) s.inputDb = 12.0f;        // +24 dB jump of the input gain
    if (i >= L + 14400) s.outputDb = -12.0f;     // -12 dB jump of the output gain
    e.setSettings(s);
    e.process(&x[(size_t)i], nullptr, &y[(size_t)i], nullptr, std::min(256, n - i));
  }
  double maxStep = 0.0;
  for (size_t i = 1; i < y.size(); ++i) maxStep = std::max(maxStep, (double)std::fabs(y[i] - y[i - 1]));
  // Natural step of the loudest part: 2 pi 1000 / 48000 x 1.0 = 0.13; a jump would be ~0.9.
  ASSERT_TRUE(maxStep < 0.2);
  // Settled values: +12 dB then +12 - 12 = 0 dB on the 0.25 sine (-15.05 dBFS RMS).
  ASSERT_NEAR(rmsDb(y, (size_t)(L + 9600), (size_t)(L + 14400)), -15.05 + 12.0, 0.1);
  ASSERT_NEAR(rmsDb(y, (size_t)(L + 24000), y.size()), -15.05, 0.1);
}

// Reference chain built from the blocks in the specified order, with constant settings.
void test_engine_chain_order_matches_reference() {
  const double sr = 48000.0;
  SignalBuilder b(sr);
  b.saw(200.0, -18.0, 0.8);
  b.hpNoise(-35.0, 0.3);
  b.saw(200.0, -18.0, 0.5);
  b.hpNoise(-15.0, 0.15);
  b.saw(200.0, -18.0, 0.5);
  const std::vector<float>& xl = b.x;
  std::vector<float> xr(xl.size());
  for (size_t i = 0; i < xl.size(); ++i) xr[i] = 0.7f * xl[i];

  Settings s = engineDefaults();
  s.inputDb = 6.0f;
  s.outputDb = -3.0f;
  s.riderTargetDb = -14.0f;
  s.riderRangeDb = 6.0f;
  s.breathAmount = 1.0f;
  s.eqAmount = 0.8f;
  s.deEssAmount = 1.0f;
  s.compAmount = 1.0f;
  s.compMix = 0.8f;
  s.tone = 0.8f;
  Engine e;
  e.prepare(sr);
  const Stereo y = runEngine(e, s, xl, xr);

  const int D = e.latencySamples();
  const int W = D;  // sliding max over every probability since the delayed sample came in
  DSP::SpectralBreathDetector det;
  det.prepare(sr);
  det.setSensitivity(s.breathSens);
  DSP::LevelMeter meter;
  meter.prepare(sr, 200.0f);
  DSP::Rider rider;
  rider.prepare(sr);
  rider.setTargetDb(s.riderTargetDb);
  rider.setMaxRangeDb(s.riderRangeDb);
  rider.setSlewDbPerSec(presetFor(s.mode).riderSlewDbPerSec);
  DSP::BreathReducer red;
  red.prepare(sr);
  red.setAmount(s.breathAmount);
  DSP::CorrectiveEq eq;
  eq.prepare(sr, 2);
  eq.setMode(s.mode);
  eq.setAmount(s.eqAmount);
  eq.reset();
  DSP::DeEsser de;
  de.prepare(sr, 2);
  de.setMode(s.mode);
  de.setAmount(s.deEssAmount);
  de.setFreqHz(s.deEssFreqHz);
  de.reset();
  DSP::DistressorComp comp;
  comp.prepare(sr, 2);
  comp.setMode(s.mode);
  comp.setAmount(s.compAmount);
  comp.setMix(s.compMix);
  comp.setRatioChoice(s.compRatioChoice);
  comp.setDistChoice(s.compDistChoice);
  comp.setDetectorHpf(s.compHpfChoice);
  comp.reset();
  DSP::ToneEq tone;
  tone.prepare(sr, 2);
  tone.setMode(s.mode);
  tone.setTone(s.tone);
  tone.reset();
  const float gIn = (float)dbToGain(s.inputDb), gOut = (float)dbToGain(s.outputDb);

  std::vector<float> prob;
  prob.reserve(xl.size());
  std::vector<int> dq;  // indices into prob, decreasing values (sliding max)
  size_t dqHead = 0;
  double maxDiff = 0.0, energy = 0.0;
  for (size_t t = 0; t < xl.size(); ++t) {
    const float ll = xl[t] * gIn, lr = xr[t] * gIn;
    const float mono = 0.5f * (ll + lr);
    const float lvl = meter.process(mono);
    prob.push_back(det.process(mono));
    while (dq.size() > dqHead && prob[(size_t)dq.back()] <= prob.back()) dq.pop_back();
    dq.push_back((int)t);
    while ((long)dq[dqHead] < (long)t - W) ++dqHead;
    const float p = prob[(size_t)dq[dqHead]];
    const float al = (t >= (size_t)D ? xl[t - (size_t)D] : 0.0f) * gIn;
    const float ar = (t >= (size_t)D ? xr[t - (size_t)D] : 0.0f) * gIn;
    const float gR = rider.processFrame(lvl, p > 0.5f);
    const float gB = red.process(p);
    float f[2] = {al * gR * gB, ar * gR * gB};
    f[0] = eq.process(0, f[0]);
    f[1] = eq.process(1, f[1]);
    de.processFrame(f);
    comp.processFrame(f);
    f[0] = tone.process(0, f[0]) * gOut;
    f[1] = tone.process(1, f[1]) * gOut;
    maxDiff = std::max(maxDiff, (double)std::max(std::fabs(f[0] - y.l[t]), std::fabs(f[1] - y.r[t])));
    energy += (double)f[0] * f[0];
  }
  ASSERT_TRUE(energy > 1.0);
  ASSERT_TRUE(maxDiff <= 1e-5);
}

void test_engine_no_alloc_in_process() {
  const double sr = 48000.0;
  const auto x = smoothVoice(sr);
  std::vector<float> l(x.size()), r(x.size());
  Settings variants[24];
  int nv = 0;
  const Settings d = engineDefaults();
  variants[nv++] = d;
  for (int m = 0; m < 6; ++m) {
    Settings s = d;
    bool* f[6] = {&s.riderOn, &s.breathOn, &s.eqOn, &s.deEssOn, &s.compOn, &s.toneOn};
    *f[m] = false;
    variants[nv++] = s;
  }
  Settings s = d;
  s.breathListen = true;
  variants[nv++] = s;
  s = d;
  s.deEssListen = true;
  variants[nv++] = s;
  s = d;
  s.bypass = true;
  variants[nv++] = s;
  for (Mode m : {Mode::Sing, Mode::Puke, Mode::Speak}) {
    s = d;
    s.mode = m;
    s.compRatioChoice = (int)m + 2;
    s.compDistChoice = (int)m + 1;
    s.compHpfChoice = (int)m;
    s.inputDb = 6.0f;
    s.outputDb = -6.0f;
    s.breathAmount = 1.0f;
    s.deEssFreqHz = 9000.0f;
    variants[nv++] = s;
  }
  Engine e;
  e.prepare(sr);
  const size_t block = x.size() / (size_t)(2 * nv);
  g_allocs = 0;
  g_countAllocs = true;
  for (int rep = 0; rep < 2; ++rep) {
    for (int k = 0; k < nv; ++k) {
      const size_t at = (size_t)(rep * nv + k) * block;
      e.setSettings(variants[k]);
      e.process(&x[at], &x[at], &l[at], &r[at], (int)block);
    }
    e.reset();
  }
  g_countAllocs = false;
  ASSERT_TRUE(g_allocs == 0);
}

// ---- BreathReducer ----

void test_reducer_amount_zero_is_unity() {
  DSP::BreathReducer r;
  r.prepare(48000.0);
  r.setAmount(0.0f);
  for (int i = 0; i < 48000; ++i) ASSERT_TRUE(r.process(1.0f) == 1.0f);
  r.setAmount(std::numeric_limits<float>::quiet_NaN());
  for (int i = 0; i < 100; ++i) ASSERT_TRUE(r.process(1.0f) == 1.0f);
}

void test_reducer_full_amount_is_minus_12_db() {
  DSP::BreathReducer r;
  r.prepare(48000.0);
  r.setAmount(1.0f);
  const double target = std::pow(10.0, -12.0 / 20.0);
  float g = 1.0f;
  for (int i = 0; i < 48000; ++i) {
    g = r.process(1.0f);
    ASSERT_TRUE(g >= target * 0.999);  // never below -12 dB
  }
  ASSERT_NEAR(20.0 * std::log10(g), -12.0, 0.3);
}

void test_reducer_scales_with_prob() {
  DSP::BreathReducer r;
  r.prepare(48000.0);
  r.setAmount(1.0f);
  float g = 1.0f;
  for (int i = 0; i < 48000; ++i) g = r.process(0.5f);
  ASSERT_NEAR(20.0 * std::log10(g), -6.0, 1.0);
}

void test_reducer_smooth() {
  DSP::BreathReducer r;
  r.prepare(48000.0);
  r.setAmount(1.0f);
  float prev = r.process(0.0f);
  for (int i = 0; i < 2000; ++i) {
    const float g = r.process(1.0f);
    ASSERT_TRUE(std::fabs(g - prev) <= 0.2f);
    prev = g;
  }
  ASSERT_TRUE(prev < 0.3f);
  for (int i = 0; i < 12000; ++i) {
    const float g = r.process(0.0f);
    ASSERT_TRUE(std::fabs(g - prev) <= 0.2f);
    prev = g;
  }
  ASSERT_NEAR(prev, 1.0, 1e-3);
}

void test_reducer_nan_prob_safe() {
  DSP::BreathReducer r;
  r.prepare(44100.0);
  r.setAmount(1.0f);
  for (int i = 0; i < 1000; ++i) r.process(1.0f);
  const float nan = std::numeric_limits<float>::quiet_NaN();
  for (int i = 0; i < 100000; ++i) {
    const float g = r.process(i % 2 ? nan : 7.0f);
    ASSERT_TRUE(std::isfinite(g) && g > 0.0f && g <= 1.0f);
  }
  for (int i = 0; i < 100000; ++i) r.process(nan);
  ASSERT_NEAR(r.process(nan), 1.0, 1e-6);
}

} // namespace

int main() {
  RUN_TEST(test_param_enum_count);
  RUN_TEST(test_deess_freq_range);
  RUN_TEST(test_comp_choice_steps);
  RUN_TEST(test_mode_from_norm_steps);
  RUN_TEST(test_choice_mapping_matches_sdk_display);
  RUN_TEST(test_io_gain_ranges);
  RUN_TEST(test_defaults);
  RUN_TEST(test_state_roundtrip);
  RUN_TEST(test_state_rejects_bad_version_and_nan);
  RUN_TEST(test_rider_raises_quiet_to_target);
  RUN_TEST(test_rider_lowers_loud);
  RUN_TEST(test_rider_slew_limited);
  RUN_TEST(test_rider_freeze_holds_gain);
  RUN_TEST(test_rider_ignores_silence);
  RUN_TEST(test_level_meter_sine);
  RUN_TEST(test_fft_peak_at_bin);
  RUN_TEST(test_breath_detects_noise_between_phrases);
  RUN_TEST(test_breath_ignores_quiet_low_voice);
  RUN_TEST(test_breath_ignores_short_click);
  RUN_TEST(test_breath_silence_is_zero);
  RUN_TEST(test_breath_sensitivity);
  RUN_TEST(test_breath_reset_is_deterministic_and_safe);
  RUN_TEST(test_puke_breath_after_scream);
  RUN_TEST(test_puke_growl_not_breath);
  RUN_TEST(test_puke_loud_gasp_not_cut);
  RUN_TEST(test_puke_silence_is_zero);
  RUN_TEST(test_puke_sensitivity);
  RUN_TEST(test_puke_long_breath_held_to_end);
  RUN_TEST(test_puke_breath_duration_cap);
  RUN_TEST(test_reducer_amount_zero_is_unity);
  RUN_TEST(test_reducer_full_amount_is_minus_12_db);
  RUN_TEST(test_reducer_scales_with_prob);
  RUN_TEST(test_reducer_smooth);
  RUN_TEST(test_reducer_nan_prob_safe);
  RUN_TEST(test_eq_hpf_attenuates_rumble);
  RUN_TEST(test_eq_mud_cut_at_300);
  RUN_TEST(test_eq_passband_flat);
  RUN_TEST(test_eq_amount_scales_mud_and_hpf);
  RUN_TEST(test_eq_notch_cuts_resonance);
  RUN_TEST(test_eq_notch_idle_on_clean_signal);
  RUN_TEST(test_eq_notch_finds_prominent_peak_under_loud_band);
  RUN_TEST(test_eq_amount_zero_notch_off);
  RUN_TEST(test_eq_notch_ignores_vowel_harmonics);
  RUN_TEST(test_eq_ignores_out_of_range_channel);
  RUN_TEST(test_biquad_high_shelf_gain);
  RUN_TEST(test_eq_stereo_channels_identical);
  RUN_TEST(test_eq_amount_change_is_smooth);
  RUN_TEST(test_biquad_low_pass);
  RUN_TEST(test_deesser_freq_moves_detection);
  RUN_TEST(test_deesser_reduces_sibilance);
  RUN_TEST(test_deesser_leaves_low_band);
  RUN_TEST(test_deesser_amount_zero_is_transparent);
  RUN_TEST(test_deesser_puke_nearly_off);
  RUN_TEST(test_deesser_listen_outputs_removed_band);
  RUN_TEST(test_deesser_finite_at_extreme_rates);
  RUN_TEST(test_deesser_gain_is_stereo_linked);
  RUN_TEST(test_deesser_freq_change_is_smooth);
  RUN_TEST(test_comp_reduces_peaks);
  RUN_TEST(test_comp_ratio_per_mode);
  RUN_TEST(test_comp_ratio_choice_overrides_mode);
  RUN_TEST(test_comp_auto_equals_mode_ratio);
  RUN_TEST(test_comp_ratio_20);
  RUN_TEST(test_comp_times_scale_with_ratio);
  RUN_TEST(test_comp_detector_hpf);
  RUN_TEST(test_comp_detector_hpf_choices);
  RUN_TEST(test_comp_unity_below_threshold);
  RUN_TEST(test_comp_puke_no_reduction_is_flat);
  RUN_TEST(test_comp_level_compensation);
  RUN_TEST(test_comp_dist2_adds_even_harmonics);
  RUN_TEST(test_comp_dist3_adds_odd_harmonics);
  RUN_TEST(test_comp_dist_choice_overrides_mode);
  RUN_TEST(test_comp_dist2_level);
  RUN_TEST(test_comp_distortion_grows_with_gr);
  RUN_TEST(test_comp_mix_zero_is_dry);
  RUN_TEST(test_comp_extreme_input_finite);
  RUN_TEST(test_comp_stereo_linked);
  RUN_TEST(test_comp_soft_knee);
  RUN_TEST(test_comp_param_changes_are_smooth);
  RUN_TEST(test_tone_neutral_is_flat);
  RUN_TEST(test_tone_up_speak);
  RUN_TEST(test_tone_down_inverts);
  RUN_TEST(test_tone_air_shelf_gain);
  RUN_TEST(test_tone_mode_curves_differ);
  RUN_TEST(test_tone_low_rate_shelf_stays_below_nyquist);
  RUN_TEST(test_tone_nonfinite_input_and_tone_are_safe);
  RUN_TEST(test_tone_stereo_channels_identical);
  RUN_TEST(test_tone_changes_are_smooth);
  RUN_TEST(test_presets_values);
  RUN_TEST(test_presets_out_of_range_mode_is_clamped);
  RUN_TEST(test_comp_huge_finite_input_recovers);
  RUN_TEST(test_engine_defaults_finite_and_mapped);
  RUN_TEST(test_engine_silence_stays_silent);
  RUN_TEST(test_engine_full_scale_finite);
  RUN_TEST(test_engine_zero_and_one_sample_blocks);
  RUN_TEST(test_engine_mono_in_mono_out);
  RUN_TEST(test_engine_latency_reported_and_bypass_delay);
  RUN_TEST(test_engine_breath_reduced_end_to_end);
  RUN_TEST(test_engine_puke_scream_intact);
  RUN_TEST(test_engine_puke_uses_level_detector);
  RUN_TEST(test_engine_breath_edges_covered);
  RUN_TEST(test_engine_puke_long_breath_cut_to_end);
  RUN_TEST(test_engine_mode_switch_no_blowup);
  RUN_TEST(test_engine_sample_rates);
  RUN_TEST(test_engine_all_in_off_is_passthrough);
  RUN_TEST(test_engine_breath_in_off_disables_reduction);
  RUN_TEST(test_engine_in_toggle_no_click);
  RUN_TEST(test_engine_breath_listen_outputs_removed);
  RUN_TEST(test_engine_deess_listen_outputs_removed_band);
  RUN_TEST(test_engine_comp_mix_zero_bypasses_comp);
  RUN_TEST(test_engine_gains_are_smoothed);
  RUN_TEST(test_engine_chain_order_matches_reference);
  RUN_TEST(test_engine_no_alloc_in_process);

  std::fprintf(stdout, "\n%d passed, %d failed.\n", g_passed, g_failed);
  if (g_failed > 0) {
    std::fprintf(stderr, "%d test(s) FAILED.\n", g_failed);
    return EXIT_FAILURE;
  }
  std::fprintf(stdout, "All tests passed.\n");
  return EXIT_SUCCESS;
}
