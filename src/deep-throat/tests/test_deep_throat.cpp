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
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

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
    ASSERT_TRUE(deesserDropDb(sr, 8000.0, -10.0, 4000.0f, 1.0f, Mode::Speak) < 3.0);
  }
}

void test_deesser_reduces_sibilance() {
  for (double sr : {44100.0, 48000.0, 96000.0})
    ASSERT_TRUE(deesserDropDb(sr, 7000.0, -10.0, 6500.0f, 1.0f, Mode::Speak, 0.1) >= 6.0);
}

void test_deesser_leaves_low_band() {
  for (double sr : {44100.0, 48000.0, 96000.0})
    ASSERT_TRUE(deesserDropDb(sr, 500.0, -10.0, 6500.0f, 1.0f, Mode::Speak) < 0.5);
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
  ASSERT_TRUE(10.0 * std::log10(std::max(se, 1e-30) / si) < -60.0);
}

void test_deesser_puke_nearly_off() {
  ASSERT_TRUE(deesserDropDb(48000.0, 7000.0, -10.0, 6500.0f, 1.0f, Mode::Puke) < 3.0);
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

void test_deesser_192k_finite_and_stereo_linked() {
  DSP::DeEsser d;
  d.prepare(192000.0, 2);
  d.setFreqHz(10000.0f);
  d.setAmount(1.0f);
  bool ok = true;
  for (int i = 0; i < 19200; ++i) {
    float f[2] = {(float)(0.3 * std::sin(0.2 * i)), (float)(0.3 * std::sin(0.2 * i))};
    d.processFrame(f);
    ok = ok && std::isfinite(f[0]) && std::isfinite(f[1]) && f[0] == f[1];
  }
  ASSERT_TRUE(ok);
}

void test_deesser_freq_change_is_smooth() {
  DSP::DeEsser d;
  d.prepare(48000.0, 2);
  d.setAmount(1.0f);
  d.setFreqHz(4000.0f);
  double maxJump = 0.0, prev = 0.0;
  for (int i = 0; i < 48000; ++i) {
    if (i == 24000) d.setFreqHz(10000.0f);
    float f[2];
    f[0] = f[1] = (float)(0.3 * std::sin(2.0 * kDsPi * 300.0 * i / 48000.0));
    d.processFrame(f);
    if (i > 100) maxJump = std::max(maxJump, std::fabs((double)f[0] - prev));
    prev = f[0];
  }
  ASSERT_TRUE(maxJump < 0.3 * 2.0 * kDsPi * 300.0 / 48000.0 * 1.5);
}

} // namespace

// ---- BreathReducer ----

void test_reducer_amount_zero_is_unity() {
  FairyTapes::DeepThroat::DSP::BreathReducer r;
  r.prepare(48000.0);
  r.setAmount(0.0f);
  for (int i = 0; i < 48000; ++i) ASSERT_TRUE(r.process(1.0f) == 1.0f);
  r.setAmount(std::numeric_limits<float>::quiet_NaN());
  for (int i = 0; i < 100; ++i) ASSERT_TRUE(r.process(1.0f) == 1.0f);
}

void test_reducer_full_amount_is_minus_12_db() {
  FairyTapes::DeepThroat::DSP::BreathReducer r;
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
  FairyTapes::DeepThroat::DSP::BreathReducer r;
  r.prepare(48000.0);
  r.setAmount(1.0f);
  float g = 1.0f;
  for (int i = 0; i < 48000; ++i) g = r.process(0.5f);
  ASSERT_NEAR(20.0 * std::log10(g), -6.0, 1.0);
}

void test_reducer_smooth() {
  FairyTapes::DeepThroat::DSP::BreathReducer r;
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
  FairyTapes::DeepThroat::DSP::BreathReducer r;
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

int main() {
  RUN_TEST(test_param_enum_count);
  RUN_TEST(test_deess_freq_range);
  RUN_TEST(test_comp_choice_steps);
  RUN_TEST(test_mode_from_norm_steps);
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
  RUN_TEST(test_deesser_192k_finite_and_stereo_linked);
  RUN_TEST(test_deesser_freq_change_is_smooth);

  std::fprintf(stdout, "\n%d passed, %d failed.\n", g_passed, g_failed);
  if (g_failed > 0) {
    std::fprintf(stderr, "%d test(s) FAILED.\n", g_failed);
    return EXIT_FAILURE;
  }
  std::fprintf(stdout, "All tests passed.\n");
  return EXIT_SUCCESS;
}
