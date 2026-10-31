/**
 * Fairy Tapes · Happy Ending — Unit tests for the display parameters (normalization, hold, pacing).
 * Run via CTest; build target: happy_ending_display_tests.
 */

#include "display.h"
#include <cmath>
#include <cstdio>
#include <limits>

using namespace FairyTapes::HappyEnding;

namespace {

int g_failed = 0;
int g_passed = 0;

#define ASSERT_TRUE(expr) do { \
  if (!(expr)) { \
    std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); \
    ++g_failed; return; \
  } \
} while(0)

#define ASSERT_NEAR(a, b, tol) ASSERT_TRUE(std::fabs((double)(a) - (double)(b)) <= (tol))

#define RUN_TEST(fn) do { \
  const int before = g_failed; \
  fn(); \
  if (g_failed == before) { ++g_passed; std::printf("PASS %s\n", #fn); } \
  else std::printf("FAIL %s\n", #fn); \
} while(0)

constexpr double kInf = std::numeric_limits<double>::infinity();

void testDisplayIdsAreContiguousFromBase() {
  ASSERT_TRUE(kDisplayBase == 1000 && kDispInL == 1000);
  ASSERT_TRUE(kDispGr4 - kDispGr0 == 4 && kDispClip8 - kDispClip0 == 8);
  ASSERT_TRUE(kDispClip0 == kDispGr4 + 1 && kDispLufsShort == kDispClip8 + 1);
  ASSERT_TRUE(kDispLatency + 1 == kClearClips && kClearClips + 1 == kResetIntegrated);
  ASSERT_TRUE(kClearClips >= kDisplayBase && static_cast<int>(kDisplayBase) > static_cast<int>(kSetActive));
}

void testPeakNorm() {
  ASSERT_NEAR(normFromPeakDb(-60.0), 0.0, 1e-12);
  ASSERT_NEAR(normFromPeakDb(-120.0), 0.0, 1e-12);
  ASSERT_NEAR(normFromPeakDb(0.0), 1.0, 1e-12);
  ASSERT_NEAR(normFromPeakDb(6.0), 1.0, 1e-12);
  ASSERT_NEAR(normFromPeakDb(-6.0206), 0.8997, 1e-3);
  ASSERT_NEAR(peakDbFromNorm(normFromPeakDb(-18.0)), -18.0, 1e-9);
}

void testGrNorm() {
  ASSERT_NEAR(normFromGrDb(0.0), 0.0, 1e-12);
  ASSERT_NEAR(normFromGrDb(6.0), 0.5, 1e-12);
  ASSERT_NEAR(normFromGrDb(30.0), 1.0, 1e-12);
  ASSERT_NEAR(grDbFromNorm(normFromGrDb(3.0)), 3.0, 1e-9);
}

void testLufsNormRoundTripAndNoMeasurement() {
  ASSERT_TRUE(normFromLufs(-kInf) == 0.0);
  ASSERT_TRUE(normFromLufs(std::nan("")) == 0.0);
  ASSERT_TRUE(normFromLufs(-70.0) == 0.0);
  ASSERT_NEAR(normFromLufs(0.0), 1.0, 1e-12);
  ASSERT_NEAR(normFromLufs(5.0), 1.0, 1e-12);
  for (double l : {-59.0, -23.0, -14.0, -9.5, -7.0, -1.0}) ASSERT_NEAR(lufsFromNorm(normFromLufs(l)), l, 1e-9);
  ASSERT_TRUE(std::isinf(lufsFromNorm(0.0)) && lufsFromNorm(0.0) < 0.0);
  ASSERT_TRUE(std::isinf(lufsFromNorm(-0.2)));
}

void testLatencyNorm() {
  ASSERT_NEAR(normFromLatency(0), 0.0, 1e-12);
  for (int s : {1, 64, 481, 1920, 16384}) ASSERT_TRUE(latencyFromNorm(normFromLatency(s)) == s);
  ASSERT_TRUE(normFromLatency(1000000) == 1.0 && normFromLatency(-5) == 0.0);
}

Meters meters(float inL, float inR, float gr0, bool clipIn, double shortL) {
  Meters m;
  m.inPeakDb[0] = inL;
  m.inPeakDb[1] = inR;
  m.outPeakDb[0] = inL - 1.0f;
  m.outPeakDb[1] = inR - 1.0f;
  m.grDb[kGrGlue] = gr0;
  m.clip[kLedIn] = clipIn;
  m.lufsShort = shortL;
  m.lufsIntegrated = shortL - 1.0;
  m.dynLow = 0.25f;
  return m;
}

void testHoldKeepsPeaksGrAndClipsBetweenWrites() {
  MeterHold h;
  h.add(meters(-6.0f, -30.0f, 4.0f, true, -20.0), 0.0);   // a transient block
  h.add(meters(-40.0f, -50.0f, 1.0f, false, -18.0), 0.0); // a quiet block after it
  const DisplayFrame f = h.take();
  ASSERT_NEAR(f.inPeakDb[0], -6.0, 1e-6);
  ASSERT_NEAR(f.inPeakDb[1], -30.0, 1e-6);
  ASSERT_NEAR(f.outPeakDb[0], -7.0, 1e-6);
  ASSERT_NEAR(f.grDb[kGrGlue], 4.0, 1e-6);
  ASSERT_TRUE(f.clip[kLedIn] && !f.clip[kLedOut]);
  ASSERT_NEAR(f.lufsShort, -18.0, 1e-9);  // the latest, not held
  ASSERT_NEAR(f.dynLow, 0.25, 1e-6);

  // After take the hold restarts from the next blocks only
  h.add(meters(-40.0f, -50.0f, 0.5f, false, -17.0), 0.0);
  const DisplayFrame g = h.take();
  ASSERT_NEAR(g.inPeakDb[0], -40.0, 1e-6);
  ASSERT_NEAR(g.grDb[kGrGlue], 0.5, 1e-6);
  ASSERT_TRUE(!g.clip[kLedIn]);
  ASSERT_NEAR(g.lufsShort, -17.0, 1e-9);
}

void testClearClipsDropsHeldLeds() {
  MeterHold h;
  h.add(meters(-6.0f, -6.0f, 0.0f, true, -20.0), 0.0);
  h.clearClips();
  h.add(meters(-6.0f, -6.0f, 0.0f, false, -20.0), 0.0);
  ASSERT_TRUE(!h.take().clip[kLedIn]);
}

void testMonoOffsetAppliesToFiniteLufsOnly() {
  MeterHold h;
  h.add(meters(-6.0f, -6.0f, 0.0f, false, -14.0), kDualMonoLufsOffsetDb);
  DisplayFrame f = h.take();
  ASSERT_NEAR(f.lufsShort, -14.0 + kDualMonoLufsOffsetDb, 1e-9);
  ASSERT_NEAR(f.lufsIntegrated, -15.0 + kDualMonoLufsOffsetDb, 1e-9);
  h.add(meters(-6.0f, -6.0f, 0.0f, false, -kInf), kDualMonoLufsOffsetDb);
  f = h.take();
  ASSERT_TRUE(std::isinf(f.lufsShort) && f.lufsShort < 0.0);
}

void testRateLimiterWritesFirstBlockThenPaced() {
  DisplayRateLimiter r;
  r.prepare(48000.0);                   // 33 ms = 1584 samples
  ASSERT_TRUE(r.advance(64));           // first block
  int writes = 0, blocks = 0;
  for (int i = 0; i < 750; ++i) { ++blocks; if (r.advance(64)) ++writes; }  // 1.0 s of audio
  ASSERT_TRUE(writes >= 28 && writes <= 31);
  ASSERT_TRUE(!r.advance(0));
}

} // namespace

int main() {
  RUN_TEST(testDisplayIdsAreContiguousFromBase);
  RUN_TEST(testPeakNorm);
  RUN_TEST(testGrNorm);
  RUN_TEST(testLufsNormRoundTripAndNoMeasurement);
  RUN_TEST(testLatencyNorm);
  RUN_TEST(testHoldKeepsPeaksGrAndClipsBetweenWrites);
  RUN_TEST(testClearClipsDropsHeldLeds);
  RUN_TEST(testMonoOffsetAppliesToFiniteLufsOnly);
  RUN_TEST(testRateLimiterWritesFirstBlockThenPaced);
  std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
  return g_failed ? 1 : 0;
}
