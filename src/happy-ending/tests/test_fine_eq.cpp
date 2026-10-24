/**
 * Fairy Tapes · Happy Ending — Unit tests for the matched biquads and the FINE EQ bands.
 * Run via CTest; build target: happy_ending_fine_eq_tests.
 */

#include "dsp/fine_eq.h"
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

} // namespace

int main() {
  RUN_TEST(testBellCentreGain);
  RUN_TEST(testNoCrampingNearNyquist);
  RUN_TEST(testQCoupling);
  RUN_TEST(testHighPassOffIsIdentity);
  RUN_TEST(testHighPassSlope12dB);
  RUN_TEST(testStableAt192kAnd44k);
  RUN_TEST(testGainStepNoZipper);
  RUN_TEST(testStageOffIsIdentity);
  RUN_TEST(testBandOffIsIdentity);
  RUN_TEST(testNoAllocationInProcess);
  std::printf("%d passed, %d failed\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
