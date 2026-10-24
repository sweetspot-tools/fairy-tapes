/**
 * Fairy Tapes · Happy Ending — Unit tests for the VIBE EQ (LOW trick, AIR, TILT, DYN, TUBE).
 * Run via CTest; build target: happy_ending_vibe_eq_tests.
 */

#include "dsp/vibe_eq.h"
#include "params.h"
#include "tuning.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
using FairyTapes::HappyEnding::DSP::VibeEq;
using FairyTapes::HappyEnding::DSP::VibeSettings;

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

// Position of a value in the table of a parameter (the tests never hardcode indices).
int indexOfValue(ParamID id, double value) {
  const ValueTable t = valueTable(id);
  for (std::size_t i = 0; i < t.size; ++i)
    if (std::fabs(t.data[i] - value) < 1e-9) return static_cast<int>(i);
  return -1;
}

VibeSettings makeSettings() {
  VibeSettings s;
  s.on = true;
  s.low = 0;
  s.air = 0;
  s.tube = 0;
  s.tiltIndex = indexOfValue(kTilt, 0.0);
  s.lowFreqIndex = indexOfValue(kLowFreq, 60.0);
  s.airFreqIndex = indexOfValue(kAirFreq, 10000.0);
  return s;
}

double dbOf(double lin) { return 20.0 * std::log10(std::max(lin, 1e-12)); }
double ampOfDb(double db) { return std::pow(10.0, db / 20.0); }

std::vector<float> sine(double hz, double sr, int n, double amp) {
  std::vector<float> x(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i)
    x[static_cast<std::size_t>(i)] = static_cast<float>(amp * std::sin(2.0 * kPi * hz * i / sr));
  return x;
}

// Amplitude of the component of x at `hz` (single-bin DFT over x[from, size)).
double toneAmp(const std::vector<float>& x, double hz, double sr, int from) {
  double re = 0.0, im = 0.0;
  const int n = static_cast<int>(x.size()) - from;
  for (int i = from; i < static_cast<int>(x.size()); ++i) {
    const double w = 2.0 * kPi * hz * i / sr;
    re += x[static_cast<std::size_t>(i)] * std::cos(w);
    im += x[static_cast<std::size_t>(i)] * std::sin(w);
  }
  return 2.0 * std::sqrt(re * re + im * im) / n;
}

// Runs a mono sine through the network in blocks of `block`; returns the output.
std::vector<float> runLinear(VibeEq& eq, double hz, double sr, double ampDb, double seconds, int block = 256) {
  const int n = static_cast<int>(seconds * sr) / block * block;
  std::vector<float> in = sine(hz, sr, n, ampOfDb(ampDb));
  std::vector<float> l = in, r = in;
  for (int i = 0; i < n; i += block) eq.processLinear(l.data() + i, r.data() + i, block);
  return l;
}

// Gain in dB of a steady sine through processLinear (second half of the run).
double measuredGainDb(VibeEq& eq, double hz, double sr, double ampDb, double seconds = 2.0) {
  const std::vector<float> out = runLinear(eq, hz, sr, ampDb, seconds);
  const int from = static_cast<int>(out.size()) / 2;
  return dbOf(toneAmp(out, hz, sr, from)) - ampDb;
}

// THD of a sine through processTube, in dB relative to the fundamental (harmonics 2..10).
double tubeThdDb(VibeEq& eq, double hz, double sr, double ampDb) {
  const int n = static_cast<int>(sr);  // one second, an integer number of cycles
  std::vector<float> in = sine(hz, sr, n, ampOfDb(ampDb));
  std::vector<float> l = in, r = in;
  const int block = 500;
  for (int i = 0; i + block <= n; i += block) eq.processTube(l.data() + i, r.data() + i, block);
  const int from = n / 2;
  const double fund = toneAmp(l, hz, sr, from);
  double harm = 0.0;
  for (int k = 2; k <= 10; ++k) {
    const double a = toneAmp(l, hz * k, sr, from);
    harm += a * a;
  }
  return dbOf(std::sqrt(harm) / fund);
}

void testLowTrickBoostAndDip() {
  const double rates[] = {44100.0, 48000.0, 96000.0};
  for (double sr : rates) {
    VibeEq eq;
    eq.prepare(sr, 512);
    VibeSettings s = makeSettings();
    s.low = 6;
    eq.set(s);
    ASSERT_TRUE(eq.magnitudeDb(60.0, true) > 2.0);
    double minDb = 1e9;
    for (double f = 100.0; f <= 400.0; f *= 1.02) minDb = std::min(minDb, eq.magnitudeDb(f, true));
    ASSERT_TRUE(minDb < 0.0);
    // The measured path follows the drawn curve (quiet level: the inductor does not saturate).
    ASSERT_NEAR_EPS(measuredGainDb(eq, 60.0, sr, -30.0), eq.magnitudeDb(60.0, true), 0.5);
  }
}

void testLowFrequencySteps() {
  // Every LOW position boosts at its own frequency.
  const double lows[] = {20.0, 30.0, 60.0, 100.0};
  for (double f : lows) {
    VibeEq eq;
    eq.prepare(48000.0, 512);
    VibeSettings s = makeSettings();
    s.low = 6;
    s.lowFreqIndex = indexOfValue(kLowFreq, f);
    eq.set(s);
    ASSERT_TRUE(eq.magnitudeDb(f, true) > 2.0);
  }
}

void testAirBoost() {
  const double rates[] = {44100.0, 48000.0, 96000.0};
  for (double sr : rates) {
    VibeEq eq;
    eq.prepare(sr, 512);
    VibeSettings s = makeSettings();
    s.air = 6;
    eq.set(s);
    ASSERT_TRUE(eq.magnitudeDb(10000.0, true) >= 2.0);
    ASSERT_NEAR_EPS(measuredGainDb(eq, 10000.0, sr, -30.0), eq.magnitudeDb(10000.0, true), 0.5);
    // Broad, but not a lift of the whole band: every AIR frequency leaves the low and mid band alone.
    for (std::size_t i = 0; i < Tables::kAirFreqs.size(); ++i) {
      s.airFreqIndex = static_cast<int>(i);
      eq.set(s);
      ASSERT_TRUE(eq.magnitudeDb(Tables::kAirFreqs[i], true) >= 2.0);
      ASSERT_TRUE(std::fabs(eq.magnitudeDb(200.0, true)) < 0.5);
      ASSERT_TRUE(std::fabs(eq.magnitudeDb(1000.0, true)) < 3.5);  // 16 kHz at 44.1 kHz is the widest
    }
  }
}

void testTiltSymmetric() {
  VibeEq eq;
  eq.prepare(48000.0, 512);
  VibeSettings s = makeSettings();
  eq.set(s);
  for (double f = 20.0; f <= 20000.0; f *= 1.1) ASSERT_NEAR_EPS(eq.magnitudeDb(f, true), 0.0, 0.1);

  for (double db : {3.0, -3.0, 1.5}) {
    s.tiltIndex = indexOfValue(kTilt, db);
    eq.set(s);
    ASSERT_NEAR_EPS(eq.magnitudeDb(50.0, true), -eq.magnitudeDb(10000.0, true), 0.5);
    ASSERT_TRUE(std::fabs(std::fabs(eq.magnitudeDb(10000.0, true)) - std::fabs(db)) < 0.7);
  }
  s.tiltIndex = indexOfValue(kTilt, 3.0);  // BRIGHT: more at the top
  eq.set(s);
  ASSERT_TRUE(eq.magnitudeDb(10000.0, true) > 2.0);
  ASSERT_TRUE(eq.magnitudeDb(50.0, true) < -2.0);
  ASSERT_NEAR_EPS(eq.magnitudeDb(1000.0, true), 0.0, 0.2);
  // Measured through the audio path.
  ASSERT_NEAR_EPS(measuredGainDb(eq, 10000.0, 48000.0, -20.0, 1.0), eq.magnitudeDb(10000.0, true), 0.3);
}

void testDynPullsBackOnLoudProgram() {
  const double sr = 48000.0;
  VibeSettings s = makeSettings();
  s.low = 8;
  s.dynLow = true;

  VibeEq quiet;
  quiet.prepare(sr, 512);
  quiet.set(s);
  const double quietGain = measuredGainDb(quiet, 60.0, sr, -30.0);
  const float quietAct = quiet.dynLowAmount();

  VibeEq loud;
  loud.prepare(sr, 512);
  loud.set(s);
  const double loudGain = measuredGainDb(loud, 60.0, sr, -6.0);
  const float loudAct = loud.dynLowAmount();

  ASSERT_TRUE(quietGain - loudGain >= 2.0);
  ASSERT_TRUE(quietAct == 0.0f);
  ASSERT_TRUE(loudAct > 0.0f && loudAct <= 1.0f);
  ASSERT_TRUE(quiet.dynAirAmount() == 0.0f && loud.dynAirAmount() == 0.0f);

  // The pull-back is the DYN's: with DYN off the loud case keeps (nearly) its boost and no ACT.
  VibeEq off;
  off.prepare(sr, 512);
  s.dynLow = false;
  off.set(s);
  const double offGain = measuredGainDb(off, 60.0, sr, -6.0);
  ASSERT_TRUE(off.dynLowAmount() == 0.0f);
  ASSERT_TRUE(offGain > loudGain + 1.0);

  // magnitudeDb: static ignores the pull-back, dynamic follows it.
  s.dynLow = true;
  ASSERT_TRUE(loud.magnitudeDb(60.0, true) > loud.magnitudeDb(60.0, false) + 1.0);
}

void testDynPullsBackAirOnLoudProgram() {
  const double sr = 48000.0;
  VibeSettings s = makeSettings();
  s.air = 8;
  s.dynAir = true;
  VibeEq quiet, loud;
  quiet.prepare(sr, 512);
  loud.prepare(sr, 512);
  quiet.set(s);
  loud.set(s);
  const double quietGain = measuredGainDb(quiet, 10000.0, sr, -30.0);
  const double loudGain = measuredGainDb(loud, 10000.0, sr, -6.0);
  ASSERT_TRUE(quietGain - loudGain >= 2.0);
  ASSERT_TRUE(quiet.dynAirAmount() == 0.0f);
  ASSERT_TRUE(loud.dynAirAmount() > 0.0f);
  ASSERT_TRUE(loud.dynLowAmount() == 0.0f);
}

void testTubeZeroIsNearlyLinear() {
  VibeEq eq;
  eq.prepare(48000.0, 512);
  VibeSettings s = makeSettings();
  s.tube = 0;
  eq.set(s);
  ASSERT_TRUE(tubeThdDb(eq, 1000.0, 48000.0, -6.0) < -60.0);
}

void testTubeAddsHarmonics() {
  VibeEq eq;
  eq.prepare(48000.0, 512);
  VibeSettings s = makeSettings();
  s.tube = 10;
  eq.set(s);
  ASSERT_TRUE(tubeThdDb(eq, 1000.0, 48000.0, -6.0) > -40.0);
}

void testTubeGrowsWithTheKnob() {
  double prev = -200.0;
  for (int t : {1, 2, 5, 10}) {
    VibeEq eq;
    eq.prepare(48000.0, 512);
    VibeSettings s = makeSettings();
    s.tube = t;
    eq.set(s);
    const double thd = tubeThdDb(eq, 1000.0, 48000.0, -6.0);
    ASSERT_TRUE(thd > prev);
    prev = thd;
  }
}

void testStageOffIsIdentity() {
  VibeEq eq;
  eq.prepare(48000.0, 512);
  VibeSettings s = makeSettings();
  s.low = 10;
  s.air = 10;
  s.tiltIndex = indexOfValue(kTilt, 3.0);
  s.tube = 10;
  s.dynLow = true;
  s.on = false;
  eq.set(s);
  std::mt19937 rng(7);
  std::uniform_real_distribution<float> d(-0.8f, 0.8f);
  std::vector<float> l(2048), r(2048);
  for (auto& v : l) v = d(rng);
  for (auto& v : r) v = d(rng);
  const std::vector<float> l0 = l, r0 = r;
  eq.processLinear(l.data(), r.data(), 2048);
  eq.processTube(l.data(), r.data(), 2048);
  ASSERT_TRUE(std::memcmp(l.data(), l0.data(), l.size() * sizeof(float)) == 0);
  ASSERT_TRUE(std::memcmp(r.data(), r0.data(), r.size() * sizeof(float)) == 0);
  ASSERT_NEAR_EPS(eq.magnitudeDb(60.0, true), 0.0, 1e-9);
  ASSERT_TRUE(eq.dynLowAmount() == 0.0f);
}

void testStageGoingOffSettlesToIdentity() {
  VibeEq eq;
  eq.prepare(48000.0, 512);
  VibeSettings s = makeSettings();
  s.low = 8;
  s.air = 8;
  s.tube = 8;
  eq.set(s);
  std::vector<float> l = sine(100.0, 48000.0, 4800, 0.3), r = l;
  eq.processLinear(l.data(), r.data(), 4800);
  s.on = false;
  eq.set(s);
  l = sine(100.0, 48000.0, 4800, 0.3);
  r = l;
  const std::vector<float> l0 = l;
  eq.processLinear(l.data(), r.data(), 4800);
  eq.processTube(l.data(), r.data(), 4800);
  for (int i = 2400; i < 4800; ++i) ASSERT_TRUE(l[static_cast<std::size_t>(i)] == l0[static_cast<std::size_t>(i)]);
}

void testNeutralSettingsAreTransparent() {
  VibeEq eq;
  eq.prepare(48000.0, 512);
  VibeSettings s = makeSettings();  // LOW 0, AIR 0, TILT 0, TUBE 0
  eq.set(s);
  ASSERT_NEAR_EPS(measuredGainDb(eq, 1000.0, 48000.0, -6.0, 1.0), 0.0, 0.01);
  ASSERT_NEAR_EPS(measuredGainDb(eq, 60.0, 48000.0, -6.0, 1.0), 0.0, 0.01);
}

void testStableAtAllRatesAndMaxSettings() {
  const double rates[] = {44100.0, 48000.0, 96000.0, 192000.0};
  for (double sr : rates) {
    VibeEq eq;
    eq.prepare(sr, 1024);
    VibeSettings s = makeSettings();
    s.low = 10;
    s.air = 10;
    s.tiltIndex = indexOfValue(kTilt, 3.0);
    s.tube = 10;
    s.dynLow = true;
    s.dynAir = true;
    for (int lf : {0, 1, 2, 3}) {
      s.lowFreqIndex = lf;
      s.airFreqIndex = lf + 1;
      eq.set(s);
      std::mt19937 rng(11);
      std::uniform_real_distribution<float> d(-1.0f, 1.0f);
      for (int b = 0; b < 40; ++b) {
        std::vector<float> l(1024), r(1024);
        for (auto& v : l) v = d(rng);
        for (auto& v : r) v = d(rng);
        eq.processLinear(l.data(), r.data(), 1024);
        eq.processTube(l.data(), r.data(), 1024);
        for (std::size_t i = 0; i < l.size(); ++i) {
          ASSERT_TRUE(std::isfinite(l[i]) && std::isfinite(r[i]));
          ASSERT_TRUE(std::fabs(l[i]) < 40.0f && std::fabs(r[i]) < 40.0f);
        }
      }
    }
  }
}

void testSettingsChangeIsContinuous() {
  // A jump of LOW from 0 to 10 glides: no sample-to-sample step bigger than the signal allows.
  const double sr = 48000.0;
  VibeEq eq;
  eq.prepare(sr, 512);
  VibeSettings s = makeSettings();
  eq.set(s);
  std::vector<float> warm = sine(1000.0, sr, 4800, 0.1), wr = warm;
  eq.processLinear(warm.data(), wr.data(), 4800);
  s.low = 10;
  s.air = 10;
  eq.set(s);
  std::vector<float> l = sine(1000.0, sr, 4800, 0.1), r = l;
  eq.processLinear(l.data(), r.data(), 4800);
  float maxStep = 0.0f;
  for (std::size_t i = 1; i < l.size(); ++i) maxStep = std::max(maxStep, std::fabs(l[i] - l[i - 1]));
  ASSERT_TRUE(maxStep < 0.1f);  // a 1 kHz sine of 0.1 moves at most ~0.013 per sample
}

void testMonoStaysMono() {
  VibeEq eq;
  eq.prepare(48000.0, 512);
  VibeSettings s = makeSettings();
  s.low = 9;
  s.air = 7;
  s.tube = 6;
  s.dynLow = true;
  eq.set(s);
  std::vector<float> l = sine(80.0, 48000.0, 9600, 0.7), r = l;
  for (int i = 0; i < 9600; i += 480) {
    eq.processLinear(l.data() + i, r.data() + i, 480);
    eq.processTube(l.data() + i, r.data() + i, 480);
  }
  ASSERT_TRUE(std::memcmp(l.data(), r.data(), l.size() * sizeof(float)) == 0);
}

void testNoAllocationInProcess() {
  VibeEq eq;
  eq.prepare(48000.0, 512);
  VibeSettings s = makeSettings();
  s.low = 6;
  s.air = 6;
  s.tube = 5;
  s.dynLow = true;
  s.dynAir = true;
  eq.set(s);
  std::vector<float> l = sine(100.0, 48000.0, 512, 0.5), r = l;
  g_allocs = 0;
  g_countAllocs = true;
  for (int i = 0; i < 30; ++i) {
    s.low = i % 11;
    s.on = (i % 7) != 3;
    eq.set(s);
    eq.processLinear(l.data(), r.data(), 512);
    eq.processTube(l.data(), r.data(), 512);
    (void)eq.magnitudeDb(100.0, false);
  }
  g_countAllocs = false;
  ASSERT_TRUE(g_allocs == 0);
}

} // namespace

int main() {
  RUN_TEST(testLowTrickBoostAndDip);
  RUN_TEST(testLowFrequencySteps);
  RUN_TEST(testAirBoost);
  RUN_TEST(testTiltSymmetric);
  RUN_TEST(testDynPullsBackOnLoudProgram);
  RUN_TEST(testDynPullsBackAirOnLoudProgram);
  RUN_TEST(testTubeZeroIsNearlyLinear);
  RUN_TEST(testTubeAddsHarmonics);
  RUN_TEST(testTubeGrowsWithTheKnob);
  RUN_TEST(testStageOffIsIdentity);
  RUN_TEST(testStageGoingOffSettlesToIdentity);
  RUN_TEST(testNeutralSettingsAreTransparent);
  RUN_TEST(testStableAtAllRatesAndMaxSettings);
  RUN_TEST(testSettingsChangeIsContinuous);
  RUN_TEST(testMonoStaysMono);
  RUN_TEST(testNoAllocationInProcess);
  std::printf("%d passed, %d failed\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
