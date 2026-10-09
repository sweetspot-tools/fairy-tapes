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

static_assert(sanitizeNorm(0.5) == 0.5, "usable in constant expressions");

} // namespace

int main() {
  std::printf("fairy_ui sanitize tests\n");
  RUN_TEST(test_in_range_passes_through);
  RUN_TEST(test_out_of_range_clamps);
  RUN_TEST(test_nan_falls_back);
  RUN_TEST(test_inf_falls_back);
  RUN_TEST(test_bad_fallback_is_sanitized_too);
  std::printf("%d passed, %d failed\n", g_passed, g_failed);
  return g_failed;
}
