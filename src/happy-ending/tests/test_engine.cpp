/**
 * Fairy Tapes · Happy Ending — Unit tests for the Engine (the whole chain, meters, bypass, latency).
 * Run via CTest; build target: happy_ending_engine_tests.
 */

#include "engine.h"
#include "loudness_macro.h"
#include "params.h"
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
namespace DSP = FairyTapes::HappyEnding::DSP;

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
    std::fprintf(stderr, "  FAIL [%s:%d]: %s ~= %s (got %.9f vs %.9f, eps %g)\n", \
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

struct Rng {
  std::uint32_t s;
  explicit Rng(std::uint32_t seed) : s(seed) {}
  double white() {  // [-1, 1)
    s = s * 1664525u + 1013904223u;
    return static_cast<double>(s >> 8) / 8388608.0 - 1.0;
  }
  int below(int n) { return static_cast<int>((white() + 1.0) * 0.5 * n) % n; }
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

std::vector<float> sineVec(int n, double freq, double amp, int phase0 = 0) {
  std::vector<float> x(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i)
    x[static_cast<std::size_t>(i)] = static_cast<float>(amp * std::sin(2.0 * kPi * freq * (i + phase0) / kSr));
  return x;
}

// All parameters at their defaults, normalized.
std::vector<double> defaultNorm() {
  std::vector<double> norm(kNumParams);
  for (int id = 0; id < kNumParams; ++id)
    norm[static_cast<std::size_t>(id)] = normFromIndex(static_cast<ParamID>(id), kParams[static_cast<std::size_t>(id)].defaultIndex);
  return norm;
}

void setIdx(std::vector<double>& norm, ParamID id, int index) {
  norm[static_cast<std::size_t>(id)] = normFromIndex(id, index);
}

EngineSettings defaults() { return mapParams(defaultNorm().data(), -1, false); }

// Every stage off: the chain is pure delay (the oversamplers' filters aside).
EngineSettings allOff() {
  EngineSettings s = defaults();
  s.fineOn = false;
  s.vibe.on = false;
  s.glue.on = false;
  s.clipOn = false;
  s.limitOn = false;
  s.finalOn = false;
  s.metalOn = false;
  return s;
}

// Runs l / r through the engine in blocks of `block` samples (in place).
void run(Engine& e, std::vector<float>& l, std::vector<float>& r, int block) {
  const int n = static_cast<int>(l.size());
  for (int pos = 0; pos < n; pos += block) {
    const int len = std::min(block, n - pos);
    e.process(l.data() + pos, r.data() + pos, len);
  }
}

// ---------------------------------------------------------------------------------------------
// Reference: the blocks wired by hand in the order of the plan, with explicit stage values (what
// the stages hear with the LOUDNESS push already added). Same calls, same order as the engine.
// ---------------------------------------------------------------------------------------------
struct RefChain {
  DSP::FineEq fine;
  DSP::VibeEq vibe;
  DSP::Glue glue;
  DSP::ClipSoftly clip;
  DSP::Limiter lim;
  DSP::FinalStage fin;
  DSP::Metalhead metal;
  DSP::Oversampler a[2], b[2];
  std::vector<float> osL, osR;
  int f = 1;
  float ceil = 1.0f;

  void prepare(int maxBlock, int factor, const EngineSettings& s, float clipDb, float threshDb, float effect,
               float driveDb) {
    f = factor;
    const double osRate = kSr * f;
    fine.prepare(kSr, maxBlock);
    vibe.prepare(kSr, maxBlock);
    vibe.prepareTube(osRate);
    glue.prepare(kSr, maxBlock);
    clip.prepare(osRate);
    lim.prepare(osRate, maxBlock * f, f);
    fin.prepare(osRate, f);
    metal.prepare(osRate, maxBlock * f, f);
    for (int c = 0; c < 2; ++c) {
      a[c].prepare(f, maxBlock);
      b[c].prepare(f, maxBlock);
    }
    osL.assign(static_cast<std::size_t>(maxBlock * f), 0.0f);
    osR.assign(static_cast<std::size_t>(maxBlock * f), 0.0f);

    fine.setStageOn(s.fineOn);
    for (int band = 0; band < DSP::FineEq::kNumBands; ++band) fine.setBand(band, s.bands[static_cast<std::size_t>(band)]);
    fine.setListen(s.listenBand);
    vibe.set(s.vibe);
    glue.set(s.glue);
    clip.setDriveDb(clipDb);
    DSP::LimiterSettings ls;
    ls.on = s.limitOn;
    ls.threshDb = threshDb;
    ls.style = s.limitStyle;
    ls.releaseTrimIndex = s.limitReleaseIndex;
    ls.ceilingDb = s.ceilingDb;
    ls.set = s.setActive;
    lim.set(ls);
    fin.set(s.finalOn, s.finalMode, effect, s.finalCurve, driveDb, s.ceilingDb);
    metal.set(s.metalOn, s.pushDb, s.ceilingDb);

    const double c = std::pow(10.0, static_cast<double>(std::min(s.ceilingDb, 0.0f)) / 20.0);
    ceil = static_cast<float>(c);
    if (static_cast<double>(ceil) > c) ceil = std::nextafter(ceil, 0.0f);
  }

  void process(float* l, float* r, int n) {
    fine.process(l, r, n);
    vibe.processLinear(l, r, n);
    if (f > 1) {
      a[0].up(l, n, osL.data());
      a[1].up(r, n, osR.data());
      vibe.processTube(osL.data(), osR.data(), n * f);
      a[0].down(osL.data(), n * f, l);
      a[1].down(osR.data(), n * f, r);
    } else {
      vibe.processTube(l, r, n);
    }
    glue.process(l, r, n);
    float* zl = l;
    float* zr = r;
    if (f > 1) {
      b[0].up(l, n, osL.data());
      b[1].up(r, n, osR.data());
      zl = osL.data();
      zr = osR.data();
    }
    clip.process(zl, zr, n * f);
    lim.process(zl, zr, n * f);
    fin.process(zl, zr, n * f);
    metal.process(zl, zr, n * f);
    if (f > 1) {
      b[0].down(osL.data(), n * f, l);
      b[1].down(osR.data(), n * f, r);
    }
    for (int i = 0; i < n; ++i) {
      l[i] = std::clamp(l[i], -ceil, ceil);
      r[i] = std::clamp(r[i], -ceil, ceil);
    }
  }
};

// Engine against the reference for the same settings (bit for bit).
bool matchesReference(const EngineSettings& s, int factor, float clipDb, float threshDb, float effect, float driveDb) {
  constexpr int kBlock = 256;
  Engine e;
  e.prepare(kSr, kBlock, factor);
  e.set(s);
  RefChain ref;
  ref.prepare(kBlock, factor, s, clipDb, threshDb, effect, driveDb);

  std::vector<float> l, r;
  pinkPair(l, r, static_cast<int>(kSr), 0.9, 4242u);
  std::vector<float> rl = l, rr = r;
  run(e, l, r, kBlock);
  {
    ScopedNoDenormals guard;
    const int n = static_cast<int>(rl.size());
    for (int pos = 0; pos < n; pos += kBlock) ref.process(rl.data() + pos, rr.data() + pos, std::min(kBlock, n - pos));
  }
  for (std::size_t i = 0; i < l.size(); ++i) {
    if (l[i] != rl[i] || r[i] != rr[i]) {
      std::fprintf(stderr, "  mismatch at %zu: %.9g / %.9g vs %.9g / %.9g\n", i, l[i], r[i], rl[i], rr[i]);
      return false;
    }
  }
  return true;
}

// ---------------------------------------------------------------------------------------------

void testMapParamsDefaults() {
  const EngineSettings s = defaults();
  ASSERT_TRUE(!s.bypass);
  ASSERT_TRUE(s.inputGainDb == 0.0f && s.outputGainDb == 0.0f);
  ASSERT_NEAR_EPS(s.ceilingDb, -0.3, 1e-6);
  ASSERT_TRUE(s.loudness == 0.0f);
  ASSERT_TRUE(s.targetLufs == -14.0f);
  ASSERT_TRUE(s.oversampling == 2);
  ASSERT_TRUE(s.fineOn);
  ASSERT_TRUE(s.bands[1].freqIndex == 3 && s.bands[1].gainIndex == 8);  // LOW SHELF 100 Hz, 0 dB
  ASSERT_TRUE(s.bands[3].freqIndex == 4 && s.bands[3].qIndex == 1);     // MID 1 kHz, Medium
  ASSERT_TRUE(s.listenBand == -1);
  ASSERT_TRUE(s.vibe.on && s.vibe.lowFreqIndex == 2 && s.vibe.tube == 2 && s.vibe.tiltIndex == 6);
  ASSERT_TRUE(s.glue.on && s.glue.threshDb == -10.0f && s.glue.attackMs == 3.0f && s.glue.releaseS == 0.0f);
  ASSERT_TRUE(s.glue.ratio == 2.0f && s.glue.mix == 0.5f && s.glue.scHpfHz == 60.0f && !s.glue.warm);
  ASSERT_TRUE(s.clipOn && s.clipDriveDb == 2.0f);
  ASSERT_TRUE(s.limitOn && s.limitThreshDb == -2.0f && s.limitStyle == 0 && s.limitReleaseIndex == 4);
  ASSERT_TRUE(!s.setActive);
  ASSERT_TRUE(s.finalOn && s.finalMode == 0 && s.finalEffect == 0.5f && s.finalCurve == 0.25f && s.finalDriveDb == 2.0f);
  ASSERT_TRUE(!s.metalOn && s.pushDb == 2.0f);

  auto norm = defaultNorm();
  setIdx(norm, kOversampling, 0);
  ASSERT_TRUE(mapParams(norm.data(), -1, false).oversampling == 1);
  setIdx(norm, kOversampling, 2);
  ASSERT_TRUE(mapParams(norm.data(), -1, true).oversampling == 4);
  ASSERT_TRUE(mapParams(norm.data(), -1, true).setActive);
}

// LISTEN is locked (off) when its band, or the FINE EQ stage, is off.
void testListenOffWhenBandOff() {
  auto norm = defaultNorm();
  ASSERT_TRUE(mapParams(norm.data(), 1, false).listenBand == 1);
  ASSERT_TRUE(mapParams(norm.data(), 5, false).listenBand == -1);
  setIdx(norm, kMidOn, 0);
  ASSERT_TRUE(mapParams(norm.data(), 1, false).listenBand == -1);
  ASSERT_TRUE(mapParams(norm.data(), 2, false).listenBand == 2);
  setIdx(norm, kFineOn, 0);
  ASSERT_TRUE(mapParams(norm.data(), 2, false).listenBand == -1);
}

void testBypassIsDelayedInput() {
  constexpr int kMax = 256;
  for (int factor : {1, 2, 4}) {
    Engine e;
    e.prepare(kSr, kMax, factor);
    EngineSettings s = defaults();
    s.bypass = true;
    s.metalOn = true;
    e.set(s);
    const int latency = e.latencySamples();
    ASSERT_TRUE(latency > 0);

    std::vector<float> l, r;
    pinkPair(l, r, static_cast<int>(4 * kSr), 0.9, 77u + static_cast<std::uint32_t>(factor));
    const std::vector<float> inL = l, inR = r;
    Rng rng(5u);
    const int n = static_cast<int>(l.size());
    for (int pos = 0; pos < n;) {
      const int len = std::min(1 + rng.below(3 * kMax), n - pos);  // also blocks longer than maxBlock
      e.process(l.data() + pos, r.data() + pos, len);
      pos += len;
    }
    for (int i = 0; i < n; ++i) {
      const float wantL = i >= latency ? inL[static_cast<std::size_t>(i - latency)] : 0.0f;
      const float wantR = i >= latency ? inR[static_cast<std::size_t>(i - latency)] : 0.0f;
      ASSERT_TRUE(l[static_cast<std::size_t>(i)] == wantL);
      ASSERT_TRUE(r[static_cast<std::size_t>(i)] == wantR);
    }
    // meters and LUFS stay live
    const Meters& m = e.meters();
    ASSERT_TRUE(m.inPeakDb[0] > -40.0f && m.outPeakDb[1] > -40.0f);
    ASSERT_TRUE(std::isfinite(m.lufsShort) && std::isfinite(m.lufsIntegrated));
  }
}

void testLatencyPerOversampling() {
  int lat[3];
  int k = 0;
  for (int factor : {1, 2, 4}) {
    Engine e;
    e.prepare(kSr, 512, factor);
    ASSERT_TRUE(e.oversampling() == factor);
    lat[k] = e.latencySamples();
    // constant across styles, modes and on / off
    EngineSettings s = defaults();
    for (int style = 0; style < 3; ++style)
      for (int mode = 0; mode < 2; ++mode)
        for (int on = 0; on < 2; ++on) {
          s.limitStyle = style;
          s.finalMode = mode;
          s.limitOn = s.finalOn = s.metalOn = s.clipOn = on == 1;
          s.setActive = on == 0;
          e.set(s);
          std::vector<float> l(512, 0.1f), r(512, 0.1f);
          e.process(l.data(), r.data(), 512);
          ASSERT_TRUE(e.latencySamples() == lat[k]);
        }

    // the reported latency is the real one: an impulse through the chain with every stage off
    // comes out (peak of the oversamplers' symmetric response) exactly latency samples later
    Engine z;
    z.prepare(kSr, 512, factor);
    z.set(allOff());
    std::vector<float> l(8192, 0.0f), r(8192, 0.0f);
    l[1000] = 0.5f;
    r[1000] = -0.5f;
    run(z, l, r, 512);
    std::size_t at = 0;
    for (std::size_t i = 1; i < l.size(); ++i)
      if (std::fabs(l[i]) > std::fabs(l[at])) at = i;
    ASSERT_TRUE(static_cast<int>(at) == 1000 + lat[k]);
    if (factor == 1) ASSERT_TRUE(l[at] == 0.5f && r[at] == -0.5f);
    ++k;
  }
  ASSERT_TRUE(lat[0] < lat[1]);
  ASSERT_TRUE(lat[1] < lat[2]);
}

// Review Focus: LOUDNESS 10, PUSH +6, EFFECT 100 %, CEIL -0.1, pink noise at 0 dBFS, OVERSAMPLING 1x / 2x / 4x.
void testAllMaxNeverAboveCeiling() {
  const double ceiling = std::pow(10.0, -0.1 / 20.0);
  std::vector<float> srcL, srcR;
  pinkPair(srcL, srcR, static_cast<int>(2 * kSr), 1.0, 2026u);
  for (int factor : {1, 2, 4})
    for (int mode = 0; mode < 2; ++mode)
      for (int inIdx : {24, 48}) {  // INPUT 0 dB, then +12 dB
        auto norm = defaultNorm();
        setIdx(norm, kCeiling, 0);
        setIdx(norm, kLoudness, 20);
        setIdx(norm, kMetalOn, 1);
        setIdx(norm, kPush, 12);
        setIdx(norm, kFinalEffect, 10);
        setIdx(norm, kFinalMode, mode);
        setIdx(norm, kInputGain, inIdx);
        Engine e;
        e.prepare(kSr, 512, factor);
        e.set(mapParams(norm.data(), -1, false));
        std::vector<float> l = srcL, r = srcR;
        run(e, l, r, 512);
        double peak = 0.0;
        for (std::size_t i = 0; i < l.size(); ++i)
          peak = std::max(peak, static_cast<double>(std::max(std::fabs(l[i]), std::fabs(r[i]))));
        if (!(peak <= ceiling))
          std::fprintf(stderr, "  factor %d mode %d input %d: peak %.9f > %.9f\n", factor, mode, inIdx, peak, ceiling);
        ASSERT_TRUE(peak <= ceiling);
        ASSERT_TRUE(peak > 0.5);
        ASSERT_TRUE(!e.meters().clip[kLedOut]);
      }

  // LIMIT, FINAL and METALHEAD in any combination (one of them on): still under the ceiling.
  for (int combo = 1; combo < 8; ++combo) {
    EngineSettings s = defaults();
    s.ceilingDb = -0.1f;
    s.inputGainDb = 12.0f;
    s.loudness = 10.0f;
    s.limitOn = (combo & 1) != 0;
    s.finalOn = (combo & 2) != 0;
    s.metalOn = (combo & 4) != 0;
    s.pushDb = 6.0f;
    Engine e;
    e.prepare(kSr, 512, 2);
    e.set(s);
    std::vector<float> l = srcL, r = srcR;
    run(e, l, r, 512);
    for (std::size_t i = 0; i < l.size(); ++i) {
      ASSERT_TRUE(std::fabs(l[i]) <= ceiling);
      ASSERT_TRUE(std::fabs(r[i]) <= ceiling);
    }
  }
}

// LOUDNESS 0 is bit for bit the chain of the stage values alone (no macro anywhere).
void testLoudnessZeroEqualsSettings() {
  for (int factor : {1, 2}) {
    EngineSettings s = defaults();
    s.metalOn = true;
    s.loudness = 0.0f;
    ASSERT_TRUE(matchesReference(s, factor, s.clipDriveDb, s.limitThreshDb, s.finalEffect, s.finalDriveDb));
  }
}

// LOUDNESS adds the push of loudPush on top of the stage values: CLIP drive + clipDb, LIMIT threshold
// - limitDb, FINAL EFFECT + 0.6 * final01 (capped at 100 %) or DRIVE + 2 dB * final01 (capped at +2 dB).
void testLoudnessPushesStages() {
  EngineSettings s = defaults();
  s.metalOn = true;
  s.loudness = 10.0f;
  s.finalEffect = 0.3f;
  const LoudPush p = loudPush(10.0f);
  ASSERT_TRUE(p.clipDb > 0.0f && p.limitDb > 0.0f && p.final01 > 0.0f);
  ASSERT_TRUE(matchesReference(s, 2, s.clipDriveDb + p.clipDb, s.limitThreshDb - p.limitDb,
                               std::min(1.0f, s.finalEffect + 0.6f * p.final01), std::min(2.0f, s.finalDriveDb + 2.0f * p.final01)));
  // FINAL in CLIP mode with room under +2 dB, half-way up the knob (only CLIP and LIMIT pushed so far)
  s.finalMode = 1;
  s.finalDriveDb = 0.5f;
  s.loudness = 9.0f;
  const LoudPush q = loudPush(9.0f);
  ASSERT_TRUE(matchesReference(s, 1, s.clipDriveDb + q.clipDb, s.limitThreshDb - q.limitDb,
                               std::min(1.0f, s.finalEffect + 0.6f * q.final01), std::min(2.0f, s.finalDriveDb + 2.0f * q.final01)));
}

// The stage controls never move: LOUDNESS only changes its own field, and the engine keeps them.
void testLoudnessDoesNotMoveParams() {
  auto norm = defaultNorm();
  const EngineSettings at0 = mapParams(norm.data(), -1, false);
  setIdx(norm, kLoudness, 20);
  const EngineSettings at10 = mapParams(norm.data(), -1, false);
  ASSERT_TRUE(at10.loudness == 10.0f);
  EngineSettings cmp = at10;
  cmp.loudness = at0.loudness;
  ASSERT_TRUE(cmp == at0);

  Engine e;
  e.prepare(kSr, 512, 2);
  e.set(at10);
  std::vector<float> l, r;
  pinkPair(l, r, 48000, 0.9, 9u);
  run(e, l, r, 512);
  ASSERT_TRUE(e.settings() == at10);
  ASSERT_TRUE(mapParams(norm.data(), -1, false) == at10);
  // the normalized stage values are untouched too
  ASSERT_TRUE(indexFromNorm(kClipDrive, norm[kClipDrive]) == kParams[kClipDrive].defaultIndex);
  ASSERT_TRUE(indexFromNorm(kLimitThresh, norm[kLimitThresh]) == kParams[kLimitThresh].defaultIndex);
  ASSERT_TRUE(indexFromNorm(kFinalEffect, norm[kFinalEffect]) == kParams[kFinalEffect].defaultIndex);
}

// Review Focus: 5 s of noise, then 10 s of zeros: the last 1000 samples are exactly 0, no denormal.
void testSilenceAfterTailIsZero() {
  for (int factor : {2, 4}) {
    EngineSettings s = defaults();
    s.metalOn = true;
    s.vibe.low = 6;
    s.vibe.air = 5;
    s.vibe.tube = 10;
    s.vibe.dynLow = s.vibe.dynAir = true;
    s.glue.warm = true;
    s.limitStyle = 2;  // DYN: the longest states
    s.loudness = 6.0f;
    Engine e;
    e.prepare(kSr, 512, factor);
    e.set(s);
    std::vector<float> l, r;
    pinkPair(l, r, static_cast<int>(5 * kSr), 0.9, 31u);
    l.resize(static_cast<std::size_t>(15 * kSr), 0.0f);
    r.resize(static_cast<std::size_t>(15 * kSr), 0.0f);
    run(e, l, r, 512);
    for (std::size_t i = 0; i < l.size(); ++i) {
      ASSERT_TRUE(std::fpclassify(l[i]) != FP_SUBNORMAL);
      ASSERT_TRUE(std::fpclassify(r[i]) != FP_SUBNORMAL);
    }
    for (std::size_t i = l.size() - 1000; i < l.size(); ++i) {
      if (l[i] != 0.0f || r[i] != 0.0f) std::fprintf(stderr, "  factor %d: [%zu] = %g / %g\n", factor, i, l[i], r[i]);
      ASSERT_TRUE(l[i] == 0.0f);
      ASSERT_TRUE(r[i] == 0.0f);
    }
  }
}

// INPUT +12 on a 0 dBFS sine lights IN and FEQ (the output stays under the ceiling); clearClips()
// turns them off; a LED is held 2 s and then goes off.
void testClipLedsPerStage() {
  Engine e;
  e.prepare(kSr, 512, 2);
  EngineSettings s = defaults();
  s.inputGainDb = 12.0f;
  e.set(s);
  std::vector<float> l = sineVec(24000, 1000.0, 1.0), r = l;
  run(e, l, r, 512);
  const Meters& m = e.meters();
  ASSERT_TRUE(m.clip[kLedIn]);
  ASSERT_TRUE(m.clip[kLedFeq]);
  ASSERT_TRUE(!m.clip[kLedOut]);
  ASSERT_TRUE(!m.clip[kLedLim] && !m.clip[kLedFin]);
  ASSERT_NEAR_EPS(m.inPeakDb[0], 12.0, 0.05);
  e.clearClips();
  for (int i = 0; i < kNumLeds; ++i) ASSERT_TRUE(!e.meters().clip[i]);

  // hold: lit by a short burst, still lit 1.9 s later, off after 2 s
  l = sineVec(4800, 1000.0, 1.0);
  r = l;
  run(e, l, r, 512);
  ASSERT_TRUE(e.meters().clip[kLedIn]);
  s.inputGainDb = 0.0f;
  e.set(s);
  l = sineVec(static_cast<int>(1.9 * kSr), 1000.0, 0.1);
  r = l;
  run(e, l, r, 480);
  ASSERT_TRUE(e.meters().clip[kLedIn]);
  l = sineVec(static_cast<int>(0.2 * kSr), 1000.0, 0.1);
  r = l;
  run(e, l, r, 480);
  ASSERT_TRUE(!e.meters().clip[kLedIn]);
  ASSERT_TRUE(!e.meters().clip[kLedFeq]);
}

void testNoAllocationInProcess() {
  Engine e;
  e.prepare(kSr, 512, 4);
  EngineSettings s = defaults();
  s.metalOn = true;
  e.set(s);
  std::vector<float> l, r;
  pinkPair(l, r, 48000, 1.0, 3u);
  std::vector<float> wl = l, wr = r;
  run(e, wl, wr, 512);  // warm up

  g_allocs = 0;
  g_countAllocs = true;
  for (int pass = 0; pass < 6; ++pass) {
    s.loudness = static_cast<float>(pass * 2);
    s.outputGainDb = pass % 2 ? -3.0f : 1.5f;
    s.limitStyle = pass % 3;
    s.finalMode = pass % 2;
    s.bypass = pass == 3;
    s.listenBand = pass == 4 ? 1 : -1;
    s.setActive = pass == 5;
    s.vibe.dynLow = pass % 2 == 0;
    s.glue.on = pass != 2;
    s.clipOn = pass != 1;
    e.set(s);
    for (int pos = 0; pos + 700 <= static_cast<int>(l.size()); pos += 700) e.process(l.data() + pos, r.data() + pos, 700);
    e.clearClips();
    e.resetIntegrated();
  }
  g_countAllocs = false;
  ASSERT_TRUE(g_allocs == 0);
}

// OUTPUT gain steps glide: no difference between consecutive samples above 0.05 on a sine at -6 dBFS.
void testOutputGainStepNoZipper() {
  Engine e;
  e.prepare(kSr, 512, 2);
  EngineSettings s = defaults();
  e.set(s);
  const int settle = static_cast<int>(kSr);
  std::vector<float> l = sineVec(settle, 100.0, 0.5), r = l;
  run(e, l, r, 512);
  const float lastBefore = l.back();
  double rmsBefore = 0.0;
  for (int i = settle - 4800; i < settle; ++i) rmsBefore += static_cast<double>(l[static_cast<std::size_t>(i)]) * l[static_cast<std::size_t>(i)];

  s.outputGainDb = 6.0f;
  e.set(s);
  const int after = static_cast<int>(0.5 * kSr);
  std::vector<float> l2 = sineVec(after, 100.0, 0.5, settle), r2 = l2;
  run(e, l2, r2, 512);
  double maxDiff = std::fabs(l2[0] - lastBefore);
  for (std::size_t i = 1; i < l2.size(); ++i) maxDiff = std::max(maxDiff, static_cast<double>(std::fabs(l2[i] - l2[i - 1])));
  ASSERT_TRUE(maxDiff <= 0.05);
  double rmsAfter = 0.0;
  for (int i = after - 4800; i < after; ++i) rmsAfter += static_cast<double>(l2[static_cast<std::size_t>(i)]) * l2[static_cast<std::size_t>(i)];
  ASSERT_NEAR_EPS(10.0 * std::log10(rmsAfter / rmsBefore), 6.0, 0.05);
}

// Inf / NaN at the entry become 0: nothing downstream is poisoned.
void testNonFiniteInputDoesNotPoison() {
  Engine e;
  e.prepare(kSr, 512, 2);
  EngineSettings s = defaults();
  s.metalOn = true;
  e.set(s);
  std::vector<float> l, r;
  pinkPair(l, r, 96000, 0.8, 12u);
  l[1000] = INFINITY;
  r[1001] = -INFINITY;
  l[20000] = NAN;
  r[20000] = NAN;
  run(e, l, r, 512);
  for (std::size_t i = 0; i < l.size(); ++i) {
    ASSERT_TRUE(std::isfinite(l[i]));
    ASSERT_TRUE(std::isfinite(r[i]));
  }
  double tail = 0.0;
  for (std::size_t i = l.size() - 4800; i < l.size(); ++i) tail = std::max(tail, static_cast<double>(std::fabs(l[i])));
  ASSERT_TRUE(tail > 0.1);
  ASSERT_TRUE(std::isfinite(e.meters().inPeakDb[0]) && std::isfinite(e.meters().outPeakDb[0]));
}

// Switching CLIP, LIMIT, FINAL, METALHEAD and BYPASS on and off mid-play glides: no step above 0.05
// between consecutive samples on a sine at -6 dBFS, with INPUT +6 dB so that every stage works.
void testStageSwitchesNoClick() {
  Engine e;
  e.prepare(kSr, 512, 2);
  EngineSettings s = defaults();
  s.inputGainDb = 6.0f;
  e.set(s);
  const int settle = static_cast<int>(kSr);
  std::vector<float> l = sineVec(settle, 100.0, 0.5), r = l;
  run(e, l, r, 512);
  float last = l.back();
  int phase = settle;
  double maxDiff = 0.0;
  auto segment = [&](int len) {
    std::vector<float> a = sineVec(len, 100.0, 0.5, phase), b = a;
    run(e, a, b, 512);
    maxDiff = std::max(maxDiff, static_cast<double>(std::fabs(a[0] - last)));
    for (std::size_t i = 1; i < a.size(); ++i) maxDiff = std::max(maxDiff, static_cast<double>(std::fabs(a[i] - a[i - 1])));
    last = a.back();
    phase += len;
  };
  bool* flags[] = {&s.clipOn, &s.limitOn, &s.finalOn, &s.metalOn, &s.bypass};
  for (bool* f : flags) {
    *f = !*f;
    e.set(s);
    segment(4800);
    *f = !*f;
    e.set(s);
    segment(4800);
  }
  ASSERT_TRUE(maxDiff <= 0.05);
}

// The meters read the stages: GR rows, LUFS, DYN.
void testMetersReadStages() {
  Engine e;
  e.prepare(kSr, 512, 2);
  EngineSettings s = defaults();
  s.metalOn = true;
  s.pushDb = 6.0f;
  s.inputGainDb = 6.0f;
  s.vibe.low = 8;
  s.vibe.dynLow = true;
  e.set(s);
  std::vector<float> l, r;
  pinkPair(l, r, static_cast<int>(4 * kSr), 1.0, 55u);
  run(e, l, r, 512);
  const Meters& m = e.meters();
  ASSERT_TRUE(m.grDb[kGrGlue] > 0.0f);
  ASSERT_TRUE(m.grDb[kGrLimit] > 0.0f);
  ASSERT_TRUE(m.grDb[kGrMetal] >= 0.0f);
  ASSERT_TRUE(std::isfinite(m.lufsShort) && m.lufsShort > -30.0 && m.lufsShort < 0.0);
  ASSERT_TRUE(std::isfinite(m.lufsIntegrated));
  ASSERT_TRUE(m.dynLow > 0.0f);
  ASSERT_TRUE(m.outPeakDb[0] <= -0.3f + 1e-4f);
  e.resetIntegrated();
  ASSERT_TRUE(!std::isfinite(e.meters().lufsIntegrated));

  // a stage that is off shows no reduction
  s.limitOn = false;
  s.metalOn = false;
  s.clipOn = false;
  e.set(s);
  run(e, l, r, 512);
  ASSERT_TRUE(e.meters().grDb[kGrLimit] == 0.0f && e.meters().grDb[kGrMetal] == 0.0f && e.meters().grDb[kGrClip] == 0.0f);
}

} // namespace

int main() {
  RUN_TEST(testMapParamsDefaults);
  RUN_TEST(testListenOffWhenBandOff);
  RUN_TEST(testBypassIsDelayedInput);
  RUN_TEST(testLatencyPerOversampling);
  RUN_TEST(testAllMaxNeverAboveCeiling);
  RUN_TEST(testLoudnessZeroEqualsSettings);
  RUN_TEST(testLoudnessPushesStages);
  RUN_TEST(testLoudnessDoesNotMoveParams);
  RUN_TEST(testSilenceAfterTailIsZero);
  RUN_TEST(testClipLedsPerStage);
  RUN_TEST(testNoAllocationInProcess);
  RUN_TEST(testOutputGainStepNoZipper);
  RUN_TEST(testNonFiniteInputDoesNotPoison);
  RUN_TEST(testStageSwitchesNoClick);
  RUN_TEST(testMetersReadStages);
  std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
