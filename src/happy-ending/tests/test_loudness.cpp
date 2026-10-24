/**
 * Fairy Tapes · Happy Ending — Unit tests for the LUFS meter (ITU-R BS.1770-4, EBU Tech 3341 cases).
 * Run via CTest; build target: happy_ending_loudness_tests.
 */

#include "dsp/loudness_meter.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
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

using FairyTapes::HappyEnding::DSP::LoudnessMeter;
using FairyTapes::HappyEnding::DSP::designKWeighting;

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

double ampOfDb(double db) { return std::pow(10.0, db / 20.0); }

// Feeds `seconds` of a 1 kHz sine of `peakDb` dBFS peak on both channels, in blocks of an awkward
// size so that nothing depends on the block boundaries. `phase` carries across calls.
struct Source {
  double sr;
  double phase = 0.0;
  explicit Source(double rate) : sr(rate) {}

  void feed(LoudnessMeter& m, double seconds, double peakDb, double hz = 1000.0) {
    const double amp = std::isinf(peakDb) ? 0.0 : ampOfDb(peakDb);
    const long total = std::lround(seconds * sr);
    std::vector<float> l(481), r(481);
    long done = 0;
    int chunk = 481;
    while (done < total) {
      const int n = static_cast<int>(std::min<long>(chunk, total - done));
      for (int i = 0; i < n; ++i) {
        const float v = static_cast<float>(amp * std::sin(phase));
        l[i] = r[i] = v;
        phase += 2.0 * kPi * hz / sr;
        if (phase > 2.0 * kPi) phase -= 2.0 * kPi;
      }
      m.process(l.data(), r.data(), n);
      done += n;
      chunk = chunk == 481 ? 37 : 481;
    }
  }
};

void testKWeightingMatchesStandardAt48k() {
  // BS.1770-4 table 1 and 2 (48 kHz).
  const auto k = designKWeighting(48000.0);
  ASSERT_NEAR_EPS(k.shelf.b0, 1.53512485958697, 1e-6);
  ASSERT_NEAR_EPS(k.shelf.b1, -2.69169618940638, 1e-6);
  ASSERT_NEAR_EPS(k.shelf.b2, 1.19839281085285, 1e-6);
  ASSERT_NEAR_EPS(k.shelf.a1, -1.69065929318241, 1e-6);
  ASSERT_NEAR_EPS(k.shelf.a2, 0.73248077421585, 1e-6);
  ASSERT_NEAR_EPS(k.highPass.b0, 1.0, 1e-9);
  ASSERT_NEAR_EPS(k.highPass.b1, -2.0, 1e-9);
  ASSERT_NEAR_EPS(k.highPass.b2, 1.0, 1e-9);
  ASSERT_NEAR_EPS(k.highPass.a1, -1.99004745483398, 1e-6);
  ASSERT_NEAR_EPS(k.highPass.a2, 0.99007225036621, 1e-6);
}

void testNoValueYet() {
  LoudnessMeter m;
  m.prepare(48000.0);
  ASSERT_TRUE(std::isinf(m.shortTermLufs()) && m.shortTermLufs() < 0);
  ASSERT_TRUE(std::isinf(m.integratedLufs()) && m.integratedLufs() < 0);
  Source s(48000.0);
  s.feed(m, 2.0, -23.0);  // not yet 3 s: no short-term value; integrated already has blocks
  ASSERT_TRUE(std::isinf(m.shortTermLufs()) && m.shortTermLufs() < 0);
  ASSERT_TRUE(!std::isinf(m.integratedLufs()));
}

void testStereoSine1kAtMinus23() {
  LoudnessMeter m;
  m.prepare(48000.0);
  Source s(48000.0);
  s.feed(m, 20.0, -23.0);
  ASSERT_NEAR_EPS(m.integratedLufs(), -23.0, 0.1);
  ASSERT_NEAR_EPS(m.shortTermLufs(), -23.0, 0.1);
}

void testStereoSine1kAtMinus33() {
  LoudnessMeter m;
  m.prepare(48000.0);
  Source s(48000.0);
  s.feed(m, 20.0, -33.0);
  ASSERT_NEAR_EPS(m.integratedLufs(), -33.0, 0.1);
  ASSERT_NEAR_EPS(m.shortTermLufs(), -33.0, 0.1);
}

void testShortTermFollowsLevel() {
  LoudnessMeter m;
  m.prepare(48000.0);
  Source s(48000.0);
  s.feed(m, 5.0, -23.0);
  ASSERT_NEAR_EPS(m.shortTermLufs(), -23.0, 0.1);
  s.feed(m, 3.0, -33.0);  // the window now holds only the new level
  ASSERT_NEAR_EPS(m.shortTermLufs(), -33.0, 0.1);
  // After 1.5 s of it the window is half and half: -23 + 10 log10((1 + 0.1) / 2) = -25.6 LUFS.
  LoudnessMeter m2;
  m2.prepare(48000.0);
  Source s2(48000.0);
  s2.feed(m2, 1.5, -23.0);
  s2.feed(m2, 1.5, -33.0);
  ASSERT_NEAR_EPS(m2.shortTermLufs(), -23.0 + 10.0 * std::log10(0.55), 0.1);
}

void testGatingIgnoresSilence() {
  LoudnessMeter m;
  m.prepare(48000.0);
  Source s(48000.0);
  s.feed(m, 10.0, -23.0);
  s.feed(m, 10.0, -std::numeric_limits<double>::infinity());
  ASSERT_NEAR_EPS(m.integratedLufs(), -23.0, 0.1);
  ASSERT_TRUE(std::isinf(m.shortTermLufs()) && m.shortTermLufs() < 0);  // the window is all silence
}

void testRelativeGateDropsQuietPart() {
  // A part more than 10 LU under the rest does not count (EBU Tech 3341 case: relative gate).
  LoudnessMeter m;
  m.prepare(48000.0);
  Source s(48000.0);
  s.feed(m, 10.0, -23.0);
  s.feed(m, 10.0, -40.0);
  ASSERT_NEAR_EPS(m.integratedLufs(), -23.0, 0.15);
}

void testRelativeGateKeepsCloseParts() {
  // 10 s at -23 and 10 s at -30: both within 10 LU, so both count: -23 + 10 log10((1 + 10^-0.7) / 2).
  LoudnessMeter m;
  m.prepare(48000.0);
  Source s(48000.0);
  s.feed(m, 10.0, -23.0);
  s.feed(m, 10.0, -30.0);
  ASSERT_NEAR_EPS(m.integratedLufs(), -23.0 + 10.0 * std::log10((1.0 + std::pow(10.0, -0.7)) / 2.0), 0.15);
}

void testAbsoluteGate() {
  // A signal under -70 LUFS never passes the absolute gate.
  LoudnessMeter m;
  m.prepare(48000.0);
  Source s(48000.0);
  s.feed(m, 10.0, -75.0);
  ASSERT_TRUE(std::isinf(m.integratedLufs()) && m.integratedLufs() < 0);
}

void testResetIntegrated() {
  LoudnessMeter m;
  m.prepare(48000.0);
  Source s(48000.0);
  s.feed(m, 10.0, -23.0);
  ASSERT_NEAR_EPS(m.integratedLufs(), -23.0, 0.1);
  const double st = m.shortTermLufs();
  m.resetIntegrated();
  ASSERT_TRUE(std::isinf(m.integratedLufs()) && m.integratedLufs() < 0);
  ASSERT_NEAR_EPS(m.shortTermLufs(), st, 1e-9);  // short-term untouched
  s.feed(m, 10.0, -33.0);
  ASSERT_NEAR_EPS(m.integratedLufs(), -33.0, 0.1);  // only what came after the reset
}

void testWorksAt44kTo192k() {
  for (double sr : {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0}) {
    LoudnessMeter m;
    m.prepare(sr);
    Source s(sr);
    s.feed(m, 20.0, -23.0);
    if (std::fabs(m.integratedLufs() + 23.0) > 0.1 || std::fabs(m.shortTermLufs() + 23.0) > 0.1) {
      std::fprintf(stderr, "  FAIL: sr %.0f integrated %.4f short-term %.4f\n", sr, m.integratedLufs(), m.shortTermLufs());
      ++g_failed;
      return;
    }
  }
}

void testKWeightingShapeAtOtherRates() {
  // The curve is the same at every rate: a 10 kHz sine reads the same LUFS at 48 and 192 kHz,
  // and a 50 Hz sine is clearly below a 1 kHz one (RLB high pass).
  auto read = [](double sr, double hz) {
    LoudnessMeter m;
    m.prepare(sr);
    Source s(sr);
    s.feed(m, 10.0, -23.0, hz);
    return m.integratedLufs();
  };
  ASSERT_NEAR_EPS(read(48000.0, 10000.0), read(192000.0, 10000.0), 0.1);
  ASSERT_NEAR_EPS(read(44100.0, 10000.0), read(96000.0, 10000.0), 0.1);
  ASSERT_TRUE(read(48000.0, 50.0) < read(48000.0, 1000.0) - 1.0);
  ASSERT_TRUE(read(48000.0, 10000.0) > read(48000.0, 1000.0) + 2.0);  // the shelf adds about +4 dB up there
}

void testChannelsAreSummed() {
  // Same level on one channel only reads 3 dB lower than on both.
  LoudnessMeter both, one;
  both.prepare(48000.0);
  one.prepare(48000.0);
  std::vector<float> sig(4800), zero(4800, 0.0f);
  double ph = 0.0;
  for (int b = 0; b < 100; ++b) {
    for (auto& v : sig) {
      v = static_cast<float>(ampOfDb(-23.0) * std::sin(ph));
      ph += 2.0 * kPi * 1000.0 / 48000.0;
    }
    both.process(sig.data(), sig.data(), 4800);
    one.process(sig.data(), zero.data(), 4800);
  }
  ASSERT_NEAR_EPS(both.integratedLufs() - one.integratedLufs(), 3.0103, 0.05);
}

void testLongRunStaysBounded() {
  // Runs for far longer than a song without allocating, and keeps reading right.
  LoudnessMeter m;
  m.prepare(48000.0);
  std::vector<float> sig(4800);
  double ph = 0.0;
  g_allocs = 0;
  g_countAllocs = true;
  for (int b = 0; b < 3000; ++b) {  // 5 minutes
    for (auto& v : sig) {
      v = static_cast<float>(ampOfDb(-23.0) * std::sin(ph));
      ph += 2.0 * kPi * 1000.0 / 48000.0;
    }
    m.process(sig.data(), sig.data(), 4800);
  }
  m.resetIntegrated();
  m.process(sig.data(), sig.data(), 4800);
  g_countAllocs = false;
  ASSERT_TRUE(g_allocs == 0);
}

void testSilenceAndSilentNoNaN() {
  LoudnessMeter m;
  m.prepare(48000.0);
  std::vector<float> z(4800, 0.0f);
  for (int b = 0; b < 100; ++b) m.process(z.data(), z.data(), 4800);
  ASSERT_TRUE(std::isinf(m.shortTermLufs()) && m.shortTermLufs() < 0);
  ASSERT_TRUE(std::isinf(m.integratedLufs()) && m.integratedLufs() < 0);
}

} // namespace

int main() {
  RUN_TEST(testKWeightingMatchesStandardAt48k);
  RUN_TEST(testNoValueYet);
  RUN_TEST(testStereoSine1kAtMinus23);
  RUN_TEST(testStereoSine1kAtMinus33);
  RUN_TEST(testShortTermFollowsLevel);
  RUN_TEST(testGatingIgnoresSilence);
  RUN_TEST(testRelativeGateDropsQuietPart);
  RUN_TEST(testRelativeGateKeepsCloseParts);
  RUN_TEST(testAbsoluteGate);
  RUN_TEST(testResetIntegrated);
  RUN_TEST(testWorksAt44kTo192k);
  RUN_TEST(testKWeightingShapeAtOtherRates);
  RUN_TEST(testChannelsAreSummed);
  RUN_TEST(testLongRunStaysBounded);
  RUN_TEST(testSilenceAndSilentNoNaN);
  std::printf("%d passed, %d failed\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
