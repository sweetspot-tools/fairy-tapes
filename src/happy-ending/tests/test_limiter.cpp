/**
 * Fairy Tapes · Happy Ending — Unit tests for LIMIT (look-ahead sample-peak limiter).
 * Run via CTest; build target: happy_ending_limiter_tests.
 */

#include "dsp/limiter.h"
#include "tuning.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
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

using namespace FairyTapes::HappyEnding;
using FairyTapes::HappyEnding::DSP::Limiter;
using FairyTapes::HappyEnding::DSP::LimiterSettings;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSr = 48000.0;
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

double ampOfDb(double db) { return std::pow(10.0, db / 20.0); }
double dbOf(double lin) { return 20.0 * std::log10(std::max(lin, 1e-12)); }

// The linear ceiling, compared in double against the float output.
double ceilLin(double db) { return ampOfDb(db); }

struct Rng {
  std::uint32_t s;
  explicit Rng(std::uint32_t seed) : s(seed) {}
  double white() {  // [-1, 1)
    s = s * 1664525u + 1013904223u;
    return static_cast<double>(s >> 8) / 8388608.0 - 1.0;
  }
};

// Pink noise (Paul Kellet's filter), scaled so that the largest sample of l and r together is `peak`.
void pinkPair(std::vector<float>& l, std::vector<float>& r, int n, double peak, std::uint32_t seed) {
  l.assign(static_cast<std::size_t>(n), 0.0f);
  r.assign(static_cast<std::size_t>(n), 0.0f);
  double m = 0.0;
  for (int ch = 0; ch < 2; ++ch) {
    Rng rng(seed + static_cast<std::uint32_t>(ch) * 7919u);
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    auto& v = ch == 0 ? l : r;
    for (int i = 0; i < n; ++i) {
      const double w = rng.white();
      b0 = 0.99886 * b0 + w * 0.0555179;
      b1 = 0.99332 * b1 + w * 0.0750759;
      b2 = 0.96900 * b2 + w * 0.1538520;
      b3 = 0.86650 * b3 + w * 0.3104856;
      b4 = 0.55000 * b4 + w * 0.5329522;
      b5 = -0.7616 * b5 - w * 0.0168980;
      const double p = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362;
      b6 = w * 0.115926;
      v[static_cast<std::size_t>(i)] = static_cast<float>(p);
      m = std::max(m, std::fabs(p));
    }
  }
  const float g = static_cast<float>(peak / m);
  for (auto& x : l) x *= g;
  for (auto& x : r) x *= g;
}

double peakOf(const std::vector<float>& x, std::size_t from = 0) {
  double m = 0.0;
  for (std::size_t i = from; i < x.size(); ++i) m = std::max(m, static_cast<double>(std::fabs(x[i])));
  return m;
}

LimiterSettings make(int style, float thresh, float ceilDb, int trim = 4, bool set = false, bool on = true) {
  LimiterSettings s;
  s.on = on;
  s.threshDb = thresh;
  s.style = style;
  s.releaseTrimIndex = trim;
  s.ceilingDb = ceilDb;
  s.set = set;
  return s;
}

// Runs l / r in place through `lim` in blocks of `block` samples.
void run(Limiter& lim, std::vector<float>& l, std::vector<float>& r, int block = 64) {
  const int n = static_cast<int>(l.size());
  for (int i = 0; i < n; i += block)
    lim.process(l.data() + i, r.data() + i, std::min(block, n - i));
}

std::vector<float> sineVec(int n, double freq, double amp, double sr = kSr) {
  std::vector<float> x(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) x[static_cast<std::size_t>(i)] = static_cast<float>(amp * std::sin(2.0 * kPi * freq * i / sr));
  return x;
}

// ---------------------------------------------------------------------------------------------

void testNeverAboveCeiling() {
  std::vector<float> pl, pr;
  pinkPair(pl, pr, 24000, 2.0, 12345u);  // +6 dBFS pink noise, both channels
  const float ceilings[4] = {-0.1f, -0.3f, -0.5f, -1.0f};
  const float threshs[3] = {0.0f, -2.0f, -4.0f};
  for (int style = 0; style < 3; ++style)
    for (float c : ceilings)
      for (float t : threshs)
        for (int set = 0; set < 2; ++set) {
          Limiter lim;
          lim.prepare(kSr, 64);
          lim.set(make(style, t, c, 4, set != 0));
          std::vector<float> l = pl, r = pr;
          run(lim, l, r);
          const double cl = ceilLin(c);
          if (!(peakOf(l) <= cl && peakOf(r) <= cl)) {
            std::fprintf(stderr, "  over ceiling: style %d ceil %.1f thresh %.1f set %d: %.6f / %.6f > %.6f\n",
                         style, c, t, set, peakOf(l), peakOf(r), cl);
          }
          ASSERT_TRUE(peakOf(l) <= cl);
          ASSERT_TRUE(peakOf(r) <= cl);
          // it must be limiting, not muting: the output gets close to the ceiling
          // (with SET the peaks sit below the ceiling by the distance between THRESH and -8 dB)
          if (!set) ASSERT_TRUE(peakOf(l) > 0.9 * cl);
        }
}

void testNeverAboveCeilingOnClicks() {
  const int n = 12000;
  for (int style = 0; style < 3; ++style)
    for (float c : {-0.1f, -1.0f}) {
      Limiter lim;
      lim.prepare(kSr, 64);
      lim.set(make(style, -2.0f, c));
      std::vector<float> l(n, 0.0f), r(n, 0.0f);
      const float amps[6] = {1.0f, 2.0f, 4.0f, 8.0f, 0.9f, 16.0f};
      for (int k = 0; k < 6; ++k) {
        l[static_cast<std::size_t>(500 + k * 1500)] = (k % 2 ? -1.0f : 1.0f) * amps[k];
        r[static_cast<std::size_t>(503 + k * 1500)] = (k % 2 ? 1.0f : -1.0f) * amps[k];
      }
      // a two-sample burst too
      l[9700] = l[9701] = 6.0f;
      run(lim, l, r);
      const double cl = ceilLin(c);
      ASSERT_TRUE(peakOf(l) <= cl);
      ASSERT_TRUE(peakOf(r) <= cl);
    }
}

void testStyleAndSetChangesKeepCeiling() {
  // changing style, threshold, release and SET at every block must never let a sample through
  std::vector<float> l, r;
  pinkPair(l, r, 48000, 3.0, 777u);
  Limiter lim;
  lim.prepare(kSr, 64);
  Rng rng(5u);
  for (int i = 0; i + 64 <= 48000; i += 64) {
    const int style = static_cast<int>((rng.white() + 1.0) * 1.49);
    const float thresh = -0.5f * static_cast<float>(static_cast<int>((rng.white() + 1.0) * 4.49));
    lim.set(make(style, thresh, -0.3f, static_cast<int>((rng.white() + 1.0) * 4.49), rng.white() > 0.5));
    lim.process(l.data() + i, r.data() + i, 64);
  }
  const double cl = ceilLin(-0.3);
  ASSERT_TRUE(peakOf(l) <= cl);
  ASSERT_TRUE(peakOf(r) <= cl);
}

void testLatencyConstantAcrossStyles() {
  Limiter lim;
  lim.prepare(kSr, 64);
  lim.set(make(0, -2.0f, -0.3f));
  const int base = lim.latencySamples();
  ASSERT_TRUE(base >= static_cast<int>(std::ceil(Tuning::kLimitLookAheadMs[0] * 0.001 * kSr)));
  for (int style = 0; style < 3; ++style) {
    lim.set(make(style, -2.0f, -0.3f));
    ASSERT_TRUE(lim.latencySamples() == base);
  }
  // and the delay really is that many samples: an off limiter is a pure delay
  Limiter off;
  off.prepare(kSr, 64);
  off.set(make(1, -2.0f, -0.3f, 4, false, false));
  ASSERT_TRUE(off.latencySamples() == base);
  std::vector<float> l(base + 100, 0.0f), r(base + 100, 0.0f);
  l[10] = 0.5f;
  r[20] = -0.25f;
  run(off, l, r);
  ASSERT_TRUE(l[10 + base] == 0.5f);
  ASSERT_TRUE(r[20 + base] == -0.25f);
}

void testLatencyMultipleOfOversampling() {
  for (double sr : {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0})
    for (int mult : {1, 2, 4}) {
      Limiter lim;
      lim.prepare(sr, 128, mult);
      const int lat = lim.latencySamples();
      const int want = static_cast<int>(std::ceil(Tuning::kLimitLookAheadMs[0] * 0.001 * sr));
      ASSERT_TRUE(lat % mult == 0);
      ASSERT_TRUE(lat >= want);
      ASSERT_TRUE(lat - want < mult);
      for (int style = 0; style < 3; ++style) {
        lim.set(make(style, -1.0f, -0.3f));
        ASSERT_TRUE(lim.latencySamples() == lat);
      }
    }
}

// Time constant of the release, in ms: after `burst` samples of a loud sine, then a quiet one,
// the time the gain deficit takes to go from 1/e to 1/e^2 of its starting value.
double releaseTauMs(int style, int trim, int burst) {
  Limiter lim;
  lim.prepare(kSr, 8);
  lim.set(make(style, -2.0f, -0.3f, trim));
  const int total = burst + static_cast<int>(1.5 * kSr);
  std::vector<float> l(static_cast<std::size_t>(total)), r;
  for (int i = 0; i < total; ++i)
    l[static_cast<std::size_t>(i)] = static_cast<float>((i < burst ? 2.0 : 0.05) * std::sin(2.0 * kPi * 1000.0 * i / kSr));
  r = l;
  std::vector<double> deficit;
  for (int i = 0; i + 8 <= total; i += 8) {
    lim.process(l.data() + i, r.data() + i, 8);
    deficit.push_back(1.0 - std::pow(10.0, -static_cast<double>(lim.grDb()) / 20.0));
  }
  double d0 = 0.0;
  std::size_t peakAt = 0;
  for (std::size_t i = 0; i < deficit.size(); ++i)
    if (deficit[i] > d0) { d0 = deficit[i]; peakAt = i; }
  std::size_t i1 = 0, i2 = 0;
  for (std::size_t i = peakAt; i < deficit.size(); ++i) {
    if (!i1 && deficit[i] <= d0 / std::exp(1.0)) i1 = i;
    if (!i2 && deficit[i] <= d0 / std::exp(2.0)) { i2 = i; break; }
  }
  if (!i1 || !i2) return -1.0;
  return static_cast<double>(i2 - i1) * 8.0 / kSr * 1000.0;
}

void testReleaseTrim() {
  const double base = Tuning::kLimitReleaseBaseMs[0];
  const int burst = static_cast<int>(0.6 * kSr);
  const double t0 = releaseTauMs(0, 4, burst);
  const double t4 = releaseTauMs(0, 8, burst);
  const double tm4 = releaseTauMs(0, 0, burst);
  ASSERT_TRUE(t0 > 0 && t4 > 0 && tm4 > 0);
  ASSERT_NEAR_EPS(t0, base, 0.1 * base);
  ASSERT_NEAR_EPS(t4, base * 2.52, 0.1 * base * 2.52);
  ASSERT_NEAR_EPS(tm4, base / 2.52, 0.1 * base / 2.52);
  // PUNCH has its own base
  const double p0 = releaseTauMs(1, 4, burst);
  ASSERT_NEAR_EPS(p0, Tuning::kLimitReleaseBaseMs[1], 0.1 * Tuning::kLimitReleaseBaseMs[1]);
}

void testDynReleaseFollowsProgram() {
  const double shortBurst = releaseTauMs(2, 4, static_cast<int>(0.02 * kSr));
  const double longBurst = releaseTauMs(2, 4, static_cast<int>(1.5 * kSr));
  ASSERT_TRUE(shortBurst > 0 && longBurst > 0);
  ASSERT_TRUE(shortBurst < 0.75 * longBurst);
}

void testPunchLetsTransientThrough() {
  const double cl = ceilLin(-0.3);
  // silence, then a rise to +6 dBFS in 0.25 ms (a click-like attack), held for 20 ms
  const int start = 4800, rise = 12, hold = 960, total = 9600;
  auto input = [&]() {
    std::vector<float> x(static_cast<std::size_t>(total), 0.0f);
    for (int i = 0; i < hold; ++i)
      x[static_cast<std::size_t>(start + i)] = static_cast<float>(2.0 * std::min(1.0, (i + 1.0) / rise));
    return x;
  };
  const double thresh = ampOfDb(-2.0);
  double first[2] = {0, 0};
  for (int style = 0; style < 2; ++style) {
    Limiter lim;
    lim.prepare(kSr, 64);
    lim.set(make(style, -2.0f, -0.3f));
    std::vector<float> l = input(), r = input();
    const std::vector<float> in = l;
    run(lim, l, r);
    ASSERT_TRUE(peakOf(l) <= cl);
    // first input sample above the threshold
    int c = 0;
    while (std::fabs(in[static_cast<std::size_t>(c)]) <= thresh) ++c;
    first[style] = std::fabs(l[static_cast<std::size_t>(c + lim.latencySamples())]);
  }
  // style 0 = MODERN, style 1 = PUNCH
  ASSERT_TRUE(first[1] > first[0]);
  ASSERT_TRUE(first[1] <= cl);
}

void testSetForcesThreshold() {
  Limiter lim;
  lim.prepare(kSr, 64);
  lim.set(make(0, -2.0f, -0.3f));
  ASSERT_TRUE(lim.effectiveThreshDb() == -2.0f);  // snaps on the first set
  std::vector<float> l(64, 0.0f), r(64, 0.0f);
  auto runMs = [&](double ms) {
    const int blocks = static_cast<int>(ms * 0.001 * kSr / 64);
    for (int b = 0; b < blocks; ++b) lim.process(l.data(), r.data(), 64);
  };
  runMs(100.0);
  ASSERT_TRUE(lim.effectiveThreshDb() == -2.0f);
  lim.set(make(0, -2.0f, -0.3f, 4, true));
  runMs(60.0);
  const float mid = lim.effectiveThreshDb();
  ASSERT_TRUE(mid < -2.0f && mid > Tuning::kSetThresholdDb);  // visibly on its way
  runMs(140.0);
  ASSERT_NEAR_EPS(lim.effectiveThreshDb(), Tuning::kSetThresholdDb, 1e-3);
  lim.set(make(0, -2.0f, -0.3f, 4, false));
  runMs(200.0);
  ASSERT_NEAR_EPS(lim.effectiveThreshDb(), -2.0, 1e-3);
  // an ordinary THRESH step glides too and lands exactly
  lim.set(make(0, -4.0f, -0.3f));
  runMs(100.0);
  ASSERT_TRUE(lim.effectiveThreshDb() == -4.0f);
}

void testSetSqueezesAudibly() {
  std::vector<float> l0 = sineVec(24000, 1000.0, 0.7), r0 = l0;
  auto settledGr = [&](bool set) {
    Limiter lim;
    lim.prepare(kSr, 64);
    lim.set(make(0, -2.0f, -0.3f, 4, set));
    std::vector<float> l = l0, r = r0;
    run(lim, l, r);
    return lim.grDb();
  };
  ASSERT_TRUE(settledGr(false) < 0.01f);
  ASSERT_TRUE(settledGr(true) > 4.0f);  // 0.7 (-3 dBFS) against a threshold of -8 dB: about 4.9 dB
}

void testSetKeepsLoudness() {
  // pink noise at -12 dBFS RMS: SET squeezes more but the gain recovery stays that of THRESH,
  // so the output is not louder than without SET
  std::vector<float> pl, pr;
  pinkPair(pl, pr, 96000, 1.0, 2024u);
  double rms = 0.0;
  for (float x : pl) rms += static_cast<double>(x) * x;
  rms = std::sqrt(rms / static_cast<double>(pl.size()));
  const float g = static_cast<float>(ampOfDb(-12.0) / rms);
  for (auto& x : pl) x *= g;
  for (auto& x : pr) x *= g;
  double outRms[2] = {0, 0};
  float gr[2] = {0, 0};
  for (int set = 0; set < 2; ++set) {
    Limiter lim;
    lim.prepare(kSr, 64);
    lim.set(make(0, -2.0f, -0.3f, 4, set != 0));
    std::vector<float> l = pl, r = pr;
    float maxGr = 0.0f;
    const int n = static_cast<int>(l.size());
    for (int i = 0; i + 64 <= n; i += 64) {
      lim.process(l.data() + i, r.data() + i, 64);
      if (i >= n / 2) maxGr = std::max(maxGr, lim.grDb());
    }
    ASSERT_TRUE(peakOf(l) <= ceilLin(-0.3));
    double s = 0.0;
    for (int i = n / 2; i < n; ++i) s += static_cast<double>(l[static_cast<std::size_t>(i)]) * l[static_cast<std::size_t>(i)];
    outRms[set] = std::sqrt(s / (n - n / 2));
    gr[set] = maxGr;
  }
  ASSERT_TRUE(gr[1] > gr[0]);
  ASSERT_TRUE(dbOf(outRms[1]) <= dbOf(outRms[0]) + 0.5);
}

void testOffIsDelayedIdentity() {
  std::vector<float> pl, pr;
  pinkPair(pl, pr, 20000, 1.8, 4242u);
  Limiter lim;
  lim.prepare(kSr, 64);
  lim.set(make(0, -2.0f, -0.3f, 4, false, false));
  std::vector<float> l = pl, r = pr;
  run(lim, l, r);
  const int d = lim.latencySamples();
  for (int i = 0; i < 20000; ++i) {
    const float wl = i < d ? 0.0f : pl[static_cast<std::size_t>(i - d)];
    const float wr = i < d ? 0.0f : pr[static_cast<std::size_t>(i - d)];
    ASSERT_TRUE(l[static_cast<std::size_t>(i)] == wl);
    ASSERT_TRUE(r[static_cast<std::size_t>(i)] == wr);
  }
  // after switching a working limiter off, it settles to the same exact identity
  Limiter lim2;
  lim2.prepare(kSr, 64);
  lim2.set(make(0, -2.0f, -0.3f));
  std::vector<float> l2 = pl, r2 = pr;
  const int blocks = 10000 / 64;  // whole blocks only: the limiter is switched off at a block boundary
  for (int b = 0; b < blocks; ++b) lim2.process(l2.data() + b * 64, r2.data() + b * 64, 64);
  lim2.set(make(0, -2.0f, -0.3f, 4, false, false));
  for (int b = blocks; b < 20000 / 64; ++b) lim2.process(l2.data() + b * 64, r2.data() + b * 64, 64);
  const int from = blocks * 64 + static_cast<int>(0.05 * kSr);  // crossfade (10 ms) long over
  for (int i = from; i < (20000 / 64) * 64; ++i) {
    ASSERT_TRUE(l2[static_cast<std::size_t>(i)] == pl[static_cast<std::size_t>(i - d)]);
    ASSERT_TRUE(r2[static_cast<std::size_t>(i)] == pr[static_cast<std::size_t>(i - d)]);
  }
}

void testGainRecoveryLikeL2() {
  // below the threshold: pure gain of (ceiling - threshold)
  {
    Limiter lim;
    lim.prepare(kSr, 64);
    lim.set(make(0, -2.0f, -0.3f));
    std::vector<float> l = sineVec(9600, 1000.0, 0.1), r = l;
    run(lim, l, r);
    const double want = ampOfDb(-0.3 - (-2.0));
    for (int i = 2000; i < 9600; ++i)
      ASSERT_NEAR_EPS(l[static_cast<std::size_t>(i)], 0.1 * want * std::sin(2.0 * kPi * 1000.0 * (i - lim.latencySamples()) / kSr), 2e-5);
  }
  // above it: the peaks sit at the ceiling
  for (int style = 0; style < 3; ++style) {
    Limiter lim;
    lim.prepare(kSr, 64);
    lim.set(make(style, -2.0f, -0.3f));
    std::vector<float> l = sineVec(48000, 1000.0, 2.0), r = l;
    run(lim, l, r);
    double m = 0.0;
    for (std::size_t i = 40000; i < l.size(); ++i) m = std::max(m, static_cast<double>(std::fabs(l[i])));
    ASSERT_TRUE(m <= ceilLin(-0.3));
    ASSERT_TRUE(dbOf(m) > -0.3 - 0.25);
  }
}

void testStereoLinked() {
  Limiter lim;
  lim.prepare(kSr, 64);
  lim.set(make(0, -2.0f, -0.3f));
  std::vector<float> l = sineVec(48000, 1000.0, 2.0), r = sineVec(48000, 1000.0, 0.1);
  const std::vector<float> l0 = l, r0 = r;
  run(lim, l, r);
  const int d = lim.latencySamples();
  // same gain on both: output / delayed input is equal sample for sample
  for (int i = 40000; i < 47000; ++i) {
    if (std::fabs(l0[static_cast<std::size_t>(i - d)]) < 0.5f) continue;
    const double gl = l[static_cast<std::size_t>(i)] / l0[static_cast<std::size_t>(i - d)];
    const double gr = r[static_cast<std::size_t>(i)] / r0[static_cast<std::size_t>(i - d)];
    ASSERT_NEAR_EPS(gl, gr, 1e-3 * std::fabs(gl));
  }
  // and the quiet channel is held down by the loud one: well below plain gain recovery
  ASSERT_TRUE(peakOf(r, 40000) < 0.1 * 0.7 * ampOfDb(1.7));
}

void testDynSoftKnee() {
  auto outRms = [](int style) {
    Limiter lim;
    lim.prepare(kSr, 64);
    lim.set(make(style, -2.0f, -0.3f));
    std::vector<float> l = sineVec(24000, 1000.0, 0.7), r = l;  // peak 1 dB under the threshold
    run(lim, l, r);
    double s = 0.0;
    for (std::size_t i = 12000; i < l.size(); ++i) s += static_cast<double>(l[i]) * l[i];
    return std::sqrt(s / 12000.0);
  };
  const double hard = outRms(0), soft = outRms(2);
  // MODERN is hard knee: just the gain recovery; DYN already leans on it
  ASSERT_NEAR_EPS(dbOf(hard / (0.7 / std::sqrt(2.0))), 1.7, 0.01);
  ASSERT_TRUE(dbOf(soft) < dbOf(hard) - 0.1);
}

void testGrDbReporting() {
  Limiter lim;
  lim.prepare(kSr, 64);
  lim.set(make(0, -2.0f, -0.3f));
  std::vector<float> l = sineVec(24000, 1000.0, 0.05), r = l;
  run(lim, l, r);
  ASSERT_TRUE(lim.grDb() == 0.0f);
  l = sineVec(24000, 1000.0, 2.0);
  r = l;
  run(lim, l, r);
  // 2.0 against a 0.794 threshold: about 8 dB
  ASSERT_NEAR_EPS(lim.grDb(), 20.0 * std::log10(2.0 / ampOfDb(-2.0)), 0.3);
}

void testSilenceStaysSilent() {
  Limiter lim;
  lim.prepare(kSr, 64);
  lim.set(make(2, -1.0f, -0.5f));
  std::vector<float> l(4096, 0.0f), r(4096, 0.0f);
  run(lim, l, r);
  ASSERT_TRUE(peakOf(l) == 0.0 && peakOf(r) == 0.0);
  ASSERT_TRUE(lim.grDb() == 0.0f);
}

void testWorksAtOversampledRate() {
  // the engine runs the limiter at 2x / 4x: same guarantees, delay of a multiple of the factor
  for (int mult : {2, 4}) {
    const double sr = kSr * mult;
    std::vector<float> pl, pr;
    pinkPair(pl, pr, 24000 * mult, 2.0, 31u);
    Limiter lim;
    lim.prepare(sr, 64, mult);
    lim.set(make(1, -3.0f, -1.0f));
    run(lim, pl, pr);
    ASSERT_TRUE(peakOf(pl) <= ceilLin(-1.0));
    ASSERT_TRUE(peakOf(pr) <= ceilLin(-1.0));
    ASSERT_TRUE(lim.latencySamples() % mult == 0);
  }
}

void testNoAllocationInProcess() {
  Limiter lim;
  lim.prepare(kSr, 512);
  std::vector<float> pl, pr;
  pinkPair(pl, pr, 512, 2.0, 5u);
  g_allocs = 0;
  g_countAllocs = true;
  for (int i = 0; i < 40; ++i) {
    lim.set(make(i % 3, -0.5f * static_cast<float>(i % 9), -0.3f, i % 9, i % 5 == 0, i % 7 != 0));
    lim.process(pl.data(), pr.data(), 512);
    (void)lim.grDb();
    (void)lim.effectiveThreshDb();
    (void)lim.latencySamples();
  }
  g_countAllocs = false;
  ASSERT_TRUE(g_allocs == 0);
}

} // namespace

int main() {
  RUN_TEST(testNeverAboveCeiling);
  RUN_TEST(testNeverAboveCeilingOnClicks);
  RUN_TEST(testStyleAndSetChangesKeepCeiling);
  RUN_TEST(testLatencyConstantAcrossStyles);
  RUN_TEST(testLatencyMultipleOfOversampling);
  RUN_TEST(testReleaseTrim);
  RUN_TEST(testDynReleaseFollowsProgram);
  RUN_TEST(testPunchLetsTransientThrough);
  RUN_TEST(testSetForcesThreshold);
  RUN_TEST(testSetSqueezesAudibly);
  RUN_TEST(testSetKeepsLoudness);
  RUN_TEST(testOffIsDelayedIdentity);
  RUN_TEST(testGainRecoveryLikeL2);
  RUN_TEST(testStereoLinked);
  RUN_TEST(testDynSoftKnee);
  RUN_TEST(testGrDbReporting);
  RUN_TEST(testSilenceStaysSilent);
  RUN_TEST(testWorksAtOversampledRate);
  RUN_TEST(testNoAllocationInProcess);
  std::printf("%d passed, %d failed\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
