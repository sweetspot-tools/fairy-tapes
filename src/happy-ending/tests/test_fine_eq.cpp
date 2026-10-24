/**
 * Fairy Tapes · Happy Ending — Unit tests for the matched biquads and the FINE EQ bands.
 * Run via CTest; build target: happy_ending_fine_eq_tests.
 */

#include "dsp/fine_eq.h"
#include "dsp/ms.h"
#include "params.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <random>
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

using namespace FairyTapes::HappyEnding;
using FairyTapes::HappyEnding::DSP::BandSettings;
using FairyTapes::HappyEnding::DSP::FineEq;

namespace {

constexpr double kPi = 3.14159265358979323846;
int g_failed = 0;
int g_passed = 0;

#define ASSERT_TRUE(expr) do { \
  if (!(expr)) { \
    std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); \
    ++g_failed; return; \
  } \
} while(0)

#define ASSERT_NEAR_EPS(a, b, eps) do { \
  if (!(std::fabs((double)(a) - (double)(b)) <= (eps))) { \
    std::fprintf(stderr, "  FAIL [%s:%d]: %s ~= %s (got %.6f vs %.6f, eps %g)\n", \
                 __FILE__, __LINE__, #a, #b, (double)(a), (double)(b), (double)(eps)); \
    ++g_failed; return; \
  } \
} while(0)

#define RUN_TEST(fn) do { \
  const int before = g_failed; \
  fn(); \
  if (g_failed == before) { ++g_passed; std::printf("PASS %s\n", #fn); } \
  else std::printf("FAIL %s\n", #fn); \
} while(0)

// Band order of the interface: 0 HI PASS, 1 LOW SHELF, 2 LO MID, 3 MID, 4 HI MID, 5 HI SHELF.
enum Band { kBandHp = 0, kBandLs, kBandLm, kBandMid, kBandHm, kBandHs };

// Position of a value in the table of a parameter (the tests never hardcode indices).
int indexOfValue(ParamID id, double value) {
  const ValueTable t = valueTable(id);
  for (std::size_t i = 0; i < t.size; ++i)
    if (std::fabs(t.data[i] - value) < 1e-9) return static_cast<int>(i);
  return -1;
}

BandSettings settings(ParamID freqId, double hz, ParamID gainId, double gainDb, int qIndex = 1) {
  BandSettings s;
  s.on = true;
  s.ms = 0;
  s.freqIndex = indexOfValue(freqId, hz);
  s.gainIndex = indexOfValue(gainId, gainDb);
  s.qIndex = qIndex;
  return s;
}

std::vector<float> sine(double hz, double sr, int n, double amp) {
  std::vector<float> x(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) x[static_cast<std::size_t>(i)] = static_cast<float>(amp * std::sin(2.0 * kPi * hz * i / sr));
  return x;
}

double rmsOf(const std::vector<float>& x, int from) {
  double acc = 0.0;
  for (std::size_t i = static_cast<std::size_t>(from); i < x.size(); ++i) acc += double(x[i]) * x[i];
  return std::sqrt(acc / static_cast<double>(x.size() - static_cast<std::size_t>(from)));
}

// Gain in dB of a steady sine through the whole block path (warm-up discarded).
double measuredGainDb(FineEq& eq, double hz, double sr) {
  const int n = static_cast<int>(sr);  // one second: far past the 64-sample coefficient ramp
  std::vector<float> in = sine(hz, sr, n, 0.1);
  std::vector<float> l = in, r = in;
  eq.process(l.data(), r.data(), n);
  const int skip = n / 2;
  return 20.0 * std::log10(rmsOf(l, skip) / rmsOf(in, skip));
}

// Analog peaking prototype H(s) = (s^2 + s*A/Q*w0 + w0^2) / (s^2 + s/(A*Q)*w0 + w0^2), in dB.
double analogBellDb(double f0, double gainDb, double q, double hz) {
  const double a = std::pow(10.0, gainDb / 40.0);
  const double x = hz / f0;  // s = j * w0 * x
  const double numRe = 1.0 - x * x, numIm = x * a / q;
  const double denRe = 1.0 - x * x, denIm = x / (a * q);
  return 10.0 * std::log10((numRe * numRe + numIm * numIm) / (denRe * denRe + denIm * denIm));
}

// Width (Hz) of a bell where the curve stays above half of its peak gain in dB.
double halfPeakWidthHz(const FineEq& eq, double centre, double peakDb) {
  const double target = peakDb / 2.0;
  double lo = centre, hi = centre;
  for (double f = centre; f > 20.0; f /= 1.0005)
    if (eq.magnitudeDb(0, f) < target) { lo = f; break; }
  for (double f = centre; f < 20000.0; f *= 1.0005)
    if (eq.magnitudeDb(0, f) < target) { hi = f; break; }
  return hi - lo;
}

void testBellCentreGain() {
  const double rates[] = {44100.0, 48000.0, 96000.0};
  for (double sr : rates) {
    FineEq eq;
    eq.prepare(sr, 512);
    eq.setStageOn(true);
    eq.setBand(kBandMid, settings(kMidFreq, 1000.0, kMidGain, 4.0, /*Q M*/ 1));
    ASSERT_NEAR_EPS(eq.magnitudeDb(0, 1000.0), 4.0, 0.1);
    ASSERT_NEAR_EPS(measuredGainDb(eq, 1000.0, sr), 4.0, 0.1);
  }
}

void testNoCrampingNearNyquist() {
  const double sr = 44100.0;
  FineEq eq;
  eq.prepare(sr, 512);
  eq.setStageOn(true);
  eq.setBand(kBandHm, settings(kHmFreq, 12500.0, kHmGain, 4.0, /*Q M*/ 1));
  const double qEff = DSP::effectiveQ(Tables::kQValues[1], 4.0);
  for (double f : {10000.0, 14000.0})
    ASSERT_NEAR_EPS(eq.magnitudeDb(0, f), analogBellDb(12500.0, 4.0, qEff, f), 0.5);
}

// HI MID 12.5 kHz, Q Wide, at 44.1 kHz: the matched bell (boost and reciprocal cut) follows the
// analog prototype all the way down the skirt; the bell does not widen into the mids.
void testHiMidBellFollowsAnalogAt44k() {
  const double sr = 44100.0;
  for (double gain : {4.0, -4.0}) {
    FineEq eq;
    eq.prepare(sr, 512);
    eq.setStageOn(true);
    eq.setBand(kBandHm, settings(kHmFreq, 12500.0, kHmGain, gain, /*Q W*/ 0));
    const double qEff = DSP::effectiveQ(Tables::kQValues[0], gain);
    for (double f : {100.0, 1000.0, 5000.0, 10000.0, 12500.0, 15000.0, 20000.0})
      ASSERT_NEAR_EPS(eq.magnitudeDb(0, f), analogBellDb(12500.0, gain, qEff, f), 0.3);
  }
}

// A cut is the exact reciprocal of the boost of the same size, at every frequency and rate.
void testBellCutIsReciprocalOfBoost() {
  const double rates[] = {44100.0, 48000.0, 96000.0, 192000.0};
  for (double sr : rates)
    for (double fc : {100.0, 1000.0, 5000.0, 12500.0})
      for (double q : {0.5, 2.0, 5.0})
        for (double g : {0.5, 4.0}) {
          DSP::BiquadCoeffs up, down;
          DSP::matched::computePeakingMatched(up, fc, sr, g, q);
          DSP::matched::computePeakingMatched(down, fc, sr, -g, q);
          for (double f : {50.0, 500.0, 3000.0, 9000.0, 17000.0})
            ASSERT_NEAR_EPS(up.magnitudeDb(f, sr) + down.magnitudeDb(f, sr), 0.0, 1e-6);
        }
}

void testQCoupling() {
  // Effective Q = Q * (1 + |gain dB| / 8), the same for boost and cut.
  ASSERT_NEAR_EPS(DSP::effectiveQ(1.0, 0.0), 1.0, 1e-12);
  ASSERT_NEAR_EPS(DSP::effectiveQ(1.0, 4.0), 1.5, 1e-12);
  ASSERT_NEAR_EPS(DSP::effectiveQ(2.0, -4.0), 3.0, 1e-12);
  ASSERT_NEAR_EPS(DSP::effectiveQ(0.5, 1.0), 0.5625, 1e-12);

  FineEq big, small;
  big.prepare(48000.0, 512);
  small.prepare(48000.0, 512);
  big.setStageOn(true);
  small.setStageOn(true);
  big.setBand(kBandMid, settings(kMidFreq, 1000.0, kMidGain, 4.0));
  small.setBand(kBandMid, settings(kMidFreq, 1000.0, kMidGain, 1.0));
  const double wBig = halfPeakWidthHz(big, 1000.0, 4.0);
  const double wSmall = halfPeakWidthHz(small, 1000.0, 1.0);
  ASSERT_TRUE(wBig > 0.0 && wSmall > 0.0);
  ASSERT_TRUE(wBig < wSmall);
}

void testHighPassOffIsIdentity() {
  const double sr = 48000.0;
  FineEq eq;
  eq.prepare(sr, 512);
  eq.setStageOn(true);
  BandSettings off;
  off.freqIndex = 0;  // 0 = Off on the HI PASS scale
  eq.setBand(kBandHp, off);
  for (double f : {10.0, 100.0, 1000.0, 15000.0}) ASSERT_NEAR_EPS(eq.magnitudeDb(0, f), 0.0, 1e-9);

  std::mt19937 rng(1);
  std::uniform_real_distribution<float> d(-1.0f, 1.0f);
  std::vector<float> l(4096), r(4096);
  for (auto& v : l) v = d(rng);
  for (auto& v : r) v = d(rng);
  const std::vector<float> l0 = l, r0 = r;
  eq.process(l.data(), r.data(), 4096);
  for (std::size_t i = 0; i < l.size(); ++i) {
    ASSERT_TRUE(l[i] == l0[i]);
    ASSERT_TRUE(r[i] == r0[i]);
  }
}

void testHighPassSlope12dB() {
  FineEq eq;
  eq.prepare(48000.0, 512);
  eq.setStageOn(true);
  BandSettings hp;
  hp.freqIndex = indexOfValue(kHpFreq, 40.0);
  eq.setBand(kBandHp, hp);
  const double slope = eq.magnitudeDb(0, 20.0) - eq.magnitudeDb(0, 10.0);  // per octave
  ASSERT_NEAR_EPS(slope, 12.0, 1.0);
  // the corner is the -3 dB point of a Q 0.707 section
  ASSERT_NEAR_EPS(eq.magnitudeDb(0, 40.0), -3.0, 0.3);
}

void testStableAt192kAnd44k() {
  for (double sr : {44100.0, 192000.0}) {
    for (double gain : {4.0, -4.0}) {
      FineEq eq;
      eq.prepare(sr, 512);
      eq.setStageOn(true);
      eq.setBand(kBandHs, settings(kHsFreq, 20000.0, kHsGain, gain));
      std::mt19937 rng(7);
      std::uniform_real_distribution<float> d(-1.0f, 1.0f);
      std::vector<float> l(100000), r(100000);
      for (auto& v : l) v = d(rng);
      for (auto& v : r) v = d(rng);
      eq.process(l.data(), r.data(), 100000);
      for (std::size_t i = 0; i < l.size(); ++i) {
        ASSERT_TRUE(std::isfinite(l[i]) && std::isfinite(r[i]));
        ASSERT_TRUE(std::fabs(l[i]) < 10.0f && std::fabs(r[i]) < 10.0f);
      }
    }
  }
}

void testGainStepNoZipper() {
  // A high shelf moves b0 by about +4 dB the instant its gain steps: without the glide that is a
  // click of several tenths on a 400 Hz sine, which itself never moves more than 0.05 per sample.
  const double sr = 48000.0;
  const int n = 4096;
  FineEq eq;
  eq.prepare(sr, n);
  eq.setStageOn(true);
  eq.setBand(kBandHs, settings(kHsFreq, 3000.0, kHsGain, 0.0));
  std::vector<float> l = sine(400.0, sr, n, 0.5), r = l;  // -6 dBFS
  eq.process(l.data(), r.data(), n / 2);
  eq.setBand(kBandHs, settings(kHsFreq, 3000.0, kHsGain, 4.0));
  eq.process(l.data() + n / 2, r.data() + n / 2, n - n / 2);
  float worst = 0.0f;
  for (int i = 1; i < n; ++i) worst = std::max(worst, std::fabs(l[static_cast<std::size_t>(i)] - l[static_cast<std::size_t>(i - 1)]));
  ASSERT_TRUE(worst < 0.05f);
  // the step did take effect
  ASSERT_TRUE(measuredGainDb(eq, 10000.0, sr) > 3.0);
}

void testStageOffIsIdentity() {
  FineEq eq;
  eq.prepare(48000.0, 512);
  eq.setBand(kBandLs, settings(kLsFreq, 100.0, kLsGain, 4.0));
  eq.setBand(kBandHs, settings(kHsFreq, 10000.0, kHsGain, -4.0));
  eq.setStageOn(false);
  for (double f : {50.0, 1000.0, 12000.0}) ASSERT_NEAR_EPS(eq.magnitudeDb(0, f), 0.0, 1e-9);
  std::vector<float> in = sine(1000.0, 48000.0, 48000, 0.3), l = in, r = in;
  eq.process(l.data(), r.data(), 48000);
  for (std::size_t i = 24000; i < l.size(); ++i) ASSERT_NEAR_EPS(l[i], in[i], 1e-6);
  eq.setStageOn(true);
  ASSERT_TRUE(std::fabs(eq.magnitudeDb(0, 50.0)) > 1.0);
}

void testBandOffIsIdentity() {
  FineEq eq;
  eq.prepare(48000.0, 512);
  eq.setStageOn(true);
  BandSettings s = settings(kMidFreq, 1000.0, kMidGain, 4.0);
  s.on = false;
  eq.setBand(kBandMid, s);
  ASSERT_NEAR_EPS(eq.magnitudeDb(0, 1000.0), 0.0, 1e-9);
}

void testNoAllocationInProcess() {
  FineEq eq;
  eq.prepare(48000.0, 512);
  eq.setStageOn(true);
  std::vector<float> l(512, 0.1f), r(512, 0.1f);
  g_allocs = 0;
  g_countAllocs = true;
  for (int i = 0; i < 20; ++i) {
    eq.setBand(kBandLm, settings(kLmFreq, 250.0, kLmGain, (i % 2) ? 2.0 : -2.0));
    BandSettings hp;
    hp.freqIndex = indexOfValue(kHpFreq, 30.0);
    eq.setBand(kBandHp, hp);
    eq.process(l.data(), r.data(), 512);
  }
  g_countAllocs = false;
  ASSERT_TRUE(g_allocs == 0);
}

// ---- Task 3: M/S routing, band IN and LISTEN ----------------------------------------------------

struct Stereo {
  std::vector<float> l, r;
};

Stereo noiseStereo(int n, unsigned seed) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> d(-1.0f, 1.0f);
  Stereo x{std::vector<float>(static_cast<std::size_t>(n)), std::vector<float>(static_cast<std::size_t>(n))};
  for (auto& v : x.l) v = d(rng);
  for (auto& v : x.r) v = d(rng);
  return x;
}

// Gain in dB of the M or S component (0 = M, 1 = S) of a steady two-channel sine.
double componentGainDb(FineEq& eq, double hz, double sr, bool side) {
  const int n = static_cast<int>(sr);
  const std::vector<float> in = sine(hz, sr, n, 0.1);
  std::vector<float> l = in, r = in;
  if (side) for (auto& v : r) v = -v;  // L = -R: pure SIDE
  eq.process(l.data(), r.data(), n);
  std::vector<float> comp(l.size()), ref(l.size());
  for (std::size_t i = 0; i < l.size(); ++i) {
    float m, s;
    DSP::msEncode(l[i], r[i], m, s);
    comp[i] = side ? s : m;
    ref[i] = in[i] * 0.5f * 2.0f;  // the input component has the amplitude of `in`
  }
  const int skip = n / 2;
  return 20.0 * std::log10(rmsOf(comp, skip) / rmsOf(ref, skip));
}

void testMsRoundTripLossless() {
  const Stereo x = noiseStereo(8192, 3);
  for (std::size_t i = 0; i < x.l.size(); ++i) {
    float m, s, l, r;
    DSP::msEncode(x.l[i], x.r[i], m, s);
    ASSERT_NEAR_EPS(m, (x.l[i] + x.r[i]) / 2.0f, 1e-7);
    ASSERT_NEAR_EPS(s, (x.l[i] - x.r[i]) / 2.0f, 1e-7);
    DSP::msDecode(m, s, l, r);
    ASSERT_NEAR_EPS(l, x.l[i], 1e-6);
    ASSERT_NEAR_EPS(r, x.r[i], 1e-6);
  }
}

void testSideBandOnMonoIsSilent() {
  const double sr = 48000.0;
  FineEq eq;
  eq.prepare(sr, 512);
  eq.setStageOn(true);
  BandSettings s = settings(kMidFreq, 1000.0, kMidGain, 4.0);
  s.ms = 2;
  eq.setBand(kBandMid, s);
  const std::vector<float> mono = sine(1000.0, sr, 48000, 0.3);
  std::vector<float> l = mono, r = mono;
  for (int pos = 0; pos < 48000; pos += 512) eq.process(l.data() + pos, r.data() + pos, std::min(512, 48000 - pos));
  for (std::size_t i = 0; i < l.size(); ++i) {
    ASSERT_NEAR_EPS(l[i], mono[i], 1e-6);
    ASSERT_TRUE(l[i] == r[i]);
  }
}

void testMidBandLeavesSideUntouched() {
  const double sr = 48000.0;
  FineEq mid, side;
  for (FineEq* e : {&mid, &side}) {
    e->prepare(sr, 512);
    e->setStageOn(true);
  }
  BandSettings s = settings(kMidFreq, 1000.0, kMidGain, 4.0);
  s.ms = 1;
  mid.setBand(kBandMid, s);
  s.ms = 2;
  side.setBand(kBandMid, s);
  ASSERT_NEAR_EPS(componentGainDb(mid, 1000.0, sr, false), 4.0, 0.1);
  mid.prepare(sr, 512);
  ASSERT_NEAR_EPS(componentGainDb(mid, 1000.0, sr, true), 0.0, 1e-3);
  // and the mirror: a SIDE band leaves the MID alone
  ASSERT_NEAR_EPS(componentGainDb(side, 1000.0, sr, true), 4.0, 0.1);
  side.prepare(sr, 512);
  ASSERT_NEAR_EPS(componentGainDb(side, 1000.0, sr, false), 0.0, 1e-3);
}

void testHpSideUsesMonoBassScale() {
  const double sr = 48000.0;
  FineEq eq;
  eq.prepare(sr, 512);
  eq.setStageOn(true);
  BandSettings hp;
  hp.ms = 2;
  hp.freqIndex = 5;  // last position: 40 Hz on the normal scale, 200 Hz on the SIDE scale
  ASSERT_TRUE(hpFreqHz(true, 5) == 200.0);
  eq.setBand(kBandHp, hp);
  ASSERT_TRUE(eq.magnitudeDb(2, 100.0) < -5.0);
  ASSERT_NEAR_EPS(eq.magnitudeDb(2, 200.0), -3.0, 0.3);  // corner of the SIDE scale
  ASSERT_NEAR_EPS(eq.magnitudeDb(1, 100.0), 0.0, 1e-9);  // the MID curve does not see a SIDE band
  ASSERT_NEAR_EPS(eq.magnitudeDb(0, 100.0), 0.0, 1e-9);  // nor does the STEREO curve
  FineEq sideRun;
  sideRun.prepare(sr, 512);
  sideRun.setStageOn(true);
  sideRun.setBand(kBandHp, hp);
  ASSERT_TRUE(componentGainDb(sideRun, 100.0, sr, true) < -5.0);
  FineEq midRun;
  midRun.prepare(sr, 512);
  midRun.setStageOn(true);
  midRun.setBand(kBandHp, hp);
  ASSERT_NEAR_EPS(componentGainDb(midRun, 100.0, sr, false), 0.0, 1e-3);
  // on ST the same index is the normal 40 Hz scale
  hp.ms = 0;
  eq.setBand(kBandHp, hp);
  ASSERT_NEAR_EPS(eq.magnitudeDb(0, 40.0), -3.0, 0.3);
}

void testMagnitudeCurvesPerTarget() {
  FineEq eq;
  eq.prepare(48000.0, 512);
  eq.setStageOn(true);
  BandSettings st = settings(kLsFreq, 100.0, kLsGain, 4.0);  // ST
  BandSettings md = settings(kMidFreq, 1000.0, kMidGain, 4.0);
  md.ms = 1;
  BandSettings sd = settings(kHsFreq, 10000.0, kHsGain, -4.0);
  sd.ms = 2;
  eq.setBand(kBandLs, st);
  eq.setBand(kBandMid, md);
  eq.setBand(kBandHs, sd);
  // ST curve: ST bands only; MID = ST + MID bands; SIDE = ST + SIDE bands
  ASSERT_TRUE(eq.magnitudeDb(0, 30.0) > 3.0);
  ASSERT_NEAR_EPS(eq.magnitudeDb(0, 1000.0), 0.0, 0.5);
  ASSERT_TRUE(eq.magnitudeDb(1, 1000.0) > 3.5);
  ASSERT_TRUE(eq.magnitudeDb(1, 30.0) > 3.0);
  ASSERT_NEAR_EPS(eq.magnitudeDb(1, 15000.0), 0.0, 0.5);
  ASSERT_TRUE(eq.magnitudeDb(2, 15000.0) < -3.0);
  ASSERT_TRUE(eq.magnitudeDb(2, 30.0) > 3.0);
  ASSERT_NEAR_EPS(eq.magnitudeDb(2, 1000.0), 0.0, 0.5);
}

// A bell soloed by LISTEN is heard as a band-pass at its frequency; a sine at `farHz` falls.
void expectListenSolos(int bell, ParamID freqId, ParamID gainId, double centre, int band) {
  const double sr = 48000.0;
  FineEq eq;
  eq.prepare(sr, 512);
  eq.setStageOn(true);
  eq.setBand(band, settings(freqId, centre, gainId, 4.0, /*Q M*/ 1));
  eq.setListen(bell);
  const double far = measuredGainDb(eq, 100.0, sr);
  FineEq eq2;
  eq2.prepare(sr, 512);
  eq2.setStageOn(true);
  eq2.setBand(band, settings(freqId, centre, gainId, 4.0, 1));
  eq2.setListen(bell);
  const double near = measuredGainDb(eq2, centre, sr);
  if (!(far < -20.0)) { std::fprintf(stderr, "  far %.2f dB\n", far); ++g_failed; return; }
  if (!(std::fabs(near) <= 1.0)) { std::fprintf(stderr, "  near %.2f dB\n", near); ++g_failed; return; }
}

void testListenSolosBell() {
  // LO MID tops out at 800 Hz (the 1 kHz of the brief belongs to the MID bell): both are checked.
  const int before = g_failed;
  expectListenSolos(0, kLmFreq, kLmGain, 800.0, kBandLm);
  ASSERT_TRUE(g_failed == before);
  expectListenSolos(1, kMidFreq, kMidGain, 1000.0, kBandMid);
  ASSERT_TRUE(g_failed == before);
  expectListenSolos(2, kHmFreq, kHmGain, 4000.0, kBandHm);
  ASSERT_TRUE(g_failed == before);
}

void testListenSilencesOtherBandsAndUsesEffectiveQ() {
  const double sr = 48000.0;
  FineEq eq;
  eq.prepare(sr, 512);
  eq.setStageOn(true);
  eq.setBand(kBandLs, settings(kLsFreq, 100.0, kLsGain, 4.0));        // not heard in LISTEN
  eq.setBand(kBandMid, settings(kMidFreq, 1000.0, kMidGain, 4.0, 3));  // Surgical Q
  eq.setListen(1);
  // 100 Hz: no shelf boost, only the band-pass skirt
  ASSERT_TRUE(measuredGainDb(eq, 100.0, sr) < -20.0);
  // an octave above the centre: the narrow (effective Q 7.5) band-pass is far lower than a wide one
  FineEq wide;
  wide.prepare(sr, 512);
  wide.setStageOn(true);
  wide.setBand(kBandMid, settings(kMidFreq, 1000.0, kMidGain, 4.0, 0));
  wide.setListen(1);
  const double narrowDb = measuredGainDb(eq, 2000.0, sr);
  const double wideDb = measuredGainDb(wide, 2000.0, sr);
  ASSERT_TRUE(narrowDb < wideDb - 6.0);
  // LISTEN off: the bands are back
  eq.setListen(-1);
  ASSERT_TRUE(measuredGainDb(eq, 100.0, sr) > 2.0);
  // the curve for the screen ignores LISTEN
  ASSERT_NEAR_EPS(eq.magnitudeDb(0, 1000.0), 4.0, 0.2);
  eq.setListen(1);
  ASSERT_NEAR_EPS(eq.magnitudeDb(0, 1000.0), 4.0, 0.2);
}

void testListenFollowsBandRoute() {
  const double sr = 48000.0;
  FineEq eq;
  eq.prepare(sr, 512);
  eq.setStageOn(true);
  BandSettings s = settings(kMidFreq, 1000.0, kMidGain, 4.0);
  s.ms = 2;
  eq.setBand(kBandMid, s);
  eq.setListen(1);
  // a SIDE bell is heard on the SIDE only: the MID component is silent
  const double sideDb = componentGainDb(eq, 1000.0, sr, true);
  ASSERT_TRUE(std::fabs(sideDb) <= 1.0);
  eq.prepare(sr, 512);
  eq.setListen(1);
  ASSERT_TRUE(componentGainDb(eq, 1000.0, sr, false) < -60.0);
}

void testBandOffAndStageOffAreIdentityMs() {
  const double sr = 48000.0;
  for (int ms = 0; ms < 3; ++ms) {
    FineEq eq;
    eq.prepare(sr, 512);
    eq.setStageOn(true);
    BandSettings s = settings(kMidFreq, 1000.0, kMidGain, 4.0);
    s.ms = ms;
    s.on = false;
    eq.setBand(kBandMid, s);
    for (int target = 0; target < 3; ++target) ASSERT_NEAR_EPS(eq.magnitudeDb(target, 1000.0), 0.0, 1e-9);
    Stereo x = noiseStereo(4096, 11);
    const Stereo x0 = x;
    eq.process(x.l.data(), x.r.data(), 4096);
    for (std::size_t i = 0; i < x.l.size(); ++i) ASSERT_TRUE(x.l[i] == x0.l[i] && x.r[i] == x0.r[i]);

    s.on = true;
    eq.setBand(kBandMid, s);
    eq.setStageOn(false);
    for (int target = 0; target < 3; ++target) ASSERT_NEAR_EPS(eq.magnitudeDb(target, 1000.0), 0.0, 1e-9);
    Stereo y = noiseStereo(4096, 12);
    eq.process(y.l.data(), y.r.data(), 4096);  // the glide to identity is over after the first 64
    Stereo z = noiseStereo(4096, 13);
    const Stereo z0 = z;
    eq.process(z.l.data(), z.r.data(), 4096);
    for (std::size_t i = 0; i < z.l.size(); ++i) ASSERT_TRUE(z.l[i] == z0.l[i] && z.r[i] == z0.r[i]);
  }
  // LISTEN with the stage off stays silent too
  FineEq eq;
  eq.prepare(sr, 512);
  eq.setBand(kBandMid, settings(kMidFreq, 1000.0, kMidGain, 4.0));
  eq.setStageOn(false);
  eq.setListen(1);
  Stereo x = noiseStereo(4096, 14);
  eq.process(x.l.data(), x.r.data(), 4096);
  const Stereo w = noiseStereo(4096, 15);
  Stereo w1 = w;
  eq.process(w1.l.data(), w1.r.data(), 4096);
  for (std::size_t i = 0; i < w.l.size(); ++i) ASSERT_TRUE(w1.l[i] == w.l[i] && w1.r[i] == w.r[i]);
}

void testUnchangedSettingsDoNotRestartRamp() {
  // Repeating the same settings in the middle of a glide must leave it alone: the output is
  // bit-identical to a twin that never saw the repeat.
  const double sr = 48000.0;
  const BandSettings s = settings(kHsFreq, 3000.0, kHsGain, 4.0);
  FineEq eq, twin;
  for (FineEq* e : {&eq, &twin}) {
    e->prepare(sr, 512);
    e->setStageOn(true);
    e->setBand(kBandHs, s);
  }
  Stereo a = noiseStereo(256, 6), b = a;
  eq.process(a.l.data(), a.r.data(), 20);  // 20 of the 64 glide samples
  twin.process(b.l.data(), b.r.data(), 20);
  eq.setBand(kBandHs, s);
  eq.setStageOn(true);
  eq.setListen(-1);
  eq.process(a.l.data() + 20, a.r.data() + 20, 236);
  twin.process(b.l.data() + 20, b.r.data() + 20, 236);
  for (std::size_t i = 0; i < a.l.size(); ++i) ASSERT_TRUE(a.l[i] == b.l[i] && a.r[i] == b.r[i]);
}

void testNoAllocationWithListenAndMs() {
  FineEq eq;
  eq.prepare(48000.0, 512);
  eq.setStageOn(true);
  std::vector<float> l(512, 0.1f), r(512, -0.1f);
  g_allocs = 0;
  g_countAllocs = true;
  for (int i = 0; i < 30; ++i) {
    BandSettings s = settings(kMidFreq, 1000.0, kMidGain, 2.0);
    s.ms = i % 3;
    eq.setBand(kBandMid, s);
    eq.setListen((i % 4) - 1);
    eq.process(l.data(), r.data(), 512);
  }
  g_countAllocs = false;
  ASSERT_TRUE(g_allocs == 0);
}

} // namespace

int main() {
  RUN_TEST(testBellCentreGain);
  RUN_TEST(testNoCrampingNearNyquist);
  RUN_TEST(testHiMidBellFollowsAnalogAt44k);
  RUN_TEST(testBellCutIsReciprocalOfBoost);
  RUN_TEST(testQCoupling);
  RUN_TEST(testHighPassOffIsIdentity);
  RUN_TEST(testHighPassSlope12dB);
  RUN_TEST(testStableAt192kAnd44k);
  RUN_TEST(testGainStepNoZipper);
  RUN_TEST(testStageOffIsIdentity);
  RUN_TEST(testBandOffIsIdentity);
  RUN_TEST(testNoAllocationInProcess);
  RUN_TEST(testMsRoundTripLossless);
  RUN_TEST(testSideBandOnMonoIsSilent);
  RUN_TEST(testMidBandLeavesSideUntouched);
  RUN_TEST(testHpSideUsesMonoBassScale);
  RUN_TEST(testMagnitudeCurvesPerTarget);
  RUN_TEST(testListenSolosBell);
  RUN_TEST(testListenSilencesOtherBandsAndUsesEffectiveQ);
  RUN_TEST(testListenFollowsBandRoute);
  RUN_TEST(testBandOffAndStageOffAreIdentityMs);
  RUN_TEST(testUnchangedSettingsDoNotRestartRamp);
  RUN_TEST(testNoAllocationWithListenAndMs);
  std::printf("%d passed, %d failed\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
