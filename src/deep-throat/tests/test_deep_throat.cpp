/**
 * Fairy Tapes · Deep Throat — Unit tests for params, mappings and state.
 * Run via CTest; build target: deep_throat_tests.
 */

#include "params.h"
#include "state.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

namespace {

int g_failed = 0;
int g_passed = 0;

bool approx(double a, double b, double eps) { return std::fabs(a - b) <= eps; }

#define ASSERT_TRUE(expr) do { \
  if (!(expr)) { \
    std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); \
    ++g_failed; return; \
  } \
} while(0)

#define ASSERT_NEAR(a, b, eps) do { \
  if (!approx((a), (b), (eps))) { \
    std::fprintf(stderr, "  FAIL [%s:%d]: %s ~= %s (got %.9f vs %.9f)\n", \
                 __FILE__, __LINE__, #a, #b, (double)(a), (double)(b)); \
    ++g_failed; return; \
  } \
} while(0)

#define RUN_TEST(fn) do { \
  fn(); \
  ++g_passed; \
} while(0)

using namespace FairyTapes::DeepThroat;

void test_param_enum_count() {
  ASSERT_TRUE(kNumParams == 25);
  ASSERT_TRUE(kBypass == 0);
  ASSERT_TRUE(kEqAmount == 24);
}

void test_deess_freq_range() {
  ASSERT_NEAR(deEssFreqHzFromNorm(0.0), 4000.0, 1e-6);
  ASSERT_NEAR(deEssFreqHzFromNorm(1.0), 10000.0, 1e-6);
  ASSERT_NEAR(deEssFreqHzFromNorm(0.53), 6500.0, 50.0);
  ASSERT_NEAR(deEssFreqHzFromNorm(kDefaultDeEssFreq), 6500.0, 50.0);
}

void test_comp_choice_steps() {
  ASSERT_TRUE(compRatioChoiceFromNorm(0.0) == 0);   // AUTO
  ASSERT_TRUE(compRatioChoiceFromNorm(1.0) == 7);   // 20:1
  ASSERT_TRUE(compRatioChoiceFromNorm(1.0 / 7.0) == 1);
  ASSERT_TRUE(compRatioFromChoice(0) == 0.0);
  ASSERT_TRUE(compRatioFromChoice(1) == 2.0);
  ASSERT_TRUE(compRatioFromChoice(7) == 20.0);
  ASSERT_TRUE(compDistChoiceFromNorm(0.0) == 0);    // AUTO
  ASSERT_TRUE(compDistChoiceFromNorm(1.0) == 3);    // Dist 3
  ASSERT_TRUE(compDistChoiceFromNorm(1.0 / 3.0) == 1);  // OFF
  ASSERT_TRUE(compHpfChoiceFromNorm(0.0) == 0);     // OFF
  ASSERT_TRUE(compHpfChoiceFromNorm(0.5) == 1);     // 80 Hz
  ASSERT_TRUE(compHpfChoiceFromNorm(1.0) == 2);     // 150 Hz
  ASSERT_TRUE(compHpfHzFromChoice(0) == 0.0);
  ASSERT_TRUE(compHpfHzFromChoice(1) == 80.0);
  ASSERT_TRUE(compHpfHzFromChoice(2) == 150.0);
}

void test_mode_from_norm_steps() {
  ASSERT_TRUE(modeFromNorm(0.0) == Mode::Speak);
  ASSERT_TRUE(modeFromNorm(0.5) == Mode::Sing);
  ASSERT_TRUE(modeFromNorm(1.0) == Mode::Puke);
  ASSERT_TRUE(modeFromNorm(0.2) == Mode::Speak);
  ASSERT_TRUE(modeFromNorm(0.8) == Mode::Puke);
}

void test_io_gain_ranges() {
  ASSERT_NEAR(inputDbFromNorm(0.0), -12.0, 1e-9);
  ASSERT_NEAR(inputDbFromNorm(0.5), 0.0, 1e-9);
  ASSERT_NEAR(inputDbFromNorm(1.0), 12.0, 1e-9);
  ASSERT_NEAR(outputDbFromNorm(0.0), -12.0, 1e-9);
  ASSERT_NEAR(outputDbFromNorm(1.0), 12.0, 1e-9);
  ASSERT_NEAR(riderTargetDbFromNorm(0.0), -30.0, 1e-9);
  ASSERT_NEAR(riderTargetDbFromNorm(1.0), -10.0, 1e-9);
}

void test_defaults() {
  double d[kNumParams];
  loadDefaults(d);
  ASSERT_TRUE(d[kBypass] == 0.0);
  ASSERT_TRUE(d[kMode] == 0.0);
  ASSERT_TRUE(d[kRiderIn] == 1.0 && d[kBreathIn] == 1.0 && d[kEqIn] == 1.0);
  ASSERT_TRUE(d[kDeEssIn] == 1.0 && d[kCompIn] == 1.0 && d[kToneIn] == 1.0);
  ASSERT_TRUE(d[kDeEssListen] == 0.0 && d[kBreathListen] == 0.0);
  ASSERT_TRUE(d[kCompMix] == 1.0 && d[kCompRatio] == 0.0 && d[kCompDist] == 0.0);
  ASSERT_TRUE(d[kCompHpf] == 0.5 && d[kEqAmount] == 0.5);
  ASSERT_TRUE(d[kInput] == 0.5 && d[kOutput] == 0.5);
}

void test_state_roundtrip() {
  double in[kNumParams];
  for (int i = 0; i < (int)kNumParams; ++i) in[i] = (i + 1) / 26.0;
  std::vector<uint8_t> bytes;
  serializeState(in, bytes);
  ASSERT_TRUE(bytes.size() == kStateSize);
  double out[kNumParams] = {};
  ASSERT_TRUE(deserializeState(bytes.data(), bytes.size(), out));
  for (int i = 0; i < (int)kNumParams; ++i) ASSERT_TRUE(out[i] == in[i]);
}

void test_state_rejects_bad_version_and_nan() {
  double in[kNumParams];
  loadDefaults(in);
  std::vector<uint8_t> bytes;
  serializeState(in, bytes);
  double out[kNumParams];
  loadDefaults(out);
  out[kTone] = 0.123;

  std::vector<uint8_t> bad = bytes;
  bad[0] = 99;  // unknown version
  ASSERT_TRUE(!deserializeState(bad.data(), bad.size(), out));
  ASSERT_TRUE(!deserializeState(bytes.data(), bytes.size() - 1, out));  // short
  ASSERT_TRUE(!deserializeState(nullptr, 0, out));

  double nan_in[kNumParams];
  loadDefaults(nan_in);
  nan_in[kTone] = std::numeric_limits<double>::quiet_NaN();
  serializeState(nan_in, bad);
  ASSERT_TRUE(!deserializeState(bad.data(), bad.size(), out));
  ASSERT_TRUE(out[kTone] == 0.123);  // untouched on failure

  double wild[kNumParams];
  loadDefaults(wild);
  wild[kInput] = 5.0;
  wild[kOutput] = -3.0;
  serializeState(wild, bad);
  ASSERT_TRUE(deserializeState(bad.data(), bad.size(), out));
  ASSERT_TRUE(out[kInput] == 1.0 && out[kOutput] == 0.0);  // clamped
}

} // namespace

int main() {
  RUN_TEST(test_param_enum_count);
  RUN_TEST(test_deess_freq_range);
  RUN_TEST(test_comp_choice_steps);
  RUN_TEST(test_mode_from_norm_steps);
  RUN_TEST(test_io_gain_ranges);
  RUN_TEST(test_defaults);
  RUN_TEST(test_state_roundtrip);
  RUN_TEST(test_state_rejects_bad_version_and_nan);

  std::fprintf(stdout, "\n%d passed, %d failed.\n", g_passed, g_failed);
  if (g_failed > 0) {
    std::fprintf(stderr, "%d test(s) FAILED.\n", g_failed);
    return EXIT_FAILURE;
  }
  std::fprintf(stdout, "All tests passed.\n");
  return EXIT_SUCCESS;
}
