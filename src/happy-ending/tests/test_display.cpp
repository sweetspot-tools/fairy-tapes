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

void testNewDisplayIdsFollowResetIntegrated() {
  // appended after the existing ids, which keep their numbers
  ASSERT_TRUE(kDispGlueIn == kResetIntegrated + 1 && kDispGlueGr == kDispGlueIn + 1);
  ASSERT_TRUE(kDispLimitIn == kDispGlueGr + 1 && kDispLimitThresh == kDispLimitIn + 1);
  ASSERT_TRUE(kDispClipIn == kDispLimitThresh + 1 && kDispFinalIn == kDispClipIn + 1);
  ASSERT_TRUE(kDispFinalIn + 1 == kNumDisplayIDs);
  ASSERT_TRUE(kDispLatency == 1000 + 22 && kClearClips == 1000 + 23 && kResetIntegrated == 1000 + 24);
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

void testStageDbNorm() {
  ASSERT_TRUE(kStageDisplayMinDb == -60.0 && kStageDisplayMaxDb == 12.0);
  ASSERT_NEAR(normFromStageDb(-60.0), 0.0, 1e-12);
  ASSERT_NEAR(normFromStageDb(-120.0), 0.0, 1e-12);   // stage off
  ASSERT_NEAR(normFromStageDb(12.0), 1.0, 1e-12);
  ASSERT_NEAR(normFromStageDb(30.0), 1.0, 1e-12);
  ASSERT_NEAR(normFromStageDb(0.0), 60.0 / 72.0, 1e-12);
  ASSERT_NEAR(normFromStageDb(-24.0), 0.5, 1e-12);
  ASSERT_TRUE(normFromStageDb(std::nan("")) == 0.0);
  ASSERT_TRUE(normFromStageDb(-kInf) == 0.0);
  ASSERT_NEAR(stageDbFromNorm(0.0), -60.0, 1e-12);
  ASSERT_NEAR(stageDbFromNorm(1.0), 12.0, 1e-12);
  ASSERT_NEAR(stageDbFromNorm(-0.3), -60.0, 1e-12);   // clamped
  ASSERT_NEAR(stageDbFromNorm(4.0), 12.0, 1e-12);
  for (double d : {-59.0, -30.0, -6.0, 0.0, 3.5, 11.0}) ASSERT_NEAR(stageDbFromNorm(normFromStageDb(d)), d, 1e-9);
}

void testThreshNorm() {
  ASSERT_NEAR(normFromThreshDb(-12.0), 0.0, 1e-12);
  ASSERT_NEAR(normFromThreshDb(0.0), 1.0, 1e-12);
  ASSERT_NEAR(normFromThreshDb(-6.0), 0.5, 1e-12);
  ASSERT_NEAR(normFromThreshDb(-30.0), 0.0, 1e-12);
  ASSERT_NEAR(normFromThreshDb(3.0), 1.0, 1e-12);
  ASSERT_TRUE(normFromThreshDb(std::nan("")) == 0.0);
  ASSERT_NEAR(threshDbFromNorm(0.0), -12.0, 1e-12);
  ASSERT_NEAR(threshDbFromNorm(1.0), 0.0, 1e-12);
  ASSERT_NEAR(threshDbFromNorm(-1.0), -12.0, 1e-12);
  ASSERT_NEAR(threshDbFromNorm(2.0), 0.0, 1e-12);
  for (double d : {-11.5, -8.0, -3.5, -2.0, -0.5}) ASSERT_NEAR(threshDbFromNorm(normFromThreshDb(d)), d, 1e-9);
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

// Stage inputs and the GLUE GR are held as maxima, the threshold is the latest value; a take restarts
// the maxima (-120 / 0) and keeps the threshold for the next write.
void testHoldStageInputsGlueGrAndThreshold() {
  MeterHold h;
  Meters a;
  a.stageInDb[0] = -10.0f; a.stageInDb[1] = -20.0f; a.stageInDb[2] = -120.0f; a.stageInDb[3] = -30.0f;
  a.glueGrDb = 5.0f; a.limitThreshDb = -2.0f;
  Meters b;
  b.stageInDb[0] = -40.0f; b.stageInDb[1] = -15.0f; b.stageInDb[2] = -50.0f; b.stageInDb[3] = -120.0f;
  b.glueGrDb = 1.0f; b.limitThreshDb = -5.0f;
  h.add(a, 0.0);
  h.add(b, 0.0);
  const DisplayFrame f = h.take();
  ASSERT_NEAR(f.stageInDb[0], -10.0, 1e-6);
  ASSERT_NEAR(f.stageInDb[1], -15.0, 1e-6);
  ASSERT_NEAR(f.stageInDb[2], -50.0, 1e-6);
  ASSERT_NEAR(f.stageInDb[3], -30.0, 1e-6);
  ASSERT_NEAR(f.glueGrDb, 5.0, 1e-6);
  ASSERT_NEAR(f.limitThreshDb, -5.0, 1e-6);   // the latest, not held

  const DisplayFrame empty = h.take();        // nothing added since the last take
  for (int i = 0; i < 4; ++i) ASSERT_NEAR(empty.stageInDb[i], -120.0, 1e-6);
  ASSERT_NEAR(empty.glueGrDb, 0.0, 1e-6);
  ASSERT_NEAR(empty.limitThreshDb, -5.0, 1e-6);   // kept for the next write

  h.add(b, 0.0);
  ASSERT_NEAR(h.take().stageInDb[1], -15.0, 1e-6);
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
  RUN_TEST(testNewDisplayIdsFollowResetIntegrated);
  RUN_TEST(testPeakNorm);
  RUN_TEST(testGrNorm);
  RUN_TEST(testLufsNormRoundTripAndNoMeasurement);
  RUN_TEST(testLatencyNorm);
  RUN_TEST(testStageDbNorm);
  RUN_TEST(testThreshNorm);
  RUN_TEST(testHoldKeepsPeaksGrAndClipsBetweenWrites);
  RUN_TEST(testHoldStageInputsGlueGrAndThreshold);
  RUN_TEST(testClearClipsDropsHeldLeds);
  RUN_TEST(testMonoOffsetAppliesToFiniteLufsOnly);
  RUN_TEST(testRateLimiterWritesFirstBlockThenPaced);
  std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
  return g_failed ? 1 : 0;
}
