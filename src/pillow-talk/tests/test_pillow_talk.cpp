/**
 * Fairy Tapes · Pillow Talk — Unit tests for params and pure helpers.
 * Run via CTest; build target: pillow_talk_tests.
 */

#include "params.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

constexpr double kEps = 1e-6;
int g_failed = 0;
int g_passed = 0;

bool approx(double a, double b, double eps = kEps) {
  return std::fabs(a - b) <= eps;
}

#define ASSERT_TRUE(expr) do { \
  if (!(expr)) { \
    std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); \
    ++g_failed; return; \
  } \
} while(0)

#define ASSERT_APPROX(a, b) do { \
  if (!approx((a), (b))) { \
    std::fprintf(stderr, "  FAIL [%s:%d]: %s ~= %s (got %.9f vs %.9f)\n", \
                 __FILE__, __LINE__, #a, #b, (double)(a), (double)(b)); \
    ++g_failed; return; \
  } \
} while(0)

#define RUN_TEST(fn) do { \
  fn(); \
  ++g_passed; \
} while(0)

using namespace FairyTapes::PillowTalk;

void test_noteToMs_120bpm() {
  ASSERT_APPROX(noteToMs(0, 120.0), 125.0);
  ASSERT_APPROX(noteToMs(1, 120.0), 250.0);
  ASSERT_APPROX(noteToMs(2, 120.0), 500.0);
  ASSERT_APPROX(noteToMs(3, 120.0), 1000.0);
}

void test_noteToMs_bpm_zero_uses_120() {
  ASSERT_APPROX(noteToMs(2, 0.0), 500.0);
  ASSERT_APPROX(noteToMs(1, -10.0), 250.0);
}

void test_ranges_boundaries() {
  ASSERT_APPROX(timeMsFromNorm(0.0), 20.0);
  ASSERT_APPROX(timeMsFromNorm(1.0), 1200.0);
  ASSERT_APPROX(holdMsFromNorm(0.0), 50.0);
  ASSERT_APPROX(holdMsFromNorm(1.0), 1000.0);
  ASSERT_APPROX(releaseMsFromNorm(0.0), 50.0);
  ASSERT_APPROX(releaseMsFromNorm(1.0), 2000.0);
  ASSERT_APPROX(feedbackFromNorm(0.0), 0.0);
  ASSERT_APPROX(feedbackFromNorm(1.0), 0.95);
  ASSERT_APPROX(toneHzFromNorm(0.0), 800.0);
  ASSERT_APPROX(toneHzFromNorm(1.0), 12000.0);
  ASSERT_APPROX(thresholdDbFromNorm(0.0), -60.0);
  ASSERT_APPROX(thresholdDbFromNorm(1.0), 0.0);
  ASSERT_APPROX(duckDepthDbFromNorm(0.0), 0.0);
}

void test_duckDepth_full_is_mute_constant() {
  ASSERT_TRUE(duckDepthDbFromNorm(1.0) == kDuckMuteDb);
}

void test_param_enum_count() {
  ASSERT_TRUE(kNumParams == 16);
}

void test_feedback_never_exceeds_095() {
  for (int i = 0; i <= 100; ++i)
    ASSERT_TRUE(feedbackFromNorm(i / 100.0) <= 0.95 + 1e-12);
}

} // namespace

int main() {
  std::fprintf(stdout, "Running Pillow Talk tests...\n\n");

  RUN_TEST(test_noteToMs_120bpm);
  RUN_TEST(test_noteToMs_bpm_zero_uses_120);
  RUN_TEST(test_ranges_boundaries);
  RUN_TEST(test_duckDepth_full_is_mute_constant);
  RUN_TEST(test_param_enum_count);
  RUN_TEST(test_feedback_never_exceeds_095);

  std::fprintf(stdout, "\n%d passed, %d failed.\n", g_passed, g_failed);
  if (g_failed > 0) {
    std::fprintf(stderr, "%d test(s) FAILED.\n", g_failed);
    return EXIT_FAILURE;
  }
  std::fprintf(stdout, "All tests passed.\n");
  return EXIT_SUCCESS;
}
