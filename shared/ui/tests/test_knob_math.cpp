/** Fairy Tapes · shared UI — unit tests for knob/control math. */
#include "fairy_ui/knob_math.h"
#include <cmath>
#include <cstdio>

namespace {

int g_failed = 0;
int g_passed = 0;

bool approx(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }

#define ASSERT_TRUE(expr) do { \
  if (!(expr)) { \
    std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); \
    ++g_failed; return; \
  } \
} while(0)

#define ASSERT_APPROX(a, b) do { \
  if (!approx((a), (b))) { \
    std::fprintf(stderr, "  FAIL [%s:%d]: %s ~ %s (got %.9f vs %.9f)\n", \
                 __FILE__, __LINE__, #a, #b, (double)(a), (double)(b)); \
    ++g_failed; return; \
  } \
} while(0)

#define RUN_TEST(fn) do { \
  int before = g_failed; \
  fn(); \
  if (g_failed == before) { ++g_passed; std::printf("  ok   %s\n", #fn); } \
  else std::printf("  FAIL %s\n", #fn); \
} while(0)

using namespace FairyTapes::Ui;

void test_knobAngle_endpoints() {
  ASSERT_APPROX(knobAngleFromNorm(0.0), 225.0);
  ASSERT_APPROX(knobAngleFromNorm(1.0), -45.0);
  ASSERT_APPROX(knobAngleFromNorm(0.5), 90.0);
  ASSERT_APPROX(knobAngleFromNorm(-3.0), 225.0);
  ASSERT_APPROX(knobAngleFromNorm(7.0), -45.0);
}

void test_normFromDrag_full_range_and_fine() {
  ASSERT_APPROX(normFromDrag(0.0, 200.0, false), 1.0);
  ASSERT_APPROX(normFromDrag(0.0, 200.0, true), 0.1);
}

void test_normFromDrag_clamps() {
  ASSERT_APPROX(normFromDrag(0.9, 500.0, false), 1.0);
  ASSERT_APPROX(normFromDrag(0.1, -500.0, false), 0.0);
  ASSERT_APPROX(normFromDrag(0.95, 1000.0, true), 1.0);
}

void test_choice_index_roundtrip() {
  for (int c = 2; c <= 4; ++c)
    for (int i = 0; i < c; ++i)
      ASSERT_TRUE(choiceIndexFromNorm(normFromChoiceIndex(i, c), c) == i);
}

void test_meterNormFromDb() {
  ASSERT_APPROX(meterNormFromDb(-60.0, -60.0, 6.0), 0.0);
  ASSERT_APPROX(meterNormFromDb(6.0, -60.0, 6.0), 1.0);
  ASSERT_APPROX(meterNormFromDb(-27.0, -60.0, 6.0), 0.5);
  ASSERT_APPROX(meterNormFromDb(-100.0, -60.0, 6.0), 0.0);
  ASSERT_APPROX(meterNormFromDb(20.0, -60.0, 6.0), 1.0);
}

} // namespace

int main() {
  std::printf("fairy_ui tests\n");
  RUN_TEST(test_knobAngle_endpoints);
  RUN_TEST(test_normFromDrag_full_range_and_fine);
  RUN_TEST(test_normFromDrag_clamps);
  RUN_TEST(test_choice_index_roundtrip);
  RUN_TEST(test_meterNormFromDb);
  std::printf("%d passed, %d failed\n", g_passed, g_failed);
  return g_failed;
}
