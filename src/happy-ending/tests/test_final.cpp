/**
 * Fairy Tapes · Happy Ending — Unit tests for FINAL (INFLATE / CLIP) and METALHEAD (brickwall).
 * Run via CTest; build target: happy_ending_final_tests.
 */

#include "dsp/final_stage.h"
#include "dsp/clip_only2.h"
#include "dsp/metalhead.h"
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
using FairyTapes::HappyEnding::DSP::ClipOnly2;
using FairyTapes::HappyEnding::DSP::FinalStage;
using FairyTapes::HappyEnding::DSP::Metalhead;

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

// White noise at full scale `peak`: the hardest case for a clipper (every sample is a candidate).
void whitePair(std::vector<float>& l, std::vector<float>& r, int n, double peak, std::uint32_t seed) {
  l.assign(static_cast<std::size_t>(n), 0.0f);
  r.assign(static_cast<std::size_t>(n), 0.0f);
  Rng a(seed), b(seed + 99u);
  for (int i = 0; i < n; ++i) {
    l[static_cast<std::size_t>(i)] = static_cast<float>(peak * a.white());
    r[static_cast<std::size_t>(i)] = static_cast<float>(peak * b.white());
  }
}

double peakOf(const std::vector<float>& x, std::size_t from = 0) {
  double m = 0.0;
  for (std::size_t i = from; i < x.size(); ++i) m = std::max(m, static_cast<double>(std::fabs(x[i])));
  return m;
}

double rmsOf(const std::vector<float>& x, std::size_t from, std::size_t to) {
  double s = 0.0;
  for (std::size_t i = from; i < to; ++i) s += static_cast<double>(x[i]) * x[i];
  return std::sqrt(s / static_cast<double>(to - from));
}

std::vector<float> sineVec(int n, double freq, double amp, double sr = kSr) {
  std::vector<float> x(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) x[static_cast<std::size_t>(i)] = static_cast<float>(amp * std::sin(2.0 * kPi * freq * i / sr));
  return x;
}

template <class Stage>
void run(Stage& st, std::vector<float>& l, std::vector<float>& r, int block = 64) {
  const int n = static_cast<int>(l.size());
  for (int i = 0; i < n; i += block) st.process(l.data() + i, r.data() + i, std::min(block, n - i));
}

constexpr int kInflate = FinalStage::kInflate;
constexpr int kClip = FinalStage::kClip;

// ---------------------------------------------------------------------------------------------
// ClipOnly2 (the port)

void testClipOnly2StaysUnderItsKnee() {
  // the source clips softly around -0.4 dB; whatever the input (even +12 dB) it never goes above
  ClipOnly2 c;
  Rng rng(77);
  double m = 0.0;
  for (int i = 0; i < 400000; ++i) {
    const double x = 4.0 * rng.white() * (i % 3000 < 1500 ? 1.0 : 0.3);
    m = std::max(m, std::fabs(c.process(0, x)));
    m = std::max(m, std::fabs(c.process(1, -x * 0.9)));
  }
  ASSERT_TRUE(m <= ClipOnly2::kKnee + 1e-9);
  ASSERT_TRUE(m > 0.9);
}

void testClipOnly2IsOneSampleDelayBelowKnee() {
  ClipOnly2 c;
  double prev = 0.0;
  for (int i = 0; i < 2000; ++i) {
    const double x = 0.9 * std::sin(2.0 * kPi * 313.0 * i / kSr);
    const double y = c.process(0, x);
    ASSERT_TRUE(y == prev);
    prev = x;
  }
}

// ---------------------------------------------------------------------------------------------
// FINAL

void testInflateEffectZeroIsIdentity() {
  std::vector<float> pl, pr;
  pinkPair(pl, pr, 20000, 0.9, 11u);  // under the -0.3 dB ceiling
  for (float curve : {0.0f, 0.5f, 1.0f}) {
    FinalStage f;
    f.prepare(kSr);
    f.set(true, kInflate, 0.0f, curve, 2.0f, -0.3f);
    std::vector<float> l = pl, r = pr;
    run(f, l, r);
    const int d = f.latencySamples();
    for (int i = 0; i < 20000; ++i) {
      const float wl = i < d ? 0.0f : pl[static_cast<std::size_t>(i - d)];
      const float wr = i < d ? 0.0f : pr[static_cast<std::size_t>(i - d)];
      ASSERT_TRUE(l[static_cast<std::size_t>(i)] == wl);
      ASSERT_TRUE(r[static_cast<std::size_t>(i)] == wr);
    }
  }
}

void testInflateAddsLevelToTheBody() {
  // EFFECT lifts the mid level (the quadratic / quartic curves bend upward), the peak stays put
  std::vector<float> l = sineVec(24000, 440.0, 0.3), r = l;
  FinalStage f;
  f.prepare(kSr);
  f.set(true, kInflate, 1.0f, 0.25f, 0.0f, -0.3f);
  run(f, l, r);
  ASSERT_TRUE(rmsOf(l, 12000, 24000) > rmsOf(sineVec(24000, 440.0, 0.3), 12000, 24000) * 1.15);
  std::vector<float> hl = sineVec(24000, 440.0, 0.96), hr = hl;  // peak at the ceiling stays there
  FinalStage g;
  g.prepare(kSr);
  g.set(true, kInflate, 1.0f, 0.25f, 0.0f, -0.3f);
  run(g, hl, hr);
  ASSERT_TRUE(peakOf(hl, 12000) <= ampOfDb(-0.3));
  ASSERT_TRUE(peakOf(hl, 12000) > 0.95);
}

void testInflateNeverAboveCeiling() {
  const float ceilings[4] = {-0.1f, -0.3f, -0.5f, -1.0f};
  for (double peak : {1.0, 2.0}) {
    std::vector<float> pl, pr, wl, wr;
    pinkPair(pl, pr, 24000, peak, 21u);
    whitePair(wl, wr, 24000, peak, 22u);
    for (float ceil : ceilings)
      for (float curve : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
        for (int src = 0; src < 4; ++src) {
          FinalStage f;
          f.prepare(kSr);
          f.set(true, kInflate, src < 2 ? 1.0f : 0.4f, curve, 2.0f, ceil);  // EFFECT 100 % and a partial one
          std::vector<float> l = src % 2 ? wl : pl, r = src % 2 ? wr : pr;
          run(f, l, r);
          ASSERT_TRUE(peakOf(l) <= ampOfDb(ceil));
          ASSERT_TRUE(peakOf(r) <= ampOfDb(ceil));
        }
  }
}

void testClipModeNeverAboveCeiling() {
  const float ceilings[4] = {-0.1f, -0.3f, -0.5f, -1.0f};
  for (double peak : {1.0, 2.0, 4.0}) {
    std::vector<float> pl, pr, wl, wr;
    pinkPair(pl, pr, 24000, peak, 31u);
    whitePair(wl, wr, 24000, peak, 32u);
    for (float ceil : ceilings)
      for (float drive : {0.0f, 1.0f, 2.0f})
        for (int src = 0; src < 2; ++src) {
          FinalStage f;
          f.prepare(kSr);
          f.set(true, kClip, 0.5f, 0.5f, drive, ceil);
          std::vector<float> l = src ? wl : pl, r = src ? wr : pr;
          run(f, l, r);
          ASSERT_TRUE(peakOf(l) <= ampOfDb(ceil));
          ASSERT_TRUE(peakOf(r) <= ampOfDb(ceil));
        }
  }
}

void testClipModeDriveAndCeiling() {
  // quiet material passes (delayed) at drive 0; drive +2 dB lifts it by 2 dB; a hot sine is rounded
  // at the ceiling but keeps its level (it does not collapse)
  FinalStage f;
  f.prepare(kSr);
  f.set(true, kClip, 0.5f, 0.5f, 0.0f, -0.3f);
  std::vector<float> in = sineVec(24000, 440.0, 0.4), l = in, r = in;
  run(f, l, r);
  const int d = f.latencySamples();
  for (int i = d; i < 24000; ++i) ASSERT_NEAR_EPS(l[static_cast<std::size_t>(i)], in[static_cast<std::size_t>(i - d)], 1e-6);

  FinalStage g;
  g.prepare(kSr);
  g.set(true, kClip, 0.5f, 0.5f, 2.0f, -0.3f);
  l = in;
  r = in;
  run(g, l, r);
  ASSERT_NEAR_EPS(dbOf(rmsOf(l, 12000, 24000)) - dbOf(rmsOf(in, 12000, 24000)), 2.0, 0.05);

  FinalStage h;
  h.prepare(kSr);
  h.set(true, kClip, 0.5f, 0.5f, 2.0f, -0.3f);
  l = sineVec(24000, 440.0, 1.5);
  r = l;
  run(h, l, r);
  ASSERT_TRUE(peakOf(l, 12000) <= ampOfDb(-0.3));
  ASSERT_TRUE(peakOf(l, 12000) > 0.9 * ampOfDb(-0.3));
}

void testModesKeepTheirValues() {
  // the stage remembers both sets: EFFECT / CURVE do nothing in CLIP and DRIVE nothing in INFLATE,
  // and going INFLATE -> CLIP -> INFLATE gives back exactly the first sound
  std::vector<float> pl, pr;
  pinkPair(pl, pr, 24000, 1.5, 41u);

  auto render = [&](int mode, float eff, float crv, float drv) {
    FinalStage f;
    f.prepare(kSr);
    f.set(true, mode, eff, crv, drv, -0.3f);
    std::vector<float> l = pl, r = pr;
    run(f, l, r);
    return l;
  };
  ASSERT_TRUE(render(kClip, 0.2f, 0.0f, 1.0f) == render(kClip, 0.9f, 1.0f, 1.0f));
  ASSERT_TRUE(render(kInflate, 0.6f, 0.5f, 0.0f) == render(kInflate, 0.6f, 0.5f, 2.0f));
  ASSERT_TRUE(render(kInflate, 0.6f, 0.5f, 0.0f) != render(kInflate, 0.9f, 0.5f, 0.0f));
  ASSERT_TRUE(render(kClip, 0.6f, 0.5f, 0.0f) != render(kClip, 0.6f, 0.5f, 2.0f));

  FinalStage f;
  f.prepare(kSr);
  f.set(true, kInflate, 0.7f, 0.5f, 1.5f, -0.3f);
  f.set(true, kClip, 0.7f, 0.5f, 1.5f, -0.3f);
  ASSERT_TRUE(f.mode() == kClip && f.effect() == 0.7f && f.curve() == 0.5f && f.driveDb() == 1.5f);
  f.set(true, kInflate, 0.7f, 0.5f, 1.5f, -0.3f);
  ASSERT_TRUE(f.mode() == kInflate && f.effect() == 0.7f && f.curve() == 0.5f && f.driveDb() == 1.5f);
  std::vector<float> l = pl, r = pr;
  run(f, l, r);
  ASSERT_TRUE(l == render(kInflate, 0.7f, 0.5f, 1.5f));
}

void testFinalOffIsDelayedIdentity() {
  std::vector<float> pl, pr;
  pinkPair(pl, pr, 20000, 1.8, 51u);  // even above the ceiling: off means off
  for (int mode : {kInflate, kClip}) {
    FinalStage f;
    f.prepare(kSr);
    f.set(false, mode, 1.0f, 0.5f, 2.0f, -0.3f);
    std::vector<float> l = pl, r = pr;
    run(f, l, r);
    const int d = f.latencySamples();
    for (int i = 0; i < 20000; ++i) {
      const float wl = i < d ? 0.0f : pl[static_cast<std::size_t>(i - d)];
      ASSERT_TRUE(l[static_cast<std::size_t>(i)] == wl);
    }
  }
  // a working stage switched off settles to the same exact identity
  FinalStage g;
  g.prepare(kSr);
  g.set(true, kClip, 0.5f, 0.5f, 2.0f, -0.3f);
  std::vector<float> l = pl, r = pr;
  const int blocks = 10000 / 64, total = 20000 / 64;
  for (int b = 0; b < blocks; ++b) g.process(l.data() + b * 64, r.data() + b * 64, 64);
  g.set(false, kClip, 0.5f, 0.5f, 2.0f, -0.3f);
  for (int b = blocks; b < total; ++b) g.process(l.data() + b * 64, r.data() + b * 64, 64);
  const int d = g.latencySamples();
  for (int i = blocks * 64 + static_cast<int>(0.05 * kSr); i < total * 64; ++i)
    ASSERT_TRUE(l[static_cast<std::size_t>(i)] == pl[static_cast<std::size_t>(i - d)]);
}

void testFinalLatencyConstantAndAligned() {
  for (double sr : {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0})
    for (int mult : {1, 2, 4}) {
      FinalStage f;
      f.prepare(sr, mult);
      const int lat = f.latencySamples();
      ASSERT_TRUE(lat >= 1 && lat % mult == 0);
      for (int mode : {kInflate, kClip})
        for (bool on : {true, false}) {
          f.set(on, mode, 0.5f, 0.5f, 1.0f, -0.3f);
          ASSERT_TRUE(f.latencySamples() == lat);
        }
      // both modes put an in-ceiling click on the same output sample (CLIP and INFLATE are aligned)
      for (int mode : {kInflate, kClip}) {
        FinalStage g;
        g.prepare(sr, mult);
        g.set(true, mode, 0.0f, 0.5f, 0.0f, -0.3f);
        std::vector<float> l(400, 0.0f), r(400, 0.0f);
        l[100] = 0.5f;
        r[150] = -0.25f;
        run(g, l, r);
        ASSERT_NEAR_EPS(l[static_cast<std::size_t>(100 + lat)], 0.5, 1e-6);
        ASSERT_NEAR_EPS(r[static_cast<std::size_t>(150 + lat)], -0.25, 1e-6);
      }
    }
}

void testFinalWorksAtOversampledRates() {
  for (int mult : {2, 4}) {
    const double sr = kSr * mult;
    std::vector<float> pl, pr;
    pinkPair(pl, pr, 24000 * mult, 2.0, 61u);
    for (int mode : {kInflate, kClip}) {
      FinalStage f;
      f.prepare(sr, mult);
      f.set(true, mode, 1.0f, 0.75f, 2.0f, -1.0f);
      std::vector<float> l = pl, r = pr;
      run(f, l, r);
      ASSERT_TRUE(peakOf(l) <= ampOfDb(-1.0));
      ASSERT_TRUE(peakOf(r) <= ampOfDb(-1.0));
      ASSERT_TRUE(peakOf(l, 4000) > 0.8 * ampOfDb(-1.0));
    }
  }
}

void testFinalChangesKeepCeiling() {
  // mode, EFFECT, CURVE, DRIVE and CEILING changed at random block by block, always on: the
  // output stays under the ceiling that is in force, also through the crossfade between modes
  std::vector<float> l, r;
  pinkPair(l, r, 64 * 600, 2.0, 71u);
  Rng rng(5);
  FinalStage f;
  f.prepare(kSr);
  const float ceilings[4] = {-0.1f, -0.3f, -0.5f, -1.0f};
  double worst = -1.0;
  for (int b = 0; b < 600; ++b) {
    const float ceil = ceilings[static_cast<int>((rng.white() + 1.0) * 1.99)];
    f.set(true, rng.white() > 0.0 ? kClip : kInflate, static_cast<float>((rng.white() + 1.0) * 0.5),
          static_cast<float>((rng.white() + 1.0) * 0.5), static_cast<float>(rng.white() + 1.0), ceil);
    f.process(l.data() + b * 64, r.data() + b * 64, 64);
    for (int i = b * 64; i < b * 64 + 64; ++i) {
      const auto k = static_cast<std::size_t>(i);
      worst = std::max(worst, std::max(std::fabs(static_cast<double>(l[k])), std::fabs(static_cast<double>(r[k]))) - ampOfDb(ceil));
    }
  }
  ASSERT_TRUE(worst <= 0.0);
}

void testFinalShavedDb() {
  std::vector<float> pl, pr;
  pinkPair(pl, pr, 8192, 2.0, 81u);
  FinalStage f;
  f.prepare(kSr);
  f.set(true, kClip, 0.5f, 0.5f, 2.0f, -0.3f);
  float maxShaved = 0.0f;
  for (int i = 0; i + 64 <= 8192; i += 64) {
    f.process(pl.data() + i, pr.data() + i, 64);
    maxShaved = std::max(maxShaved, f.shavedDb());
  }
  ASSERT_TRUE(maxShaved > 3.0f);
  std::vector<float> ql = sineVec(2048, 440.0, 0.2), qr = ql;
  FinalStage g;
  g.prepare(kSr);
  g.set(true, kClip, 0.5f, 0.5f, 0.0f, -0.3f);
  for (int i = 0; i + 64 <= 2048; i += 64) g.process(ql.data() + i, qr.data() + i, 64);
  ASSERT_TRUE(g.shavedDb() < 0.01f);
  FinalStage h;
  h.prepare(kSr);
  h.set(true, kInflate, 1.0f, 0.5f, 0.0f, -0.3f);
  std::vector<float> il = sineVec(2048, 440.0, 0.2), ir = il;
  for (int i = 0; i + 64 <= 2048; i += 64) h.process(il.data() + i, ir.data() + i, 64);
  ASSERT_TRUE(h.shavedDb() < 0.01f);
}

void testFinalSilenceAndNonFinite() {
  FinalStage f;
  f.prepare(kSr);
  f.set(true, kInflate, 1.0f, 0.5f, 2.0f, -0.3f);
  std::vector<float> l(2048, 0.0f), r(2048, 0.0f);
  run(f, l, r);
  ASSERT_TRUE(peakOf(l) == 0.0 && peakOf(r) == 0.0);
  ASSERT_TRUE(f.shavedDb() == 0.0f);
  for (int mode : {kInflate, kClip}) {
    FinalStage g;
    g.prepare(kSr);
    g.set(true, mode, 1.0f, 0.5f, 2.0f, -0.3f);
    std::vector<float> a(256, 0.5f), b(256, 0.5f);
    a[10] = INFINITY;
    b[20] = NAN;
    run(g, a, b);
    for (std::size_t i = 0; i < 256; ++i) ASSERT_TRUE(std::isfinite(a[i]) && std::isfinite(b[i]) && std::fabs(a[i]) <= 1.0f);
  }
}

void testFinalNoAllocationInProcess() {
  FinalStage f;
  f.prepare(kSr);
  std::vector<float> pl, pr;
  pinkPair(pl, pr, 512, 2.0, 91u);
  g_allocs = 0;
  g_countAllocs = true;
  for (int i = 0; i < 40; ++i) {
    f.set(i % 7 != 0, i % 2, 0.1f * static_cast<float>(i % 11), 0.25f * static_cast<float>(i % 5), 0.5f * static_cast<float>(i % 5), -0.3f);
    f.process(pl.data(), pr.data(), 512);
    (void)f.shavedDb();
    (void)f.latencySamples();
  }
  g_countAllocs = false;
  ASSERT_TRUE(g_allocs == 0);
}

// ---------------------------------------------------------------------------------------------
// METALHEAD

void testMetalheadOffIsDelayedIdentity() {
  std::vector<float> pl, pr;
  pinkPair(pl, pr, 20000, 1.8, 101u);
  Metalhead m;
  m.prepare(kSr, 64);
  m.set(false, 3.0f, -0.3f);
  std::vector<float> l = pl, r = pr;
  run(m, l, r);
  const int d = m.latencySamples();
  ASSERT_TRUE(d >= 1);
  for (int i = 0; i < 20000; ++i) {
    const float wl = i < d ? 0.0f : pl[static_cast<std::size_t>(i - d)];
    const float wr = i < d ? 0.0f : pr[static_cast<std::size_t>(i - d)];
    ASSERT_TRUE(l[static_cast<std::size_t>(i)] == wl);
    ASSERT_TRUE(r[static_cast<std::size_t>(i)] == wr);
  }
  // the latency is the same switched on
  m.set(true, 3.0f, -0.3f);
  ASSERT_TRUE(m.latencySamples() == d);
  Metalhead on;
  on.prepare(kSr, 64);
  on.set(true, 3.0f, -0.3f);
  ASSERT_TRUE(on.latencySamples() == d);
  // after switching a working one off it settles to the same exact identity
  Metalhead g;
  g.prepare(kSr, 64);
  g.set(true, 6.0f, -0.3f);
  l = pl;
  r = pr;
  const int blocks = 10000 / 64, total = 20000 / 64;
  for (int b = 0; b < blocks; ++b) g.process(l.data() + b * 64, r.data() + b * 64, 64);
  g.set(false, 6.0f, -0.3f);
  for (int b = blocks; b < total; ++b) g.process(l.data() + b * 64, r.data() + b * 64, 64);
  for (int i = blocks * 64 + static_cast<int>(0.05 * kSr); i < total * 64; ++i)
    ASSERT_TRUE(l[static_cast<std::size_t>(i)] == pl[static_cast<std::size_t>(i - d)]);
}

void testMetalheadNeverAboveCeiling() {
  const float ceilings[4] = {-0.1f, -0.3f, -0.5f, -1.0f};
  for (double peak : {1.0, 2.0, 4.0}) {
    std::vector<float> pl, pr, wl, wr;
    pinkPair(pl, pr, 24000, peak, 111u);
    whitePair(wl, wr, 24000, peak, 112u);
    for (float ceil : ceilings)
      for (float push : {0.0f, 3.0f, 6.0f})
        for (int src = 0; src < 2; ++src) {
          Metalhead m;
          m.prepare(kSr, 64);
          m.set(true, push, ceil);
          std::vector<float> l = src ? wl : pl, r = src ? wr : pr;
          run(m, l, r);
          ASSERT_TRUE(peakOf(l) <= ampOfDb(ceil));
          ASSERT_TRUE(peakOf(r) <= ampOfDb(ceil));
        }
  }
  // PUSH, ceiling and on/off moved at random block by block
  std::vector<float> pl, pr;
  pinkPair(pl, pr, 64 * 600, 3.0, 113u);
  Rng rng(9);
  Metalhead m;
  m.prepare(kSr, 64);
  double worst = -1.0;
  for (int b = 0; b < 600; ++b) {
    const float ceil = ceilings[static_cast<int>((rng.white() + 1.0) * 1.99)];
    m.set(true, static_cast<float>((rng.white() + 1.0) * 3.0), ceil);
    m.process(pl.data() + b * 64, pr.data() + b * 64, 64);
    for (int i = b * 64; i < b * 64 + 64; ++i)
      worst = std::max(worst, std::fabs(static_cast<double>(pl[static_cast<std::size_t>(i)])) - ampOfDb(ceil));
  }
  ASSERT_TRUE(worst <= 0.0);
}

void testMetalheadPushLiftsTheProgram() {
  // push 0 is transparent below the ceiling; push 6 lifts the level of a quiet program by 6 dB
  std::vector<float> pl, pr;
  pinkPair(pl, pr, 48000, 0.05, 121u);  // far from any limiting
  for (float push : {0.0f, 3.0f, 6.0f}) {
    Metalhead m;
    m.prepare(kSr, 64);
    m.set(true, push, -0.3f);
    std::vector<float> l = pl, r = pr;
    run(m, l, r);
    const int d = m.latencySamples();
    for (int i = 24000; i < 48000; ++i)
      ASSERT_NEAR_EPS(l[static_cast<std::size_t>(i)], pl[static_cast<std::size_t>(i - d)] * ampOfDb(push), 2e-6);
  }
}

void testMetalheadPushAgainstTheCeiling() {
  // a program at the ceiling, pushed 6 dB: the peaks stay at the ceiling, the body goes up
  std::vector<float> pl, pr;
  pinkPair(pl, pr, 96000, ampOfDb(-0.3), 131u);
  Metalhead m;
  m.prepare(kSr, 64);
  m.set(true, 6.0f, -0.3f);
  std::vector<float> l = pl, r = pr;
  run(m, l, r);
  ASSERT_TRUE(peakOf(l, 48000) <= ampOfDb(-0.3));
  ASSERT_TRUE(peakOf(l, 48000) > ampOfDb(-0.3) - 0.02);
  const double gain = dbOf(rmsOf(l, 48000, 96000)) - dbOf(rmsOf(pl, 48000, 96000));
  ASSERT_TRUE(gain > 2.0 && gain < 6.0);
  ASSERT_TRUE(m.grDb() > 0.5f);
}

void testMetalheadLookAheadAndRelease() {
  // a constant program with one loud sample: the gain starts coming down about 1 ms before the
  // loud sample appears at the output, and recovers with a time constant of about 30 ms
  const int n = static_cast<int>(1.0 * kSr);
  const int click = 12000;
  std::vector<float> l(static_cast<std::size_t>(n), 0.1f), r = l;
  l[click] = 2.0f;
  Metalhead m;
  m.prepare(kSr, 64);
  m.set(true, 6.0f, -0.3f);
  const std::vector<float> in = l;
  run(m, l, r);
  const int d = m.latencySamples();
  const double settled = 0.1 * ampOfDb(6.0);  // the program level before any reduction
  int start = -1;
  for (int j = click - 200; j < click + d; ++j)
    if (l[static_cast<std::size_t>(j)] < settled * 0.99) { start = j; break; }
  ASSERT_TRUE(start > 0);
  const double leadMs = (click + d - start) * 1000.0 / kSr;
  ASSERT_TRUE(leadMs > 0.8 && leadMs < 1.1);
  // release: the deficit of the gain goes down by 1/e in about 30 ms
  double minOut = 1e9;
  int minAt = 0;
  for (int j = click + d; j < click + d + 400; ++j)
    if (l[static_cast<std::size_t>(j)] < minOut) { minOut = l[static_cast<std::size_t>(j)]; minAt = j; }
  const double deficit0 = settled - minOut;
  ASSERT_TRUE(deficit0 > 0.01);
  const int t30 = minAt + static_cast<int>(0.030 * kSr);
  const double deficit30 = settled - l[static_cast<std::size_t>(t30)];
  ASSERT_NEAR_EPS(deficit30 / deficit0, std::exp(-1.0), 0.07);
}

void testMetalheadLatencyMultipleOfOversampling() {
  for (double sr : {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0})
    for (int mult : {1, 2, 4}) {
      Metalhead m;
      m.prepare(sr, 128, mult);
      const int lat = m.latencySamples();
      ASSERT_TRUE(lat % mult == 0);
      ASSERT_TRUE(lat >= static_cast<int>(std::ceil(Tuning::kMetalLookAheadMs * 0.001 * sr)));
      m.set(true, 6.0f, -0.3f);
      ASSERT_TRUE(m.latencySamples() == lat);
      m.set(false, 0.0f, -0.3f);
      ASSERT_TRUE(m.latencySamples() == lat);
    }
}

void testMetalheadWorksAtOversampledRate() {
  for (int mult : {2, 4}) {
    const double sr = kSr * mult;
    std::vector<float> pl, pr;
    pinkPair(pl, pr, 24000 * mult, 2.0, 141u);
    Metalhead m;
    m.prepare(sr, 64, mult);
    m.set(true, 6.0f, -1.0f);
    run(m, pl, pr);
    ASSERT_TRUE(peakOf(pl) <= ampOfDb(-1.0));
    ASSERT_TRUE(peakOf(pr) <= ampOfDb(-1.0));
  }
}

void testMetalheadSilenceAndNonFinite() {
  Metalhead m;
  m.prepare(kSr, 64);
  m.set(true, 6.0f, -0.3f);
  std::vector<float> l(4096, 0.0f), r(4096, 0.0f);
  run(m, l, r);
  ASSERT_TRUE(peakOf(l) == 0.0 && peakOf(r) == 0.0);
  ASSERT_TRUE(m.grDb() < 1e-6f);
  std::vector<float> a(256, 0.1f), b(256, 0.1f);
  a[5] = INFINITY;
  b[9] = NAN;
  run(m, a, b);
  for (std::size_t i = 0; i < 256; ++i) ASSERT_TRUE(std::isfinite(a[i]) && std::isfinite(b[i]));
}

void testMetalheadNoAllocationInProcess() {
  Metalhead m;
  m.prepare(kSr, 512);
  std::vector<float> pl, pr;
  pinkPair(pl, pr, 512, 2.0, 151u);
  g_allocs = 0;
  g_countAllocs = true;
  for (int i = 0; i < 40; ++i) {
    m.set(i % 5 != 0, 0.5f * static_cast<float>(i % 13), -0.3f);
    m.process(pl.data(), pr.data(), 512);
    (void)m.grDb();
    (void)m.latencySamples();
  }
  g_countAllocs = false;
  ASSERT_TRUE(g_allocs == 0);
}

} // namespace

int main() {
  RUN_TEST(testClipOnly2StaysUnderItsKnee);
  RUN_TEST(testClipOnly2IsOneSampleDelayBelowKnee);
  RUN_TEST(testInflateEffectZeroIsIdentity);
  RUN_TEST(testInflateAddsLevelToTheBody);
  RUN_TEST(testInflateNeverAboveCeiling);
  RUN_TEST(testClipModeNeverAboveCeiling);
  RUN_TEST(testClipModeDriveAndCeiling);
  RUN_TEST(testModesKeepTheirValues);
  RUN_TEST(testFinalOffIsDelayedIdentity);
  RUN_TEST(testFinalLatencyConstantAndAligned);
  RUN_TEST(testFinalWorksAtOversampledRates);
  RUN_TEST(testFinalChangesKeepCeiling);
  RUN_TEST(testFinalShavedDb);
  RUN_TEST(testFinalSilenceAndNonFinite);
  RUN_TEST(testFinalNoAllocationInProcess);
  RUN_TEST(testMetalheadOffIsDelayedIdentity);
  RUN_TEST(testMetalheadNeverAboveCeiling);
  RUN_TEST(testMetalheadPushLiftsTheProgram);
  RUN_TEST(testMetalheadPushAgainstTheCeiling);
  RUN_TEST(testMetalheadLookAheadAndRelease);
  RUN_TEST(testMetalheadLatencyMultipleOfOversampling);
  RUN_TEST(testMetalheadWorksAtOversampledRate);
  RUN_TEST(testMetalheadSilenceAndNonFinite);
  RUN_TEST(testMetalheadNoAllocationInProcess);
  std::printf("%d passed, %d failed\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
