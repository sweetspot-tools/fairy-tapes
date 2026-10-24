/**
 * Fairy Tapes · Happy Ending — Unit tests for CLIP (ClipSoftly port, level matched).
 * Run via CTest; build target: happy_ending_clip_tests.
 */

#include "dsp/clip_softly.h"
#include "tuning.h"
#include <algorithm>
#include <atomic>
#include <cmath>
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
using FairyTapes::HappyEnding::DSP::ClipSoftly;

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

double dbOf(double lin) { return 20.0 * std::log10(std::max(lin, 1e-12)); }
double ampOfDb(double db) { return std::pow(10.0, db / 20.0); }

// Tone of 100 cycles over n samples (1 kHz at 48 kHz, n = sr / 10 at any rate): bins are exact.
constexpr int kN = 4800;
constexpr int kCycles = 100;

std::vector<float> sine(int n, int cycles, double amp) {
  std::vector<float> x(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) x[static_cast<std::size_t>(i)] = static_cast<float>(amp * std::sin(2.0 * kPi * cycles * i / n));
  return x;
}

// Amplitude of the k-th harmonic of a tone of `cycles` cycles over x.
double harmonic(const std::vector<float>& x, int cycles, int k) {
  double re = 0.0, im = 0.0;
  const int n = static_cast<int>(x.size());
  for (int i = 0; i < n; ++i) {
    const double ph = 2.0 * kPi * cycles * k * i / n;
    re += x[static_cast<std::size_t>(i)] * std::cos(ph);
    im += x[static_cast<std::size_t>(i)] * std::sin(ph);
  }
  return 2.0 * std::sqrt(re * re + im * im) / n;
}

// THD in dB: harmonics 2..9 against the fundamental.
double thdDb(const std::vector<float>& x, int cycles) {
  const double f = harmonic(x, cycles, 1);
  double sum = 0.0;
  for (int k = 2; k <= 9; ++k) { const double h = harmonic(x, cycles, k); sum += h * h; }
  return dbOf(std::sqrt(sum) / f);
}

double peakOf(const std::vector<float>& x) {
  double p = 0.0;
  for (float v : x) p = std::max(p, static_cast<double>(std::fabs(v)));
  return p;
}

// Runs the clipper over `x` (both channels the same), returns the left output.
std::vector<float> run(ClipSoftly& c, const std::vector<float>& x) {
  std::vector<float> l = x, r = x;
  c.process(l.data(), r.data(), static_cast<int>(l.size()));
  return l;
}

// One block of silence: lets the drive glide settle before measuring.
void settle(ClipSoftly& c, int n = kN) { run(c, std::vector<float>(static_cast<std::size_t>(n), 0.0f)); }

double fundamentalDb(const std::vector<float>& y, int cycles) { return dbOf(harmonic(y, cycles, 1)); }

void testQuietPassesUnchanged() {
  ClipSoftly c;
  c.prepare(48000.0);
  c.setDriveDb(6.0f);
  settle(c);
  const auto y = run(c, sine(kN, kCycles, ampOfDb(-40.0)));
  ASSERT_NEAR_EPS(fundamentalDb(y, kCycles), -40.0, 0.1);
}

void testPeaksAreRounded() {
  ClipSoftly c;
  c.prepare(48000.0);
  c.setDriveDb(6.0f);
  settle(c);
  const auto x = sine(kN, kCycles, ampOfDb(-1.0));
  const auto y = run(c, x);
  ASSERT_TRUE(peakOf(y) < peakOf(x));
  ASSERT_TRUE(c.shavedDb() > 1.0f);
  // Rounded, not flat-topped into a corner: no step larger than the input's own.
  double maxStepIn = 0.0, maxStepOut = 0.0;
  for (std::size_t i = 1; i < x.size(); ++i) {
    maxStepIn = std::max(maxStepIn, static_cast<double>(std::fabs(x[i] - x[i - 1])));
    maxStepOut = std::max(maxStepOut, static_cast<double>(std::fabs(y[i] - y[i - 1])));
  }
  ASSERT_TRUE(maxStepOut <= maxStepIn * 1.01);
}

// The plan asks for THD below -60 dB at DRIVE 0 on a -6 dBFS sine. The ClipSoftly curve is
// sin(x): its third harmonic on a 0.5 amplitude sine is about -41 dB (J3(0.5)/J1(0.5)), so that
// figure cannot hold for a faithful port. Transparent means: clean at moderate level, and only
// the curve's own gentle third harmonic near the top.
void testDriveZeroTransparent() {
  ClipSoftly c;
  c.prepare(48000.0);
  c.setDriveDb(0.0f);
  const auto quiet = run(c, sine(kN, kCycles, ampOfDb(-20.0)));
  ASSERT_TRUE(thdDb(quiet, kCycles) < -60.0);
  ASSERT_NEAR_EPS(fundamentalDb(quiet, kCycles), -20.0, 0.1);
  ClipSoftly d;
  d.prepare(48000.0);
  d.setDriveDb(0.0f);
  const auto hot = run(d, sine(kN, kCycles, ampOfDb(-6.0)));
  ASSERT_TRUE(thdDb(hot, kCycles) < -38.0);
  ASSERT_NEAR_EPS(fundamentalDb(hot, kCycles), -6.0, 0.4);
}

void testLevelMatchedAtOversampledRates() {
  for (double sr : {88200.0, 176400.0}) {
    const int n = static_cast<int>(sr / 10.0);
    ClipSoftly c;
    c.prepare(sr);
    c.setDriveDb(6.0f);
    settle(c, n);
    const auto y = run(c, sine(n, kCycles, ampOfDb(-40.0)));
    ASSERT_NEAR_EPS(fundamentalDb(y, kCycles), -40.0, 0.1);
    // and the peaks are still rounded at that rate
    ClipSoftly d;
    d.prepare(sr);
    d.setDriveDb(6.0f);
    settle(d, n);
    const auto hot = sine(n, kCycles, ampOfDb(-1.0));
    const auto z = run(d, hot);
    ASSERT_TRUE(peakOf(z) < peakOf(hot));
    ASSERT_TRUE(d.shavedDb() > 1.0f);
  }
}

void testSameShapeAtAnyRate() {
  // The same 1 kHz sine gives the same peak and about the same harmonic content at 48, 96 and 192 kHz.
  double peak[3], thd[3];
  int idx = 0;
  for (double sr : {48000.0, 96000.0, 192000.0}) {
    const int n = static_cast<int>(sr / 10.0);
    ClipSoftly c;
    c.prepare(sr);
    c.setDriveDb(4.0f);
    settle(c, n);
    const auto y = run(c, sine(n, kCycles, ampOfDb(-3.0)));
    peak[idx] = peakOf(y);
    thd[idx] = thdDb(y, kCycles);
    ++idx;
  }
  ASSERT_NEAR_EPS(dbOf(peak[1]), dbOf(peak[0]), 0.05);
  ASSERT_NEAR_EPS(dbOf(peak[2]), dbOf(peak[0]), 0.05);
  ASSERT_NEAR_EPS(thd[1], thd[0], 1.0);
  ASSERT_NEAR_EPS(thd[2], thd[0], 1.0);
}

void testPeakCeilingFollowsDrive() {
  // Level matched: the loudest output possible is 1 / drive.
  for (float drive : {0.0f, 3.0f, 6.0f}) {
    ClipSoftly c;
    c.prepare(48000.0);
    c.setDriveDb(drive);
    settle(c);
    const auto y = run(c, sine(kN, kCycles, 4.0));  // far beyond full scale
    ASSERT_TRUE(peakOf(y) <= 1.0 / ampOfDb(drive) + 1e-4);
  }
}

void testNoDelay() {
  ClipSoftly c;
  c.prepare(48000.0);
  c.setDriveDb(0.0f);
  std::vector<float> x(64, 0.0f);
  x[10] = 0.01f;
  const auto y = run(c, x);
  ASSERT_NEAR_EPS(y[10], 0.01, 1e-4);
  ASSERT_TRUE(std::fabs(y[11]) < 1e-6);
  ASSERT_TRUE(std::fabs(y[9]) < 1e-6);
}

void testShavedDbQuietIsZeroAndResetsEachBlock() {
  ClipSoftly c;
  c.prepare(48000.0);
  c.setDriveDb(6.0f);
  settle(c);
  run(c, sine(kN, kCycles, ampOfDb(-1.0)));
  ASSERT_TRUE(c.shavedDb() > 1.0f);
  run(c, sine(kN, kCycles, ampOfDb(-40.0)));
  ASSERT_TRUE(c.shavedDb() < 0.05f);
  run(c, std::vector<float>(static_cast<std::size_t>(kN), 0.0f));
  ASSERT_TRUE(c.shavedDb() == 0.0f);
}

void testShavedDbIsLinkedStereo() {
  ClipSoftly c;
  c.prepare(48000.0);
  c.setDriveDb(6.0f);
  settle(c);
  std::vector<float> l = sine(kN, kCycles, ampOfDb(-40.0));
  std::vector<float> r = sine(kN, kCycles, ampOfDb(-1.0));
  c.process(l.data(), r.data(), kN);
  ASSERT_TRUE(c.shavedDb() > 1.0f);                              // the loud right channel drives the reading
  ASSERT_NEAR_EPS(fundamentalDb(l, kCycles), -40.0, 0.1);        // the quiet left one is untouched
  ASSERT_TRUE(peakOf(r) < ampOfDb(-1.0));
}

void testDriveChangeIsContinuous() {
  ClipSoftly c;
  c.prepare(48000.0);
  c.setDriveDb(0.0f);
  const auto x = sine(kN * 2, kCycles * 2, ampOfDb(-3.0));
  const std::vector<float> a(x.begin(), x.begin() + kN);
  const std::vector<float> b(x.begin() + kN, x.end());
  const auto ya = run(c, a);
  c.setDriveDb(6.0f);
  const auto yb = run(c, b);
  // No click at the seam or while gliding: a 1 kHz sine at -3 dBFS moves at most 0.0926 per sample.
  double maxStep = std::fabs(yb[0] - ya.back());
  for (std::size_t i = 1; i < yb.size(); ++i) maxStep = std::max(maxStep, static_cast<double>(std::fabs(yb[i] - yb[i - 1])));
  ASSERT_TRUE(maxStep < 0.1);
  // The glide lasts kClipRampMs: by the end of the block the new drive is fully in force.
  const std::vector<float> tail(yb.end() - kN / 4, yb.end());
  const std::vector<float> tailIn(b.end() - kN / 4, b.end());
  ASSERT_TRUE(peakOf(tail) < peakOf(tailIn) * 0.8);
}

void testSilenceStaysSilent() {
  ClipSoftly c;
  c.prepare(48000.0);
  c.setDriveDb(6.0f);
  const auto y = run(c, std::vector<float>(512, 0.0f));
  ASSERT_TRUE(peakOf(y) == 0.0);
  ASSERT_TRUE(c.shavedDb() == 0.0f);
}

void testNoAllocationInProcess() {
  ClipSoftly c;
  c.prepare(48000.0);
  std::vector<float> l = sine(512, 10, 0.9), r = l;
  g_allocs = 0;
  g_countAllocs = true;
  for (int i = 0; i < 20; ++i) {
    c.setDriveDb(static_cast<float>(i % 13) * 0.5f);
    c.process(l.data(), r.data(), 512);
    (void)c.shavedDb();
  }
  g_countAllocs = false;
  ASSERT_TRUE(g_allocs == 0);
}

} // namespace

int main() {
  RUN_TEST(testQuietPassesUnchanged);
  RUN_TEST(testPeaksAreRounded);
  RUN_TEST(testDriveZeroTransparent);
  RUN_TEST(testLevelMatchedAtOversampledRates);
  RUN_TEST(testSameShapeAtAnyRate);
  RUN_TEST(testPeakCeilingFollowsDrive);
  RUN_TEST(testNoDelay);
  RUN_TEST(testShavedDbQuietIsZeroAndResetsEachBlock);
  RUN_TEST(testShavedDbIsLinkedStereo);
  RUN_TEST(testDriveChangeIsContinuous);
  RUN_TEST(testSilenceStaysSilent);
  RUN_TEST(testNoAllocationInProcess);
  std::printf("%d passed, %d failed\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
