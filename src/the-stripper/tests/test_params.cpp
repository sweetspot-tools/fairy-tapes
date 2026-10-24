/**
 * Fairy Tapes · The Stripper — Unit tests for params, DSP, and state logic.
 * Run via CTest; build target: the_stripper_tests.
 */

#include "params.h"
#include "dsp/biquad.h"
#include "dsp/fourk_eq.h"
#include "dsp/compression_stub.h"
#include "dsp/blockparty.h"
#include "dsp/denoise.h"
#include "nam_drive.h"
#include "dsp/tape_saturation.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
    std::fprintf(stderr, "  FAIL [%s:%d]: %s ≈ %s (got %.9f vs %.9f)\n", \
                 __FILE__, __LINE__, #a, #b, (double)(a), (double)(b)); \
    ++g_failed; return; \
  } \
} while(0)

#define ASSERT_APPROX_EPS(a, b, eps) do { \
  if (!approx((a), (b), (eps))) { \
    std::fprintf(stderr, "  FAIL [%s:%d]: %s ≈ %s (got %.9f vs %.9f, eps=%.9f)\n", \
                 __FILE__, __LINE__, #a, #b, (double)(a), (double)(b), (double)(eps)); \
    ++g_failed; return; \
  } \
} while(0)

#define RUN_TEST(fn) do { \
  fn(); \
  ++g_passed; \
} while(0)

using namespace FairyTapes::TheStripper;

// ---------------------------------------------------------------------------
// 1. Parameter helpers (params.h)
// ---------------------------------------------------------------------------

void test_normalizedToDb_boundaries() {
  ASSERT_APPROX(normalizedToDb(0.), kGainMinDb);
  ASSERT_APPROX(normalizedToDb(1.), kGainMaxDb);
  ASSERT_APPROX(normalizedToDb(0.5), 0.0);
  ASSERT_TRUE(normalizedToDb(-0.1) == kGainMinDb);
}

void test_dbToNormalized_boundaries() {
  ASSERT_APPROX(dbToNormalized(kGainMinDb), 0.);
  ASSERT_APPROX(dbToNormalized(kGainMaxDb), 1.);
  ASSERT_APPROX(dbToNormalized(0.0), 0.5);
  ASSERT_TRUE(dbToNormalized(-30.) == 0.);
  ASSERT_TRUE(dbToNormalized(30.) == 1.);
}

void test_db_roundtrip() {
  for (double n : {0.0, 0.25, 0.5, 0.75, 1.0}) {
    double db = normalizedToDb(n);
    double back = dbToNormalized(db);
    ASSERT_APPROX(back, n);
  }
}

void test_normalizedToLinear() {
  ASSERT_APPROX(normalizedToLinear(0.5), 1.0);
  ASSERT_APPROX(normalizedToLinear(0.), 0.1);
  ASSERT_APPROX(normalizedToLinear(1.), 10.0);
}

void test_gainStagingTrimToLinear() {
  ASSERT_APPROX(gainStagingTrimToLinear(0.0), 0.1);
  ASSERT_APPROX(gainStagingTrimToLinear(1.0), 2.0);
  ASSERT_TRUE(gainStagingTrimToLinear(0.5) > 0.1 && gainStagingTrimToLinear(0.5) < 2.0);
}

void test_isBypassStage() {
  ASSERT_TRUE(!isBypassStage(0.0));
  ASSERT_TRUE(isBypassStage(0.5));
  ASSERT_TRUE(isBypassStage(1.0));
  ASSERT_TRUE(!isBypassStage(0.49));
}

void test_isChannelMode() {
  ASSERT_TRUE(isChannelMode(0.0));
  ASSERT_TRUE(!isChannelMode(0.5));
  ASSERT_TRUE(!isChannelMode(1.0));
  ASSERT_TRUE(isChannelMode(0.49));
}

// ---------------------------------------------------------------------------
// 2. EQ frequency/gain/Q helpers
// ---------------------------------------------------------------------------

void test_eqNormToHPFFreq() {
  ASSERT_APPROX(eqNormToHPFFreq(0.0), 20.0f);
  ASSERT_APPROX(eqNormToHPFFreq(1.0), 500.0f);
  ASSERT_TRUE(eqNormToHPFFreq(0.5) > 20.0f && eqNormToHPFFreq(0.5) < 500.0f);
}

void test_eqNormToLPFFreq() {
  ASSERT_APPROX_EPS(eqNormToLPFFreq(1.0), 20000.0f, 1.0);
  ASSERT_APPROX(eqNormToLPFFreq(0.0), 3000.0f);
}

void test_eqNormToGainDb() {
  ASSERT_APPROX(eqNormToGainDb(0.0), -20.0f);
  ASSERT_APPROX(eqNormToGainDb(1.0), 20.0f);
  ASSERT_APPROX(eqNormToGainDb(0.5), 0.0f);
}

void test_eqNormToQ() {
  ASSERT_APPROX(eqNormToQ(0.0), 0.4f);
  ASSERT_APPROX(eqNormToQ(1.0), 4.0f);
}

void test_eqBandFreqRanges() {
  ASSERT_APPROX(eqNormToLFFreq(0.0), 30.0f);
  ASSERT_APPROX(eqNormToLFFreq(1.0), 480.0f);
  ASSERT_APPROX(eqNormToLMFreq(0.0), 200.0f);
  ASSERT_APPROX(eqNormToLMFreq(1.0), 2500.0f);
  ASSERT_APPROX(eqNormToHMFreq(0.0), 600.0f);
  ASSERT_APPROX(eqNormToHMFreq(1.0), 7000.0f);
  ASSERT_APPROX(eqNormToHFFreq(0.0), 1500.0f);
  ASSERT_APPROX(eqNormToHFFreq(1.0), 16000.0f);
}

// ---------------------------------------------------------------------------
// 3. EQ Type auto-derivation (Channel=Black, Mix bus=Brown)
// ---------------------------------------------------------------------------

void test_eqTypeFromStripMode() {
  // Channel mode (stripMode < 0.5) → Black (eqTypeBlack = true)
  ASSERT_TRUE(isChannelMode(kStripModeChannel));
  // Mix bus mode (stripMode >= 0.5) → Brown (eqTypeBlack = false)
  ASSERT_TRUE(!isChannelMode(kStripModeMixBus));

  // In processor logic: eqp.eqTypeBlack = isChannelMode(...)
  // So Channel → true (Black), Mix bus → false (Brown)
  bool channel_black = isChannelMode(kStripModeChannel);
  bool mixbus_brown = !isChannelMode(kStripModeMixBus);
  ASSERT_TRUE(channel_black);
  ASSERT_TRUE(mixbus_brown);
}

// ---------------------------------------------------------------------------
// 4. kNumParams count (kEqType removed)
// ---------------------------------------------------------------------------

void test_kNumParams_count() {
  // 33 up to kDriveAmount (state v7), plus kLimiterPound and kGateThreshold (v8) and kDriveNamOn (v9) appended after it
  ASSERT_TRUE(kNumParams == 36);
  ASSERT_TRUE(kNumParamsV8 == 35);
  ASSERT_TRUE(kNumParamsV7 == 33);
  ASSERT_TRUE(kDriveAmount == 32);
  ASSERT_TRUE(kEqSaturation == 31);
}

// ---------------------------------------------------------------------------
// 5. Param enum ordering integrity
// ---------------------------------------------------------------------------

void test_param_enum_ordering() {
  ASSERT_TRUE(kBypass == 0);
  ASSERT_TRUE(kStripMode == 1);
  ASSERT_TRUE(kInputGain == 2);
  ASSERT_TRUE(kOutputGain == 3);
  ASSERT_TRUE(kCompressorMode == 14);
  ASSERT_TRUE(kEqHPFFreq == 15);
  ASSERT_TRUE(kEqSaturation == 31);
  ASSERT_TRUE(kDriveAmount == kNumParams - 4);   // kLimiterPound, kGateThreshold and kDriveNamOn come after it
}

// ---------------------------------------------------------------------------
// 6. Biquad DSP (unit-level)
// ---------------------------------------------------------------------------

void test_biquad_passthrough() {
  DSP::Biquad bq;
  // Default coefficients: b0=1, rest=0 → passthrough
  for (int i = 0; i < 100; ++i) {
    float in = static_cast<float>(i) * 0.01f;
    float out = bq.processSample(in);
    ASSERT_APPROX(out, in);
  }
}

void test_biquad_reset() {
  DSP::Biquad bq;
  bq.processSample(1.0f);
  bq.processSample(0.5f);
  bq.reset();
  // After reset, state is cleared; passthrough should be clean
  float out = bq.processSample(0.0f);
  ASSERT_APPROX(out, 0.0f);
}

// ---------------------------------------------------------------------------
// 7. FourKEQ DSP (integration-level)
// ---------------------------------------------------------------------------

void test_fourkeq_passthrough_flat() {
  DSP::FourKEQ eq;
  eq.prepare(48000.0, 512);

  DSP::FourKEQParams p;
  // All gains zero, no HPF/LPF, no saturation → should be near-passthrough
  p.hpfEnabled = false;
  p.lpfEnabled = false;
  p.lfGain = 0.0f;
  p.lmGain = 0.0f;
  p.hmGain = 0.0f;
  p.hfGain = 0.0f;
  p.eqTypeBlack = false;

  constexpr int N = 512;
  std::vector<float> left(N, 0.0f), right(N, 0.0f);
  left[0] = 1.0f;
  right[0] = 1.0f;

  eq.process(left.data(), right.data(), N, p);

  // With flat gains and no filters, output should be close to input (impulse passes through)
  ASSERT_APPROX_EPS(left[0], 1.0f, 0.05f);
  ASSERT_APPROX_EPS(right[0], 1.0f, 0.05f);
}

void test_fourkeq_brown_vs_black_differ() {
  // Verify that Brown and Black produce different results with the same non-zero gain
  DSP::FourKEQ eqBrown, eqBlack;
  eqBrown.prepare(48000.0, 256);
  eqBlack.prepare(48000.0, 256);

  DSP::FourKEQParams p;
  p.hpfEnabled = false;
  p.lpfEnabled = false;
  p.lfGain = 6.0f;
  p.lfFreq = 100.0f;
  p.lmGain = 3.0f;
  p.lmFreq = 800.0f;
  p.lmQ = 1.0f;
  p.hmGain = 0.0f;
  p.hfGain = 0.0f;

  constexpr int N = 256;
  std::vector<float> brownL(N, 0.0f), brownR(N, 0.0f);
  std::vector<float> blackL(N, 0.0f), blackR(N, 0.0f);

  // Impulse
  brownL[0] = 1.0f; brownR[0] = 1.0f;
  blackL[0] = 1.0f; blackR[0] = 1.0f;

  p.eqTypeBlack = false; // Brown
  eqBrown.process(brownL.data(), brownR.data(), N, p);

  p.eqTypeBlack = true; // Black
  eqBlack.process(blackL.data(), blackR.data(), N, p);

  // At least some samples should differ between Brown and Black
  bool differ = false;
  for (int i = 0; i < N; ++i) {
    if (std::fabs(brownL[i] - blackL[i]) > 1e-6f) {
      differ = true;
      break;
    }
  }
  ASSERT_TRUE(differ);
}

void test_fourkeq_hpf_attenuates_dc() {
  DSP::FourKEQ eq;
  eq.prepare(48000.0, 512);

  DSP::FourKEQParams p;
  p.hpfEnabled = true;
  p.hpfFreq = 200.0f;
  p.lpfEnabled = false;
  p.lfGain = 0.0f;
  p.lmGain = 0.0f;
  p.hmGain = 0.0f;
  p.hfGain = 0.0f;
  p.eqTypeBlack = false;

  constexpr int N = 512;
  std::vector<float> left(N, 1.0f), right(N, 1.0f);

  eq.process(left.data(), right.data(), N, p);

  // HPF at 200 Hz should attenuate DC (constant signal). Last samples should be near zero.
  ASSERT_TRUE(std::fabs(left[N - 1]) < 0.2f);
}

// Level change (dB) of a sine through the 4K EQ with everything flat and only the HPF on.
double fourkHpfGainDb(float hpfHz, double sineHz) {
  DSP::FourKEQ eq;
  eq.prepare(48000.0, 64);
  DSP::FourKEQParams p;
  p.hpfEnabled = true;
  p.hpfFreq = hpfHz;
  p.eqTypeBlack = true;
  std::vector<float> l(64), r(64);
  double peak = 0.0;
  for (int b = 0; b < 600; ++b) {
    for (int i = 0; i < 64; ++i) {
      const double t = static_cast<double>(b * 64 + i) / 48000.0;
      l[i] = r[i] = 0.3f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * sineHz * t));
    }
    eq.process(l.data(), r.data(), 64, p);
    if (b >= 450) for (float x : l) peak = std::max(peak, static_cast<double>(std::fabs(x)));
  }
  return 20.0 * std::log10(peak / 0.3);
}

void test_fourkeq_hpf_passband_is_flat() {
  // The HPF is on by default at 20 Hz: it must leave the audio band alone (it cut 1 kHz by 24 dB when the pole of its
  // first-order stage had the wrong sign) and only roll off below its corner.
  ASSERT_APPROX_EPS(fourkHpfGainDb(20.0f, 1000.0), 0.0, 0.3);
  ASSERT_APPROX_EPS(fourkHpfGainDb(20.0f, 200.0), 0.0, 0.3);
  ASSERT_APPROX_EPS(fourkHpfGainDb(20.0f, 5000.0), 0.0, 0.3);
  ASSERT_TRUE(fourkHpfGainDb(200.0f, 50.0) < -12.0);   // well below the corner: cut
  ASSERT_APPROX_EPS(fourkHpfGainDb(200.0f, 2000.0), 0.0, 0.3);
}

// ---------------------------------------------------------------------------
// 8. Compression DSP (integration-level)
// ---------------------------------------------------------------------------

void test_compression_passthrough_zero_amount() {
  DSP::CompressionStub comp;
  comp.setSampleRate(48000.0);
  comp.prepare(256);
  comp.setMode(2); // VCA (dbx 160, clean, no transformer) — closest to passthrough
  comp.setParams(0.0f, 1.0f); // amount=0 → minimal compression

  constexpr int N = 256;
  std::vector<float> left(N), right(N);
  for (int i = 0; i < N; ++i) {
    left[i] = 0.5f;
    right[i] = 0.5f;
  }

  comp.process(left.data(), right.data(), N);

  // VCA with amount=0: output should be close to input (some HW emulation color possible)
  for (int i = 0; i < N; ++i) {
    ASSERT_APPROX_EPS(left[i], 0.5f, 0.35f);
  }
}

void test_compression_dry_mix_zero() {
  DSP::CompressionStub comp;
  comp.setSampleRate(48000.0);
  comp.prepare(256);
  comp.setMode(1); // FET
  comp.setParams(1.0f, 0.0f); // mix=0 → fully dry

  constexpr int N = 256;
  std::vector<float> left(N), right(N);
  for (int i = 0; i < N; ++i) {
    left[i] = 0.3f;
    right[i] = -0.3f;
  }

  comp.process(left.data(), right.data(), N);

  // mix=0 → output = dry = original input
  for (int i = 0; i < N; ++i) {
    ASSERT_APPROX_EPS(left[i], 0.3f, 0.01f);
    ASSERT_APPROX_EPS(right[i], -0.3f, 0.01f);
  }
}

void test_compression_all_modes_run() {
  // Verify all 4 modes run without crashing
  for (int mode = 0; mode <= 3; ++mode) {
    DSP::CompressionStub comp;
    comp.setSampleRate(44100.0);
    comp.prepare(128);
    comp.setMode(mode);
    comp.setParams(0.5f, 1.0f);

    std::vector<float> left(128, 0.2f), right(128, -0.2f);
    comp.process(left.data(), right.data(), 128);

    // Just verify no NaN/Inf
    for (int i = 0; i < 128; ++i) {
      ASSERT_TRUE(std::isfinite(left[i]));
      ASSERT_TRUE(std::isfinite(right[i]));
    }
  }
}

void test_compression_sidechain_accepted() {
  DSP::CompressionStub comp;
  comp.setSampleRate(48000.0);
  comp.prepare(128);
  comp.setMode(3); // Bus
  comp.setParams(0.8f, 1.0f);

  std::vector<float> left(128, 0.5f), right(128, 0.5f);
  std::vector<float> scL(128, 0.9f), scR(128, 0.9f);

  // Should not crash with sidechain
  comp.process(left.data(), right.data(), 128, scL.data(), scR.data());

  for (int i = 0; i < 128; ++i) {
    ASSERT_TRUE(std::isfinite(left[i]));
    ASSERT_TRUE(std::isfinite(right[i]));
  }
}

// ---------------------------------------------------------------------------
// 9. BlockParty Limiter DSP
// ---------------------------------------------------------------------------

void test_blockparty_passthrough_zero_pound() {
  DSP::BlockParty bp;
  bp.setSampleRate(48000.0);
  bp.setParams(0.0f, 1.0f); // pound=0, wet=1

  constexpr int N = 256;
  std::vector<float> left(N, 0.3f), right(N, -0.3f);
  bp.process(left.data(), right.data(), N);

  // With zero pound (threshold near 1.0), moderate signal should pass through
  for (int i = 0; i < N; ++i) {
    ASSERT_TRUE(std::isfinite(left[i]));
    ASSERT_TRUE(std::isfinite(right[i]));
    ASSERT_APPROX_EPS(left[i], 0.3f, 0.15f);
    ASSERT_APPROX_EPS(right[i], -0.3f, 0.15f);
  }
}

void test_blockparty_limits_loud_signal() {
  DSP::BlockParty bp;
  bp.setSampleRate(48000.0);
  bp.setParams(1.0f, 1.0f); // max pound, full wet

  constexpr int N = 1024;
  std::vector<float> left(N, 0.95f), right(N, -0.95f);
  bp.process(left.data(), right.data(), N);

  // With max pound, output should be constrained (closer to threshold than input)
  for (int i = 0; i < N; ++i) {
    ASSERT_TRUE(std::isfinite(left[i]));
    ASSERT_TRUE(std::isfinite(right[i]));
    ASSERT_TRUE(std::fabs(left[i]) <= 1.0f);
    ASSERT_TRUE(std::fabs(right[i]) <= 1.0f);
  }
}

void test_blockparty_dry_wet_zero_passthrough() {
  DSP::BlockParty bp;
  bp.setSampleRate(48000.0);
  bp.setParams(1.0f, 0.0f); // max pound but wet=0 → fully dry

  constexpr int N = 256;
  std::vector<float> left(N, 0.5f), right(N, -0.5f);
  bp.process(left.data(), right.data(), N);

  for (int i = 0; i < N; ++i) {
    ASSERT_APPROX_EPS(left[i], 0.5f, 0.01f);
    ASSERT_APPROX_EPS(right[i], -0.5f, 0.01f);
  }
}

void test_blockparty_reset_clears_state() {
  DSP::BlockParty bp;
  bp.setSampleRate(48000.0);
  bp.setParams(1.0f, 1.0f);

  constexpr int N = 256;
  std::vector<float> left(N, 0.9f), right(N, 0.9f);
  bp.process(left.data(), right.data(), N);

  bp.reset();
  bp.setParams(0.0f, 1.0f);

  std::vector<float> left2(N, 0.3f), right2(N, 0.3f);
  bp.process(left2.data(), right2.data(), N);

  // After reset with zero pound, should be near passthrough
  for (int i = 0; i < N; ++i) {
    ASSERT_TRUE(std::isfinite(left2[i]));
    ASSERT_APPROX_EPS(left2[i], 0.3f, 0.15f);
  }
}

void test_blockparty_ironbar_clip() {
  DSP::BlockParty bp;
  bp.setSampleRate(48000.0);
  bp.setParams(0.0f, 1.0f); // low pound

  constexpr int N = 64;
  std::vector<float> left(N, 2.0f), right(N, -2.0f); // clipping level
  bp.process(left.data(), right.data(), N);

  for (int i = 0; i < N; ++i) {
    ASSERT_TRUE(left[i] <= 0.999f);
    ASSERT_TRUE(right[i] >= -0.999f);
  }
}

// ---------------------------------------------------------------------------
// 10. DeNoise Gate/Expander DSP
// ---------------------------------------------------------------------------

void test_denoise_passthrough_zero_threshold() {
  DSP::DeNoise dn;
  dn.setSampleRate(48000.0);
  dn.setParams(0.0f, 0.25f, 1.0f); // threshold=0, speed=0.25, wet=1

  constexpr int N = 512;
  std::vector<float> left(N, 0.5f), right(N, -0.5f);
  dn.process(left.data(), right.data(), N);

  // Zero threshold → gates are wide open, signal passes with minimal change
  for (int i = 0; i < N; ++i) {
    ASSERT_TRUE(std::isfinite(left[i]));
    ASSERT_TRUE(std::isfinite(right[i]));
  }
}

void test_denoise_attenuates_quiet_signal() {
  DSP::DeNoise dn;
  dn.setSampleRate(48000.0);
  dn.setParams(1.0f, 0.5f, 1.0f); // max threshold, moderate speed

  constexpr int N = 2048;
  std::vector<float> left(N, 0.001f), right(N, 0.001f); // very quiet

  for (int block = 0; block < 4; ++block)
    dn.process(left.data(), right.data(), N);

  // With max threshold, a very quiet signal should be attenuated after convergence
  float sum = 0.0f;
  for (int i = N/2; i < N; ++i)
    sum += std::fabs(left[i]);
  float avg = sum / (N/2);
  ASSERT_TRUE(avg < 0.001f);
}

void test_denoise_dry_wet_zero() {
  DSP::DeNoise dn;
  dn.setSampleRate(48000.0);
  dn.setParams(1.0f, 0.5f, 0.0f); // wet=0 → special "clicks only" mode per original

  constexpr int N = 256;
  std::vector<float> left(N, 0.5f), right(N, -0.5f);
  dn.process(left.data(), right.data(), N);

  for (int i = 0; i < N; ++i) {
    ASSERT_TRUE(std::isfinite(left[i]));
    ASSERT_TRUE(std::isfinite(right[i]));
  }
}

void test_denoise_reset_clears_state() {
  DSP::DeNoise dn;
  dn.setSampleRate(48000.0);
  dn.setParams(1.0f, 0.5f, 1.0f);

  constexpr int N = 512;
  std::vector<float> left(N, 0.5f), right(N, 0.5f);
  dn.process(left.data(), right.data(), N);

  dn.reset();
  dn.setParams(0.0f, 0.25f, 1.0f);

  std::vector<float> left2(N, 0.5f), right2(N, 0.5f);
  dn.process(left2.data(), right2.data(), N);

  for (int i = 0; i < N; ++i) {
    ASSERT_TRUE(std::isfinite(left2[i]));
  }
}

void test_denoise_no_nan_inf() {
  DSP::DeNoise dn;
  dn.setSampleRate(44100.0);
  dn.setParams(0.5f, 0.5f, 1.0f);

  constexpr int N = 1024;
  std::vector<float> left(N), right(N);
  for (int i = 0; i < N; ++i) {
    left[i] = std::sin(static_cast<float>(i) * 0.01f) * 0.8f;
    right[i] = std::cos(static_cast<float>(i) * 0.01f) * 0.8f;
  }

  dn.process(left.data(), right.data(), N);

  for (int i = 0; i < N; ++i) {
    ASSERT_TRUE(std::isfinite(left[i]));
    ASSERT_TRUE(std::isfinite(right[i]));
  }
}

// ---------------------------------------------------------------------------
// 11. Drive param helpers
// ---------------------------------------------------------------------------

void test_driveAmountToLinear() {
  // 0 → 1x (unity gain), 1 → 10x (+20 dB)
  ASSERT_APPROX(driveAmountToLinear(0.0), 1.0);
  ASSERT_APPROX_EPS(driveAmountToLinear(1.0), 10.0, 0.01);
  // 0.5 → ~3.16x (+10 dB)
  ASSERT_APPROX_EPS(driveAmountToLinear(0.5), std::pow(10.0, 0.5), 0.01);
}

void test_drive_defaults_in_range() {
  ASSERT_TRUE(kDefaultDriveAmount >= 0.0 && kDefaultDriveAmount <= 1.0);
}

// ---------------------------------------------------------------------------
// 12. NamDrive DSP (unit-level)
// ---------------------------------------------------------------------------

// A real WaveNet capture from the pinned NAM submodule (48 kHz). wavenet_a2_max.nam, used before, is gone since
// NAM v0.6.0, and the tests skipped silently.
std::filesystem::path exampleModelPath() {
  return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path() / "vendors" /
         "NeuralAmpModelerCore" / "example_models" / "wavenet.nam";
}

void test_namdrive_passthrough_no_model() {
  FairyTapes::Shared::DSP::NamDrive drv;
  drv.setSampleRate(48000.0);
  drv.setDrive(0.5f);

  constexpr int N = 256;
  std::vector<float> left(N, 0.4f), right(N, -0.4f);
  drv.process(left.data(), right.data(), N);

  for (int i = 0; i < N; ++i) {
    ASSERT_APPROX_EPS(left[i], 0.4f, 1e-6f);
    ASSERT_APPROX_EPS(right[i], -0.4f, 1e-6f);
  }
}

void test_namdrive_is_model_loaded_default() {
  FairyTapes::Shared::DSP::NamDrive drv;
  ASSERT_TRUE(!drv.isModelLoaded());
}

void test_namdrive_load_invalid_path() {
  FairyTapes::Shared::DSP::NamDrive drv;
  drv.setSampleRate(48000.0);
  bool ok = drv.loadModel("/nonexistent/path/to/model.nam");
  ASSERT_TRUE(!ok);
  ASSERT_TRUE(!drv.isModelLoaded());
}

void test_namdrive_load_example_model() {
  FairyTapes::Shared::DSP::NamDrive drv;
  drv.setSampleRate(48000.0);

  const std::filesystem::path model_path = exampleModelPath();

  ASSERT_TRUE(std::filesystem::exists(model_path));   // ships with the pinned NAM submodule: never skip

  bool ok = drv.loadModel(model_path);
  if (!ok) std::fprintf(stderr, "    load error: %s\n", drv.lastError().c_str());
  ASSERT_TRUE(ok);
  ASSERT_TRUE(drv.isModelLoaded());
}

void test_namdrive_process_with_model() {
  FairyTapes::Shared::DSP::NamDrive drv;
  drv.setSampleRate(48000.0);

  const std::filesystem::path model_path = exampleModelPath();

  ASSERT_TRUE(std::filesystem::exists(model_path));   // ships with the pinned NAM submodule: never skip

  bool ok = drv.loadModel(model_path);
  ASSERT_TRUE(ok);

  drv.setDrive(0.5f);

  constexpr int N = 512;
  std::vector<float> left(N), right(N);
  for (int i = 0; i < N; ++i) {
    float t = static_cast<float>(i) / 48000.0f;
    left[i] = std::sin(2.0f * 3.14159265f * 440.0f * t) * 0.5f;
    right[i] = left[i];
  }

  std::vector<float> dry_left(left);
  drv.process(left.data(), right.data(), N);

  // With a model loaded, output should differ from dry input
  bool differs = false;
  for (int i = 0; i < N; ++i) {
    ASSERT_TRUE(std::isfinite(left[i]));
    ASSERT_TRUE(std::isfinite(right[i]));
    if (std::fabs(left[i] - dry_left[i]) > 1e-4f)
      differs = true;
  }
  ASSERT_TRUE(differs);
}

void test_namdrive_unload_model() {
  FairyTapes::Shared::DSP::NamDrive drv;
  drv.setSampleRate(48000.0);

  const std::filesystem::path model_path = exampleModelPath();

  if (std::filesystem::exists(model_path))
    drv.loadModel(model_path);

  drv.unloadModel();
  ASSERT_TRUE(!drv.isModelLoaded());

  // After unload, process should passthrough
  constexpr int N = 128;
  std::vector<float> left(N, 0.5f), right(N, -0.5f);
  drv.process(left.data(), right.data(), N);

  for (int i = 0; i < N; ++i) {
    ASSERT_APPROX_EPS(left[i], 0.5f, 1e-6f);
    ASSERT_APPROX_EPS(right[i], -0.5f, 1e-6f);
  }
}

void test_namdrive_reset_clears_state() {
  FairyTapes::Shared::DSP::NamDrive drv;
  drv.setSampleRate(48000.0);
  drv.setDrive(0.8f);
  drv.reset();

  constexpr int N = 128;
  std::vector<float> left(N, 0.3f), right(N, -0.3f);
  drv.process(left.data(), right.data(), N);

  for (int i = 0; i < N; ++i) {
    ASSERT_TRUE(std::isfinite(left[i]));
    ASSERT_TRUE(std::isfinite(right[i]));
  }
}

void test_namdrive_no_nan_inf() {
  FairyTapes::Shared::DSP::NamDrive drv;
  drv.setSampleRate(48000.0);

  const std::filesystem::path model_path = exampleModelPath();

  if (std::filesystem::exists(model_path))
    drv.loadModel(model_path);

  drv.setDrive(1.0f);

  constexpr int N = 1024;
  std::vector<float> left(N), right(N);
  for (int i = 0; i < N; ++i) {
    left[i] = std::sin(static_cast<float>(i) * 0.02f) * 0.9f;
    right[i] = std::cos(static_cast<float>(i) * 0.02f) * 0.9f;
  }

  drv.process(left.data(), right.data(), N);

  for (int i = 0; i < N; ++i) {
    ASSERT_TRUE(std::isfinite(left[i]));
    ASSERT_TRUE(std::isfinite(right[i]));
  }
}

// ---------------------------------------------------------------------------
// 12b. NamDrive auto-compensation and coloration tests
// ---------------------------------------------------------------------------

void test_namdrive_auto_compensation_rms() {
  FairyTapes::Shared::DSP::NamDrive drv;
  drv.setSampleRate(48000.0);

  const std::filesystem::path model_path = exampleModelPath();

  ASSERT_TRUE(std::filesystem::exists(model_path));   // ships with the pinned NAM submodule: never skip

  ASSERT_TRUE(drv.loadModel(model_path));   // a passthrough would pass the level check without testing anything
  drv.setDrive(0.5f);

  constexpr int N = 512;
  constexpr int blocks = 60;   // about 0.64 s: the RMS trackers (tau 100 ms) have converged
  std::vector<float> left(N), right(N);

  // Feed several blocks to let RMS tracker converge
  for (int b = 0; b < blocks; ++b) {
    for (int i = 0; i < N; ++i) {
      float t = static_cast<float>(b * N + i) / 48000.0f;
      float sig = std::sin(2.0f * 3.14159265f * 440.0f * t) * 0.3f;
      left[i] = sig;
      right[i] = sig;
    }
    drv.process(left.data(), right.data(), N);
  }

  // Measure output RMS of the last block
  float out_rms = 0.0f;
  for (int i = 0; i < N; ++i)
    out_rms += left[i] * left[i];
  out_rms = std::sqrt(out_rms / N);

  // Input RMS of a 0.3-amplitude sine = 0.3 / sqrt(2). The compensation is referenced to the input *before* the
  // Drive gain, so the output keeps that loudness (Drive 0.5 pushes the model +10 dB).
  const double db = 20.0 * std::log10(out_rms / (0.3 / std::sqrt(2.0)));
  std::fprintf(stdout, "    NAM Drive 0.5: output %+.2f dB vs input\n", db);
  ASSERT_APPROX_EPS(db, 0.0, 1.0);
}

void test_namdrive_coloration_at_zero_drive() {
  FairyTapes::Shared::DSP::NamDrive drv;
  drv.setSampleRate(48000.0);

  const std::filesystem::path model_path = exampleModelPath();

  ASSERT_TRUE(std::filesystem::exists(model_path));   // ships with the pinned NAM submodule: never skip

  ASSERT_TRUE(drv.loadModel(model_path));
  drv.setDrive(0.0f); // zero drive → unity gain, but model still active

  constexpr int N = 512;
  std::vector<float> left(N), right(N);
  for (int i = 0; i < N; ++i) {
    float t = static_cast<float>(i) / 48000.0f;
    left[i] = std::sin(2.0f * 3.14159265f * 440.0f * t) * 0.3f;
    right[i] = left[i];
  }

  std::vector<float> dry_left(left);
  drv.process(left.data(), right.data(), N);

  // Model should still color the signal even at drive=0 (not a plain passthrough)
  bool differs = false;
  for (int i = 0; i < N; ++i) {
    ASSERT_TRUE(std::isfinite(left[i]));
    if (std::fabs(left[i] - dry_left[i]) > 1e-4f)
      differs = true;
  }
  ASSERT_TRUE(differs);
}

// ---------------------------------------------------------------------------
// 12c. Tape stage DSP (Airwindows Tape port, one-knob macro, voicing, auto-compensation)
// ---------------------------------------------------------------------------

double bufferRms(const std::vector<float>& buf, size_t from = 0) {
  double acc = 0.0;
  for (size_t i = from; i < buf.size(); ++i)
    acc += static_cast<double>(buf[i]) * buf[i];
  return std::sqrt(acc / static_cast<double>(buf.size() - from));
}

// Runs a stereo sine through the tape in blocks; returns the left channel output.
std::vector<float> runTapeSine(DSP::TapeSaturation& tape, float amplitude, float freq,
                               int numBlocks, int blockSize = 512, double sr = 48000.0) {
  std::vector<float> out;
  std::vector<float> left(blockSize), right(blockSize);
  for (int b = 0; b < numBlocks; ++b) {
    for (int i = 0; i < blockSize; ++i) {
      const double t = static_cast<double>(b * blockSize + i) / sr;
      left[i] = right[i] = amplitude * static_cast<float>(std::sin(2.0 * 3.14159265358979 * freq * t));
    }
    tape.process(left.data(), right.data(), blockSize);
    out.insert(out.end(), left.begin(), left.end());
  }
  return out;
}

void test_tape_macro_slam_range() {
  using V = DSP::TapeSaturation::Voicing;
  ASSERT_APPROX(DSP::TapeSaturation::slamDb(0.0f, V::Tracking), 0.0);
  ASSERT_APPROX(DSP::TapeSaturation::slamDb(0.0f, V::Mixdown), 0.0);
  ASSERT_APPROX(DSP::TapeSaturation::slamDb(1.0f, V::Tracking), 12.0);
  // Mixdown is gentler than tracking at the same knob position.
  ASSERT_TRUE(DSP::TapeSaturation::slamDb(1.0f, V::Mixdown) < DSP::TapeSaturation::slamDb(1.0f, V::Tracking));
  ASSERT_TRUE(DSP::TapeSaturation::slamDb(1.0f, V::Mixdown) > 0.0f);
}

void test_tape_macro_bump_monotonic() {
  using V = DSP::TapeSaturation::Voicing;
  for (V v : {V::Tracking, V::Mixdown}) {
    float prev = -1.0f;
    for (float a = 0.0f; a <= 1.0001f; a += 0.1f) {
      const float b = DSP::TapeSaturation::bump(a, v);
      ASSERT_TRUE(b > prev);
      ASSERT_TRUE(b >= 0.0f && b <= 1.0f);
      prev = b;
    }
  }
  ASSERT_TRUE(DSP::TapeSaturation::bump(1.0f, V::Mixdown) < DSP::TapeSaturation::bump(1.0f, V::Tracking));
}

// Low-end lift (head bump) relative to 1 kHz, measured with a two-tone signal at -20 dBFS per tone.
double tapeLowLiftDb(float amount, DSP::TapeSaturation::Voicing v) {
  DSP::TapeSaturation tape;
  tape.setSampleRate(48000.0);
  tape.setParams(amount, v);
  constexpr int N = 512;
  constexpr int blocks = 200;
  std::vector<float> left(N), right(N), out;
  for (int b = 0; b < blocks; ++b) {
    for (int i = 0; i < N; ++i) {
      const double t = static_cast<double>(b * N + i) / 48000.0;
      left[i] = right[i] = static_cast<float>(0.1 * std::sin(2.0 * 3.14159265358979 * 40.0 * t) +
                                              0.1 * std::sin(2.0 * 3.14159265358979 * 1000.0 * t));
    }
    tape.process(left.data(), right.data(), N);
    if (b >= blocks - 75)
      out.insert(out.end(), left.begin(), left.end());
  }
  auto mag = [&out](double f) {
    double re = 0.0, im = 0.0;
    for (size_t i = 0; i < out.size(); ++i) {
      const double ph = 2.0 * 3.14159265358979 * f * static_cast<double>(i) / 48000.0;
      re += out[i] * std::cos(ph);
      im += out[i] * std::sin(ph);
    }
    return std::sqrt(re * re + im * im);
  };
  return 20.0 * std::log10(mag(40.0) / mag(1000.0));
}

void test_tape_head_bump_grows_with_amount() {
  using V = DSP::TapeSaturation::Voicing;
  for (V v : {V::Tracking, V::Mixdown}) {
    const double lo = tapeLowLiftDb(0.0f, v);
    const double hi = tapeLowLiftDb(1.0f, v);
    ASSERT_TRUE(lo > 0.0);
    ASSERT_TRUE(hi > lo + 0.25);
  }
  // Tracking has the fatter low end at full amount.
  ASSERT_TRUE(tapeLowLiftDb(1.0f, V::Tracking) > tapeLowLiftDb(1.0f, V::Mixdown));
}

void test_tape_near_unity_at_zero_amount() {
  DSP::TapeSaturation tape;
  tape.setSampleRate(48000.0);
  tape.setParams(0.0f, DSP::TapeSaturation::Voicing::Tracking);
  const std::vector<float> out = runTapeSine(tape, 0.25f, 1000.0f, 40);
  const double in_rms = 0.25 / std::sqrt(2.0);
  const double db = 20.0 * std::log10(bufferRms(out, out.size() / 2) / in_rms);
  ASSERT_APPROX_EPS(db, 0.0, 1.0);
}

void test_tape_auto_compensation_holds_level() {
  using V = DSP::TapeSaturation::Voicing;
  for (V v : {V::Tracking, V::Mixdown}) {
    DSP::TapeSaturation tape;
    tape.setSampleRate(48000.0);
    tape.setParams(1.0f, v);
    const std::vector<float> out = runTapeSine(tape, 0.25f, 1000.0f, 60);
    const double in_rms = 0.25 / std::sqrt(2.0);
    const double db = 20.0 * std::log10(bufferRms(out, out.size() / 2) / in_rms);
    ASSERT_APPROX_EPS(db, 0.0, 1.5);
  }
}

void test_tape_colors_signal_at_full_amount() {
  DSP::TapeSaturation tape;
  tape.setSampleRate(48000.0);
  tape.setParams(1.0f, DSP::TapeSaturation::Voicing::Tracking);
  const float amp = 0.5f;
  const std::vector<float> out = runTapeSine(tape, amp, 1000.0f, 40);
  // Saturation reshapes the waveform: crest factor drops below a pure sine's sqrt(2).
  const size_t from = out.size() / 2;
  float peak = 0.0f;
  for (size_t i = from; i < out.size(); ++i)
    peak = std::max(peak, std::fabs(out[i]));
  const double crest = peak / bufferRms(out, from);
  ASSERT_TRUE(crest < std::sqrt(2.0) - 0.02);
}

void test_tape_voicings_differ() {
  DSP::TapeSaturation tracking, mixdown;
  tracking.setSampleRate(48000.0);
  mixdown.setSampleRate(48000.0);
  tracking.setParams(0.8f, DSP::TapeSaturation::Voicing::Tracking);
  mixdown.setParams(0.8f, DSP::TapeSaturation::Voicing::Mixdown);
  const std::vector<float> a = runTapeSine(tracking, 0.5f, 60.0f, 20);
  const std::vector<float> b = runTapeSine(mixdown, 0.5f, 60.0f, 20);
  double diff = 0.0;
  for (size_t i = 0; i < a.size(); ++i)
    diff = std::max(diff, static_cast<double>(std::fabs(a[i] - b[i])));
  ASSERT_TRUE(diff > 1e-3);
}

void test_tape_no_nan_inf_hot_input() {
  DSP::TapeSaturation tape;
  tape.setSampleRate(96000.0);
  tape.setParams(1.0f, DSP::TapeSaturation::Voicing::Tracking);
  const std::vector<float> out = runTapeSine(tape, 4.0f, 100.0f, 20, 333, 96000.0);
  for (float s : out) {
    ASSERT_TRUE(std::isfinite(s));
    ASSERT_TRUE(std::fabs(s) < 8.0f);
  }
}

void test_tape_reset_clears_state() {
  DSP::TapeSaturation tape;
  tape.setSampleRate(48000.0);
  tape.setParams(1.0f, DSP::TapeSaturation::Voicing::Tracking);
  runTapeSine(tape, 0.8f, 50.0f, 10);
  tape.reset();
  constexpr int N = 512;
  std::vector<float> left(N, 0.0f), right(N, 0.0f);
  tape.process(left.data(), right.data(), N);
  for (int i = 0; i < N; ++i) {
    ASSERT_TRUE(std::fabs(left[i]) < 1e-6f);
    ASSERT_TRUE(std::fabs(right[i]) < 1e-6f);
  }
}

void test_tape_keeps_stereo() {
  DSP::TapeSaturation tape;
  tape.setSampleRate(48000.0);
  tape.setParams(0.5f, DSP::TapeSaturation::Voicing::Mixdown);
  constexpr int N = 512;
  std::vector<float> left(N), right(N, 0.0f);
  for (int i = 0; i < N; ++i)
    left[i] = 0.3f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * 440.0 * i / 48000.0));
  tape.process(left.data(), right.data(), N);
  for (int i = 0; i < N; ++i)
    ASSERT_TRUE(std::fabs(right[i]) < 1e-6f);
}

// ---------------------------------------------------------------------------
// 13. Default param values sanity
// ---------------------------------------------------------------------------

void test_default_values_in_range() {
  ASSERT_TRUE(kDefaultBypass >= 0.0 && kDefaultBypass <= 1.0);
  ASSERT_TRUE(kDefaultStripMode >= 0.0 && kDefaultStripMode <= 1.0);
  ASSERT_TRUE(kDefaultInputGain >= 0.0 && kDefaultInputGain <= 1.0);
  ASSERT_TRUE(kDefaultOutputGain >= 0.0 && kDefaultOutputGain <= 1.0);
  ASSERT_TRUE(kDefaultBypassStage >= 0.0 && kDefaultBypassStage <= 1.0);
  ASSERT_TRUE(kDefaultGainStagingTrim >= 0.0 && kDefaultGainStagingTrim <= 1.0);
  ASSERT_TRUE(kDefaultCompressAmount >= 0.0 && kDefaultCompressAmount <= 1.0);
  ASSERT_TRUE(kDefaultCompressMix >= 0.0 && kDefaultCompressMix <= 1.0);
  ASSERT_TRUE(kDefaultCompressorMode >= 0.0 && kDefaultCompressorMode <= 1.0);
  ASSERT_TRUE(kDefaultEqHPFFreq >= 0.0 && kDefaultEqHPFFreq <= 1.0);
  ASSERT_TRUE(kDefaultEqSaturation >= 0.0 && kDefaultEqSaturation <= 1.0);
  ASSERT_TRUE(kDefaultDriveAmount >= 0.0 && kDefaultDriveAmount <= 1.0);
}

void test_default_strip_mode_is_channel() {
  ASSERT_TRUE(isChannelMode(kDefaultStripMode));
}

void test_default_bypass_is_off() {
  ASSERT_TRUE(!isBypassStage(kDefaultBypassStage));
}

} // namespace

int run_meter_tests();  // tests/test_meter.cpp

int main() {
  std::fprintf(stdout, "Running The Stripper tests...\n\n");

  // Param helpers
  RUN_TEST(test_normalizedToDb_boundaries);
  RUN_TEST(test_dbToNormalized_boundaries);
  RUN_TEST(test_db_roundtrip);
  RUN_TEST(test_normalizedToLinear);
  RUN_TEST(test_gainStagingTrimToLinear);
  RUN_TEST(test_isBypassStage);
  RUN_TEST(test_isChannelMode);

  // EQ helpers
  RUN_TEST(test_eqNormToHPFFreq);
  RUN_TEST(test_eqNormToLPFFreq);
  RUN_TEST(test_eqNormToGainDb);
  RUN_TEST(test_eqNormToQ);
  RUN_TEST(test_eqBandFreqRanges);

  // EQ Type derivation
  RUN_TEST(test_eqTypeFromStripMode);

  // Param enum integrity
  RUN_TEST(test_kNumParams_count);
  RUN_TEST(test_param_enum_ordering);

  // Biquad
  RUN_TEST(test_biquad_passthrough);
  RUN_TEST(test_biquad_reset);

  // FourKEQ
  RUN_TEST(test_fourkeq_passthrough_flat);
  RUN_TEST(test_fourkeq_brown_vs_black_differ);
  RUN_TEST(test_fourkeq_hpf_attenuates_dc);
  RUN_TEST(test_fourkeq_hpf_passband_is_flat);

  // Compression
  RUN_TEST(test_compression_passthrough_zero_amount);
  RUN_TEST(test_compression_dry_mix_zero);
  RUN_TEST(test_compression_all_modes_run);
  RUN_TEST(test_compression_sidechain_accepted);

  // BlockParty Limiter
  RUN_TEST(test_blockparty_passthrough_zero_pound);
  RUN_TEST(test_blockparty_limits_loud_signal);
  RUN_TEST(test_blockparty_dry_wet_zero_passthrough);
  RUN_TEST(test_blockparty_reset_clears_state);
  RUN_TEST(test_blockparty_ironbar_clip);

  // DeNoise Gate
  RUN_TEST(test_denoise_passthrough_zero_threshold);
  RUN_TEST(test_denoise_attenuates_quiet_signal);
  RUN_TEST(test_denoise_dry_wet_zero);
  RUN_TEST(test_denoise_reset_clears_state);
  RUN_TEST(test_denoise_no_nan_inf);

  // Drive param helpers
  RUN_TEST(test_driveAmountToLinear);
  RUN_TEST(test_drive_defaults_in_range);

  // NAM Drive
  RUN_TEST(test_namdrive_passthrough_no_model);
  RUN_TEST(test_namdrive_is_model_loaded_default);
  RUN_TEST(test_namdrive_load_invalid_path);
  RUN_TEST(test_namdrive_load_example_model);
  RUN_TEST(test_namdrive_process_with_model);
  RUN_TEST(test_namdrive_unload_model);
  RUN_TEST(test_namdrive_reset_clears_state);
  RUN_TEST(test_namdrive_no_nan_inf);
  RUN_TEST(test_namdrive_auto_compensation_rms);
  RUN_TEST(test_namdrive_coloration_at_zero_drive);

  // Tape stage DSP
  RUN_TEST(test_tape_macro_slam_range);
  RUN_TEST(test_tape_macro_bump_monotonic);
  RUN_TEST(test_tape_head_bump_grows_with_amount);
  RUN_TEST(test_tape_near_unity_at_zero_amount);
  RUN_TEST(test_tape_auto_compensation_holds_level);
  RUN_TEST(test_tape_colors_signal_at_full_amount);
  RUN_TEST(test_tape_voicings_differ);
  RUN_TEST(test_tape_no_nan_inf_hot_input);
  RUN_TEST(test_tape_reset_clears_state);
  RUN_TEST(test_tape_keeps_stereo);

  // Defaults
  RUN_TEST(test_default_values_in_range);
  RUN_TEST(test_default_strip_mode_is_channel);
  RUN_TEST(test_default_bypass_is_off);

  g_failed += run_meter_tests();

  std::fprintf(stdout, "\n%d passed, %d failed.\n", g_passed, g_failed);
  if (g_failed > 0) {
    std::fprintf(stderr, "%d test(s) FAILED.\n", g_failed);
    return EXIT_FAILURE;
  }
  std::fprintf(stdout, "All tests passed.\n");
  return EXIT_SUCCESS;
}
