/**
 * Fairy Tapes · Happy Ending — Unit tests for the LOUDNESS push curve and MATCH.
 * Run via CTest; build target: happy_ending_loudness_macro_tests.
 */

#include "loudness_macro.h"
#include "tuning.h"
#include <cmath>
#include <cstdio>

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

#define ASSERT_NEAR_EPS(a, b, eps) do { \
  if (!(std::fabs((double)(a) - (double)(b)) <= (eps))) { \
    std::fprintf(stderr, "  FAIL [%s:%d]: %s ~= %s (got %.6f vs %.6f, eps %g)\n", \
                 __FILE__, __LINE__, #a, #b, (double)(a), (double)(b), (double)(eps)); \
    ++g_failed; return; \
  } \
} while(0)

#define RUN_TEST(fn) do { \
  int before = g_failed; \
  std::printf("  %s ... ", #fn); \
  fn(); \
  if (g_failed == before) { std::printf("ok\n"); ++g_passed; } else std::printf("FAILED\n"); \
} while(0)

void testZeroIsNoPush() {
  const LoudPush p = loudPush(0.0f);
  ASSERT_TRUE(p.clipDb == 0.0f);
  ASSERT_TRUE(p.limitDb == 0.0f);
  ASSERT_TRUE(p.final01 == 0.0f);
}

void testOrderClipThenLimitThenFinal() {
  const LoudPush a = loudPush(3.0f);
  ASSERT_TRUE(a.clipDb > 0.0f);
  ASSERT_TRUE(a.limitDb == 0.0f);
  ASSERT_TRUE(a.final01 == 0.0f);

  const LoudPush b = loudPush(6.0f);
  ASSERT_NEAR_EPS(b.clipDb, Tuning::kLoudCurve.clipDb, 1e-6);
  ASSERT_TRUE(b.limitDb > 0.0f);
  ASSERT_TRUE(b.final01 == 0.0f);

  const LoudPush c = loudPush(10.0f);
  ASSERT_NEAR_EPS(c.clipDb, 1.5, 1e-6);
  ASSERT_NEAR_EPS(c.limitDb, 3.0, 1e-6);   // a positive amount: the engine subtracts it
  ASSERT_NEAR_EPS(c.final01, 1.0, 1e-6);
}

void testCurveFollowsTuning() {
  const LoudPush c = loudPush(10.0f);
  ASSERT_NEAR_EPS(c.clipDb, Tuning::kLoudCurve.clipDb, 1e-6);
  ASSERT_NEAR_EPS(c.limitDb, -Tuning::kLoudCurve.limitDb, 1e-6);
  ASSERT_NEAR_EPS(c.final01, 1.0, 1e-6);   // 0..1 of the stage ramp; the amounts live in kLoudCurve
}

void testMonotonic() {
  LoudPush prev = loudPush(0.0f);
  for (int i = 1; i <= 20; ++i) {
    const LoudPush p = loudPush(static_cast<float>(i) * 0.5f);
    ASSERT_TRUE(p.clipDb >= prev.clipDb);
    ASSERT_TRUE(p.limitDb >= prev.limitDb);
    ASSERT_TRUE(p.final01 >= prev.final01);
    prev = p;
  }
}

void testOutOfRangeSaturates() {
  const LoudPush lo = loudPush(-2.0f), hi = loudPush(15.0f), top = loudPush(10.0f);
  ASSERT_TRUE(lo.clipDb == 0.0f && lo.limitDb == 0.0f && lo.final01 == 0.0f);
  ASSERT_TRUE(hi.clipDb == top.clipDb && hi.limitDb == top.limitDb && hi.final01 == top.final01);
}

void testMatchMovesTowardTarget() {
  ASSERT_TRUE(matchLoudnessIndex(4, -12.0, -9.0) == 8);   // 4 + round(3 / 0.75)
  ASSERT_TRUE(matchLoudnessIndex(8, -6.0, -9.0) == 4);    // too loud: the knob goes down
  ASSERT_TRUE(matchLoudnessIndex(7, -9.0, -9.0) == 7);    // already centred
}

void testMatchClamps() {
  ASSERT_TRUE(matchLoudnessIndex(18, -30.0, -7.0) == 20);
  ASSERT_TRUE(matchLoudnessIndex(2, 0.0, -14.0) == 0);
  for (int idx = 0; idx <= 20; ++idx)
    for (double st = -60.0; st <= 0.0; st += 7.0) {
      const int r = matchLoudnessIndex(idx, st, -9.0);
      ASSERT_TRUE(r >= 0 && r <= 20);
    }
}

void testMatchIgnoresNoSignal() {
  ASSERT_TRUE(matchLoudnessIndex(5, -INFINITY, -9.0) == 5);
  ASSERT_TRUE(matchLoudnessIndex(5, NAN, -9.0) == 5);
  ASSERT_TRUE(matchLoudnessIndex(5, INFINITY, -9.0) == 5);
}

} // namespace

int main() {
  RUN_TEST(testZeroIsNoPush);
  RUN_TEST(testOrderClipThenLimitThenFinal);
  RUN_TEST(testCurveFollowsTuning);
  RUN_TEST(testMonotonic);
  RUN_TEST(testOutOfRangeSaturates);
  RUN_TEST(testMatchMovesTowardTarget);
  RUN_TEST(testMatchClamps);
  RUN_TEST(testMatchIgnoresNoSignal);
  std::printf("%d passed, %d failed\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
