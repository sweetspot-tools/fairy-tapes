/** Fairy Tapes · shared UI — unit tests for normalized-value sanitizing. */
#include "fairy_ui/sanitize.h"
#include <cmath>
#include <cstdio>
#include <limits>

namespace {

int g_failed = 0;
int g_passed = 0;

#define ASSERT_EQ_D(a, b) do { \
  const double va_ = (a), vb_ = (b); \
  if (!(va_ == vb_)) { \
    std::fprintf(stderr, "  FAIL [%s:%d]: %s == %s (got %.9f vs %.9f)\n", \
                 __FILE__, __LINE__, #a, #b, va_, vb_); \
    ++g_failed; return; \
  } \
} while(0)

#define RUN_TEST(fn) do { \
  int before = g_failed; \
  fn(); \
  if (g_failed == before) { ++g_passed; std::printf("  ok   %s\n", #fn); } \
  else std::printf("  FAIL %s\n", #fn); \
} while(0)

using FairyTapes::Ui::sanitizeNorm;
using FairyTapes::Ui::snapToggle;
using FairyTapes::Ui::snapChoice;
using FairyTapes::Ui::normFromChoiceIndex;

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

void test_in_range_passes_through() {
  ASSERT_EQ_D(sanitizeNorm(0.0), 0.0);
  ASSERT_EQ_D(sanitizeNorm(0.25), 0.25);
  ASSERT_EQ_D(sanitizeNorm(1.0), 1.0);
}

void test_out_of_range_clamps() {
  ASSERT_EQ_D(sanitizeNorm(-0.5), 0.0);
  ASSERT_EQ_D(sanitizeNorm(-1e300), 0.0);
  ASSERT_EQ_D(sanitizeNorm(1.5), 1.0);
  ASSERT_EQ_D(sanitizeNorm(1e300), 1.0);
}

void test_nan_falls_back() {
  ASSERT_EQ_D(sanitizeNorm(kNaN), 0.0);
  ASSERT_EQ_D(sanitizeNorm(kNaN, 0.5), 0.5);
}

void test_inf_falls_back() {
  ASSERT_EQ_D(sanitizeNorm(kInf), 0.0);
  ASSERT_EQ_D(sanitizeNorm(-kInf), 0.0);
  ASSERT_EQ_D(sanitizeNorm(kInf, 0.75), 0.75);
  ASSERT_EQ_D(sanitizeNorm(-kInf, 0.75), 0.75);
}

void test_bad_fallback_is_sanitized_too() {
  ASSERT_EQ_D(sanitizeNorm(kNaN, kNaN), 0.0);
  ASSERT_EQ_D(sanitizeNorm(kNaN, kInf), 0.0);
  ASSERT_EQ_D(sanitizeNorm(kNaN, 2.0), 1.0);
  ASSERT_EQ_D(sanitizeNorm(kNaN, -2.0), 0.0);
}

void test_snapToggle() {
  ASSERT_EQ_D(snapToggle(0.0), 0.0);
  ASSERT_EQ_D(snapToggle(0.49), 0.0);
  ASSERT_EQ_D(snapToggle(0.5), 1.0);
  ASSERT_EQ_D(snapToggle(1.0), 1.0);
  ASSERT_EQ_D(snapToggle(-3.0), 0.0);
  ASSERT_EQ_D(snapToggle(3.0), 1.0);
  ASSERT_EQ_D(snapToggle(kNaN), 0.0);
  ASSERT_EQ_D(snapToggle(kNaN, 1.0), 1.0);
  ASSERT_EQ_D(snapToggle(kInf, 0.7), 1.0);   // fallback is snapped too
  ASSERT_EQ_D(snapToggle(kNaN, kNaN), 0.0);
}

void test_snapChoice_grid_values_are_fixed_points() {
  for (int n = 2; n <= 5; ++n)
    for (int i = 0; i < n; ++i)
      ASSERT_EQ_D(snapChoice(normFromChoiceIndex(i, n), n), normFromChoiceIndex(i, n));
}

void test_snapChoice_off_grid_snaps_to_nearest() {
  for (int n = 2; n <= 5; ++n) {
    for (int k = 0; k <= 1000; ++k) {
      const double v = k / 1000.0;
      const double s = snapChoice(v, n);
      double best = 2.0;
      for (int i = 0; i < n; ++i) {
        const double d = std::fabs(normFromChoiceIndex(i, n) - v);
        if (d < best) best = d;
      }
      // s is a grid value and as close to v as the closest grid value
      bool onGrid = false;
      for (int i = 0; i < n; ++i) onGrid = onGrid || s == normFromChoiceIndex(i, n);
      if (!onGrid || std::fabs(std::fabs(s - v) - best) > 1e-12) {
        std::fprintf(stderr, "  FAIL n=%d v=%.3f -> %.6f (nearest distance %.6f)\n", n, v, s, best);
        ++g_failed; return;
      }
    }
    ASSERT_EQ_D(snapChoice(-1.0, n), 0.0);
    ASSERT_EQ_D(snapChoice(2.0, n), 1.0);
  }
  ASSERT_EQ_D(snapChoice(0.4, 4), 1.0 / 3.0);
  ASSERT_EQ_D(snapChoice(0.5, 3), 0.5);
}

void test_snapChoice_non_finite_uses_snapped_default() {
  ASSERT_EQ_D(snapChoice(kNaN, 4), 0.0);
  ASSERT_EQ_D(snapChoice(kNaN, 4, 1.0), 1.0);
  ASSERT_EQ_D(snapChoice(kInf, 4, 0.7), 2.0 / 3.0);
  ASSERT_EQ_D(snapChoice(-kInf, 3, kNaN), 0.0);
}

static_assert(sanitizeNorm(0.5) == 0.5, "usable in constant expressions");

} // namespace

int main() {
  std::printf("fairy_ui sanitize tests\n");
  RUN_TEST(test_in_range_passes_through);
  RUN_TEST(test_out_of_range_clamps);
  RUN_TEST(test_nan_falls_back);
  RUN_TEST(test_inf_falls_back);
  RUN_TEST(test_bad_fallback_is_sanitized_too);
  RUN_TEST(test_snapToggle);
  RUN_TEST(test_snapChoice_grid_values_are_fixed_points);
  RUN_TEST(test_snapChoice_off_grid_snaps_to_nearest);
  RUN_TEST(test_snapChoice_non_finite_uses_snapped_default);
  std::printf("%d passed, %d failed\n", g_passed, g_failed);
  return g_failed;
}
