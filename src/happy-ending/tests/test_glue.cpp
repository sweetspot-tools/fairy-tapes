/**
 * Fairy Tapes · Happy Ending — Unit tests for GLUE (SSL-style bus compressor, parallel mix).
 * Run via CTest; build target: happy_ending_glue_tests.
 */

#include "dsp/glue.h"
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
using FairyTapes::HappyEnding::DSP::Glue;
using FairyTapes::HappyEnding::DSP::GlueSettings;

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

double dbOf(double lin) { return 20.0 * std::log10(std::max(lin, 1e-12)); }
double ampOfDb(double db) { return std::pow(10.0, db / 20.0); }

// Position of a value in the table of a parameter (the tests never hardcode indices).
double tableValue(ParamID id, std::size_t index) { return valueTable(id).data[index]; }

// Pink noise (Paul Kellet's filter), scaled to a given RMS in dBFS; deterministic.
std::vector<float> pinkNoise(int n, double rmsDb, unsigned seed = 1) {
  std::mt19937 gen(seed);
  std::uniform_real_distribution<double> dist(-1.0, 1.0);
  std::vector<double> x(static_cast<std::size_t>(n));
  double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
  double sum = 0.0;
  for (int i = 0; i < n; ++i) {
    const double w = dist(gen);
    b0 = 0.99886 * b0 + w * 0.0555179;
    b1 = 0.99332 * b1 + w * 0.0750759;
    b2 = 0.96900 * b2 + w * 0.1538520;
    b3 = 0.86650 * b3 + w * 0.3104856;
    b4 = 0.55000 * b4 + w * 0.5329522;
    b5 = -0.7616 * b5 - w * 0.0168980;
    const double p = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362;
    b6 = w * 0.115926;
    x[static_cast<std::size_t>(i)] = p;
    sum += p * p;
  }
  const double scale = ampOfDb(rmsDb) / std::sqrt(sum / n);
  std::vector<float> out(x.size());
  for (std::size_t i = 0; i < x.size(); ++i) out[i] = static_cast<float>(x[i] * scale);
  return out;
}

std::vector<float> sine(double hz, int n, double amp, double sr = kSr) {
  std::vector<float> x(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i)
    x[static_cast<std::size_t>(i)] = static_cast<float>(amp * std::sin(2.0 * kPi * hz * i / sr));
  return x;
}

double rmsDb(const std::vector<float>& x, int from, int to) {
  double s = 0.0;
  for (int i = from; i < to; ++i) s += static_cast<double>(x[static_cast<std::size_t>(i)]) * x[static_cast<std::size_t>(i)];
  return dbOf(std::sqrt(s / std::max(1, to - from)));
}

GlueSettings baseSettings() {
  GlueSettings s;
  s.on = true;
  s.threshDb = -20.0f;
  s.attackMs = static_cast<float>(tableValue(kGlueAttack, 3));        // 3 ms
  s.releaseS = static_cast<float>(tableValue(kGlueRelease, 1));       // 0.3 s
  s.ratio = static_cast<float>(tableValue(kGlueRatio, 1));            // 2:1
  s.makeupDb = 0.0f;
  s.mix = 1.0f;
  s.scHpfHz = static_cast<float>(tableValue(kGlueScHpf, 1));          // 60 Hz
  s.warm = false;
  return s;
}

struct RunResult {
  std::vector<float> l, r;
  std::vector<float> grPerBlock;   // grDb() after each block
  std::vector<float> netPerBlock;  // netGrDb() after each block
};

RunResult run(const GlueSettings& settings, const std::vector<float>& inL, const std::vector<float>& inR,
              double sr = kSr, int block = 256) {
  Glue g;
  g.prepare(sr, block);
  g.set(settings);
  RunResult res;
  res.l = inL;
  res.r = inR;
  const int n = static_cast<int>(inL.size());
  for (int i = 0; i < n; i += block) {
    const int m = std::min(block, n - i);
    g.process(res.l.data() + i, res.r.data() + i, m);
    res.grPerBlock.push_back(g.grDb());
    res.netPerBlock.push_back(g.netGrDb());
  }
  return res;
}

RunResult run(const GlueSettings& settings, const std::vector<float>& in, double sr = kSr) {
  return run(settings, in, in, sr);
}

// Mean of the per-block GR over the blocks from `fromFrac` of the run to its end.
double meanGr(const std::vector<float>& gr, double fromFrac) {
  const std::size_t from = static_cast<std::size_t>(gr.size() * fromFrac);
  double s = 0.0;
  for (std::size_t i = from; i < gr.size(); ++i) s += gr[i];
  return s / static_cast<double>(gr.size() - from);
}

bool allFinite(const std::vector<float>& x) {
  for (float v : x) if (!std::isfinite(v)) return false;
  return true;
}

void testMoreRatioMoreReduction() {
  const std::vector<float> in = pinkNoise(static_cast<int>(4 * kSr), -6.0);
  double gr[3];
  for (std::size_t i = 0; i < 3; ++i) {
    GlueSettings s = baseSettings();
    s.ratio = static_cast<float>(tableValue(kGlueRatio, i));
    gr[i] = meanGr(run(s, in).grPerBlock, 0.5);
  }
  ASSERT_TRUE(gr[0] > 0.5);        // 1.5:1 already works
  ASSERT_TRUE(gr[1] > gr[0] + 0.3);
  ASSERT_TRUE(gr[2] > gr[1] + 0.3);
}

void testMixZeroIsDry() {
  const std::vector<float> in = pinkNoise(static_cast<int>(2 * kSr), -6.0);
  GlueSettings s = baseSettings();
  s.mix = 0.0f;
  s.makeupDb = 6.0f;  // MAKEUP only touches the compressed branch
  const RunResult r = run(s, in);
  double worst = 0.0;
  for (std::size_t i = 0; i < in.size(); ++i) {
    worst = std::max(worst, std::fabs(static_cast<double>(r.l[i]) - in[i]));
    worst = std::max(worst, std::fabs(static_cast<double>(r.r[i]) - in[i]));
  }
  ASSERT_TRUE(worst <= 1e-6);
  // The compressor itself still works inside the block.
  ASSERT_TRUE(meanGr(r.grPerBlock, 0.5) > 1.0);
}

void testMixHalfIsParallel() {
  const std::vector<float> in = pinkNoise(static_cast<int>(3 * kSr), -9.0);
  GlueSettings s = baseSettings();
  s.makeupDb = 3.0f;
  s.mix = 1.0f;
  const RunResult wet = run(s, in);
  s.mix = 0.5f;
  const RunResult half = run(s, in);
  const int n = static_cast<int>(in.size());
  std::vector<float> sum(in.size());
  for (std::size_t i = 0; i < in.size(); ++i) sum[i] = 0.5f * wet.l[i] + 0.5f * in[i];
  const int from = n / 2;
  // The two branches really differ (otherwise the test would say nothing).
  ASSERT_TRUE(std::fabs(rmsDb(wet.l, from, n) - rmsDb(in, from, n)) > 1.0);
  ASSERT_NEAR_EPS(rmsDb(half.l, from, n), rmsDb(sum, from, n), 0.1);
}

void testScHpfReducesBassPumping() {
  const std::vector<float> in = sine(40.0, static_cast<int>(3 * kSr), ampOfDb(-6.0));
  GlueSettings s = baseSettings();
  s.ratio = static_cast<float>(tableValue(kGlueRatio, 2));  // 4:1
  s.scHpfHz = static_cast<float>(tableValue(kGlueScHpf, 0));  // Off
  const double off = meanGr(run(s, in).grPerBlock, 0.5);
  s.scHpfHz = static_cast<float>(tableValue(kGlueScHpf, 3));  // 150 Hz
  const double hpf = meanGr(run(s, in).grPerBlock, 0.5);
  ASSERT_TRUE(off > 3.0);
  ASSERT_TRUE(hpf <= off - 3.0);
}

// A click: a loud 1 kHz burst after silence. The "first transient" is the first few milliseconds
// of the burst (after the first cycles, which no detector can catch); WARM lets more of it through.
double clickPeak(bool warm) {
  const int pre = static_cast<int>(0.1 * kSr);
  const int burst = static_cast<int>(0.05 * kSr);
  std::vector<float> in(static_cast<std::size_t>(pre + burst + static_cast<int>(0.05 * kSr)), 0.0f);
  const std::vector<float> tone = sine(1000.0, burst, ampOfDb(-3.0));
  std::copy(tone.begin(), tone.end(), in.begin() + pre);
  GlueSettings s = baseSettings();
  s.ratio = static_cast<float>(tableValue(kGlueRatio, 2));
  s.attackMs = static_cast<float>(tableValue(kGlueAttack, 3));  // 3 ms
  s.warm = warm;
  const RunResult r = run(s, in);
  const int a = pre + static_cast<int>(0.003 * kSr);
  const int b = pre + static_cast<int>(0.012 * kSr);
  float peak = 0.0f;
  for (int i = a; i < b; ++i) peak = std::max(peak, std::fabs(r.l[static_cast<std::size_t>(i)]));
  return dbOf(peak);
}

void testWarmSoftensAttack() {
  const double plain = clickPeak(false);
  const double warm = clickPeak(true);
  ASSERT_TRUE(warm > plain + 0.3);
}

void testAutoReleaseRuns() {
  const int sig = static_cast<int>(3 * kSr);
  const int total = static_cast<int>(6 * kSr);
  std::vector<float> in = pinkNoise(sig, -6.0);
  in.resize(static_cast<std::size_t>(total), 0.0f);
  GlueSettings s = baseSettings();
  s.ratio = static_cast<float>(tableValue(kGlueRatio, 2));
  s.releaseS = static_cast<float>(tableValue(kGlueRelease, 4));  // Auto = 0
  ASSERT_TRUE(s.releaseS == 0.0f);
  const RunResult r = run(s, in);
  const std::size_t endBlock = static_cast<std::size_t>(sig / 256);
  const std::size_t after2s = endBlock + static_cast<std::size_t>(2.0 * kSr / 256) + 1;
  ASSERT_TRUE(r.grPerBlock[endBlock - 1] > 2.0f);   // it was compressing
  ASSERT_TRUE(r.grPerBlock[after2s] < 0.5f);
  // And it does recover gradually, not instantly.
  ASSERT_TRUE(r.grPerBlock[endBlock + 1] > r.grPerBlock[after2s]);
}

void testStageOffIsIdentity() {
  const std::vector<float> in = pinkNoise(static_cast<int>(kSr), -6.0);
  GlueSettings s = baseSettings();
  s.on = false;
  s.makeupDb = 6.0f;
  const RunResult r = run(s, in);
  ASSERT_TRUE(r.l == in && r.r == in);
  ASSERT_TRUE(r.grPerBlock.back() == 0.0f && r.netPerBlock.back() == 0.0f);
}

void testStageSwitchIsContinuous() {
  const std::vector<float> in = sine(200.0, static_cast<int>(2 * kSr), ampOfDb(-6.0));
  Glue g;
  g.prepare(kSr, 256);
  GlueSettings s = baseSettings();
  s.ratio = 4.0f;
  g.set(s);
  std::vector<float> l = in, r = in;
  const int half = static_cast<int>(kSr);
  g.process(l.data(), r.data(), half);
  s.on = false;
  g.set(s);
  g.process(l.data() + half, r.data() + half, half);
  // The sine moves at most 2 pi f / sr * amp per sample; allow for the gain change on top.
  const double maxStep = 2.0 * kPi * 200.0 / kSr * ampOfDb(-6.0) * 1.5;
  double worst = 0.0;
  for (int i = half - 4; i < half + 2000; ++i)
    worst = std::max(worst, std::fabs(static_cast<double>(l[i + 1]) - l[i]));
  ASSERT_TRUE(worst < maxStep);
  // Settled: bit exact after the glide.
  ASSERT_TRUE(l[2 * half - 1] == in[2 * half - 1]);
}

void testMakeupOnlyOnCompressedBranch() {
  // Far below the threshold nothing compresses: MAKEUP is a plain gain of the compressed branch,
  // and the stage is otherwise transparent.
  const std::vector<float> in = sine(1000.0, static_cast<int>(kSr), ampOfDb(-40.0));
  GlueSettings s = baseSettings();
  s.threshDb = 0.0f;
  const RunResult flat = run(s, in);
  s.makeupDb = 6.0f;
  const RunResult up = run(s, in);
  const int n = static_cast<int>(in.size());
  ASSERT_NEAR_EPS(rmsDb(flat.l, n / 2, n) - rmsDb(in, n / 2, n), 0.0, 0.3);
  ASSERT_NEAR_EPS(rmsDb(up.l, n / 2, n) - rmsDb(flat.l, n / 2, n), 6.0, 0.05);
}

void testNetGrFollowsMix() {
  const std::vector<float> in = pinkNoise(static_cast<int>(3 * kSr), -6.0);
  GlueSettings s = baseSettings();
  s.ratio = 4.0f;
  s.makeupDb = 6.0f;
  s.mix = 1.0f;
  const RunResult full = run(s, in);
  const double gr = full.grPerBlock.back();
  ASSERT_TRUE(gr > 2.0);
  ASSERT_NEAR_EPS(full.netPerBlock.back(), gr, 1e-4);  // MAKEUP does not enter
  s.mix = 0.5f;
  const RunResult half = run(s, in);
  const double g = std::pow(10.0, -half.grPerBlock.back() / 20.0);
  ASSERT_NEAR_EPS(half.netPerBlock.back(), -20.0 * std::log10(0.5 * g + 0.5), 1e-3);
  ASSERT_TRUE(half.netPerBlock.back() < half.grPerBlock.back());
  s.mix = 0.0f;
  const RunResult dry = run(s, in);
  ASSERT_TRUE(dry.netPerBlock.back() == 0.0f);
}

void testStereoIsLinked() {
  // The right channel is 20 dB quieter: an unlinked compressor would leave it alone.
  const std::vector<float> l = pinkNoise(static_cast<int>(4 * kSr), -6.0);
  std::vector<float> r(l.size());
  for (std::size_t i = 0; i < l.size(); ++i) r[i] = l[i] * 0.1f;
  GlueSettings s = baseSettings();
  s.ratio = 4.0f;
  const RunResult res = run(s, l, r);
  ASSERT_TRUE(meanGr(res.grPerBlock, 0.5) > 2.0);
  const int n = static_cast<int>(l.size());
  const double gainL = rmsDb(res.l, n / 2, n) - rmsDb(l, n / 2, n);
  const double gainR = rmsDb(res.r, n / 2, n) - rmsDb(r, n / 2, n);
  ASSERT_NEAR_EPS(gainL, gainR, 0.5);
  ASSERT_TRUE(gainR < -1.0);
}

void testMonoStaysMono() {
  const std::vector<float> in = pinkNoise(static_cast<int>(kSr), -6.0);
  const RunResult r = run(baseSettings(), in);
  ASSERT_TRUE(r.l == r.r);
}

void testSilenceSettlesToExactZero() {
  const int sig = static_cast<int>(2 * kSr);
  std::vector<float> in = pinkNoise(sig, -3.0);
  in.resize(static_cast<std::size_t>(sig + static_cast<int>(8 * kSr)), 0.0f);
  GlueSettings s = baseSettings();
  s.ratio = 4.0f;
  s.threshDb = -20.0f;
  const RunResult r = run(s, in);
  const std::size_t n = in.size();
  for (std::size_t i = n - 2000; i < n; ++i) {
    ASSERT_TRUE(r.l[i] == 0.0f);   // exactly zero: no denormals left in the filters
    ASSERT_TRUE(r.r[i] == 0.0f);
  }
}

void testStableAtExtremes() {
  const double rates[] = {44100.0, 48000.0, 96000.0};
  for (double sr : rates) {
    std::vector<float> in = pinkNoise(static_cast<int>(2 * sr), 0.0);
    const std::vector<float> loud = sine(80.0, static_cast<int>(sr), 3.0, sr);  // +9.5 dBFS
    in.insert(in.end(), loud.begin(), loud.end());
    for (float ratio : {1.5f, 4.0f})
      for (float rel : {0.0f, 0.1f, 1.2f})
        for (float att : {0.1f, 30.0f})
          for (float hpf : {0.0f, 150.0f}) {
            GlueSettings s = baseSettings();
            s.threshDb = -20.0f;
            s.ratio = ratio;
            s.releaseS = rel;
            s.attackMs = att;
            s.scHpfHz = hpf;
            s.makeupDb = 6.0f;
            s.mix = 0.5f;
            s.warm = true;
            const RunResult r = run(s, in, sr);
            ASSERT_TRUE(allFinite(r.l) && allFinite(r.r));
            for (float v : r.l) ASSERT_TRUE(std::fabs(v) < 8.0f);
          }
  }
}

void testSettingsChangeIsContinuous() {
  // Stepping MAKEUP and MIX while a sine runs does not click.
  const int block = 128;
  Glue g;
  g.prepare(kSr, block);
  GlueSettings s = baseSettings();
  s.threshDb = 0.0f;
  g.set(s);
  const std::vector<float> in = sine(100.0, static_cast<int>(kSr), ampOfDb(-12.0));
  std::vector<float> l = in, r = in;
  double worst = 0.0;
  float prev = 0.0f;
  for (int i = 0, k = 0; i + block <= static_cast<int>(in.size()); i += block, ++k) {
    if (k % 8 == 0) {
      s.makeupDb = (k % 16 == 0) ? 0.0f : 6.0f;
      s.mix = (k % 24 == 0) ? 1.0f : 0.5f;
      g.set(s);
    }
    g.process(l.data() + i, r.data() + i, block);
    for (int j = 0; j < block; ++j) {
      worst = std::max(worst, std::fabs(static_cast<double>(l[i + j]) - prev));
      prev = l[i + j];
    }
  }
  const double sineStep = 2.0 * kPi * 100.0 / kSr * ampOfDb(-12.0) * 2.0;
  ASSERT_TRUE(worst < sineStep * 1.5);
}

void testNoAllocationInProcess() {
  Glue g;
  g.prepare(kSr, 512);
  GlueSettings s = baseSettings();
  std::vector<float> l = pinkNoise(512, -6.0), r = l;
  g.set(s);
  g.process(l.data(), r.data(), 512);
  g_allocs = 0;
  g_countAllocs = true;
  for (int i = 0; i < 40; ++i) {
    s.releaseS = (i % 3 == 0) ? 0.0f : 0.3f;
    s.ratio = (i % 2 == 0) ? 4.0f : 1.5f;
    s.scHpfHz = (i % 4 == 0) ? 0.0f : 90.0f;
    s.warm = (i % 5 == 0);
    s.on = (i % 7) != 3;
    g.set(s);
    g.process(l.data(), r.data(), 512);
    (void)g.grDb();
    (void)g.netGrDb();
  }
  g_countAllocs = false;
  ASSERT_TRUE(g_allocs == 0);
}

} // namespace

int main() {
  RUN_TEST(testMoreRatioMoreReduction);
  RUN_TEST(testMixZeroIsDry);
  RUN_TEST(testMixHalfIsParallel);
  RUN_TEST(testScHpfReducesBassPumping);
  RUN_TEST(testWarmSoftensAttack);
  RUN_TEST(testAutoReleaseRuns);
  RUN_TEST(testStageOffIsIdentity);
  RUN_TEST(testStageSwitchIsContinuous);
  RUN_TEST(testMakeupOnlyOnCompressedBranch);
  RUN_TEST(testNetGrFollowsMix);
  RUN_TEST(testStereoIsLinked);
  RUN_TEST(testMonoStaysMono);
  RUN_TEST(testSilenceSettlesToExactZero);
  RUN_TEST(testStableAtExtremes);
  RUN_TEST(testSettingsChangeIsContinuous);
  RUN_TEST(testNoAllocationInProcess);
  std::printf("%d passed, %d failed\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
