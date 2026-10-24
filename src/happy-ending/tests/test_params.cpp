/**
 * Fairy Tapes · Happy Ending — Unit tests for the frozen parameter table.
 * Run via CTest; build target: happy_ending_params_tests.
 */

#include "params.h"
#include "tuning.h"
#include <cmath>
#include <cstdio>
#include <cstring>

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

#define ASSERT_NEAR(a, b) do { \
  if (std::fabs((double)(a) - (double)(b)) > 1e-9) { \
    std::fprintf(stderr, "  FAIL [%s:%d]: %s ~= %s (got %.9f vs %.9f)\n", \
                 __FILE__, __LINE__, #a, #b, (double)(a), (double)(b)); \
    ++g_failed; return; \
  } \
} while(0)

#define RUN_TEST(fn) do { \
  const int before = g_failed; \
  fn(); \
  if (g_failed == before) { ++g_passed; std::printf("PASS %s\n", #fn); } \
  else std::printf("FAIL %s\n", #fn); \
} while(0)

// Value looked up through the param -> table mapping.
double valueOf(ParamID id, int index) { return valueTable(id).data[index]; }

void testStepsMatchTables() {
  int withTable = 0;
  for (int i = 0; i < kNumParams; ++i) {
    const auto id = static_cast<ParamID>(i);
    const ValueTable t = valueTable(id);
    if (t.size == 0) continue;  // booleans, enum selectors and kLimitRelease have no table
    ++withTable;
    ASSERT_TRUE(kParams[id].steps == static_cast<int>(t.size));
  }
  ASSERT_TRUE(withTable > 20);
}

void testStepsAndDefaultsInRange() {
  for (int i = 0; i < kNumParams; ++i) {
    ASSERT_TRUE(kParams[i].name != nullptr && kParams[i].name[0] != '\0');
    ASSERT_TRUE(kParams[i].steps >= 2);
    ASSERT_TRUE(kParams[i].defaultIndex >= 0 && kParams[i].defaultIndex < kParams[i].steps);
  }
}

void testNamesUnique() {
  for (int i = 0; i < kNumParams; ++i)
    for (int j = i + 1; j < kNumParams; ++j)
      ASSERT_TRUE(std::strcmp(kParams[i].name, kParams[j].name) != 0);
}

void testDefaultsRoundTrip() {
  for (int i = 0; i < kNumParams; ++i) {
    const auto id = static_cast<ParamID>(i);
    const int def = kParams[id].defaultIndex;
    ASSERT_TRUE(indexFromNorm(id, normFromIndex(id, def)) == def);
    // every step round-trips, not only the default
    for (int s = 0; s < kParams[id].steps; ++s)
      ASSERT_TRUE(indexFromNorm(id, normFromIndex(id, s)) == s);
  }
}

void testNormEdges() {
  ASSERT_NEAR(normFromIndex(kBypass, 0), 0.0);
  ASSERT_NEAR(normFromIndex(kBypass, 1), 1.0);
  ASSERT_TRUE(indexFromNorm(kInputGain, -0.3) == 0);
  ASSERT_TRUE(indexFromNorm(kInputGain, 1.7) == kParams[kInputGain].steps - 1);
}

void testFrozenOrder() {
  ASSERT_TRUE(kBypass == 0);
  ASSERT_TRUE(kPush == kNumParams - 1);
  ASSERT_TRUE(static_cast<int>(kListenBand) > static_cast<int>(kNumParams));
  ASSERT_TRUE(static_cast<int>(kSetActive) > static_cast<int>(kListenBand));
  ASSERT_TRUE(kInputGain == 1 && kOversampling == 6 && kFineOn == 7);
  ASSERT_TRUE(kHpOn == kFineOn + 1);
  ASSERT_TRUE(kHsGain == kVibeOn - 1);
  ASSERT_TRUE(kMetalOn == kPush - 1);
  ASSERT_TRUE(std::strcmp(kParams[kBypass].name, "Bypass") == 0);
  ASSERT_TRUE(std::strcmp(kParams[kPush].name, "Push") == 0);
  ASSERT_TRUE(std::strcmp(kParams[kHsGain].name, "Hi Shelf Gain") == 0);
}

void testHpSideScaleSameIndexRange() {
  ASSERT_TRUE(Tables::kHpSideFreqs.size() == 6);
  ASSERT_TRUE(Tables::kHpFreqs.size() == 6);
  ASSERT_TRUE(kParams[kHpFreq].steps == 6);
  const int maxIdx = static_cast<int>(Tables::kHpFreqs.size()) - 1;
  for (int idx = 0; idx < kParams[kHpFreq].steps; ++idx) {
    const int side = hpIndexForScale(true, idx);
    ASSERT_TRUE(side >= 0 && side <= maxIdx);
    ASSERT_TRUE(side < static_cast<int>(Tables::kHpSideFreqs.size()));
    ASSERT_TRUE(hpIndexForScale(false, idx) == idx);
  }
  ASSERT_TRUE(hpIndexForScale(true, 99) == maxIdx);
  ASSERT_NEAR(hpFreqHz(false, 0), 0.0);
  ASSERT_NEAR(hpFreqHz(false, 1), 20.0);
  ASSERT_NEAR(hpFreqHz(true, 0), 0.0);
  ASSERT_NEAR(hpFreqHz(true, 1), 60.0);
  ASSERT_NEAR(hpFreqHz(true, 99), 200.0);
  ASSERT_NEAR(hpFreqHz(true, 5), 200.0);  // SIDE scale reaches 200 Hz
  ASSERT_NEAR(hpFreqHz(false, 4), 35.0);
  ASSERT_NEAR(hpFreqHz(false, 5), 40.0);
}

void testDefaultValues() {
  ASSERT_TRUE(kParams[kBypass].defaultIndex == 0);
  ASSERT_NEAR(valueOf(kInputGain, kParams[kInputGain].defaultIndex), 0.0);
  ASSERT_NEAR(valueOf(kOutputGain, kParams[kOutputGain].defaultIndex), 0.0);
  ASSERT_NEAR(valueOf(kInputGain, 0), -12.0);
  ASSERT_NEAR(valueOf(kInputGain, kParams[kInputGain].steps - 1), 12.0);
  ASSERT_NEAR(valueOf(kCeiling, kParams[kCeiling].defaultIndex), -0.3);
  ASSERT_NEAR(valueOf(kLoudness, kParams[kLoudness].defaultIndex), 0.0);
  ASSERT_NEAR(valueOf(kTarget, kParams[kTarget].defaultIndex), -14.0);
  ASSERT_TRUE(kParams[kOversampling].steps == 3 && kParams[kOversampling].defaultIndex == 1);
  ASSERT_TRUE(!isAutomatable(kOversampling) && isAutomatable(kBypass));

  ASSERT_NEAR(valueOf(kHpFreq, kParams[kHpFreq].defaultIndex), 0.0);  // Off
  ASSERT_NEAR(valueOf(kLsFreq, kParams[kLsFreq].defaultIndex), 100.0);
  ASSERT_NEAR(valueOf(kLmFreq, kParams[kLmFreq].defaultIndex), 250.0);
  ASSERT_NEAR(valueOf(kMidFreq, kParams[kMidFreq].defaultIndex), 1000.0);
  ASSERT_NEAR(valueOf(kHmFreq, kParams[kHmFreq].defaultIndex), 4000.0);
  ASSERT_NEAR(valueOf(kHsFreq, kParams[kHsFreq].defaultIndex), 12000.0);
  ASSERT_NEAR(valueOf(kMidGain, kParams[kMidGain].defaultIndex), 0.0);
  ASSERT_NEAR(valueOf(kMidQ, kParams[kMidQ].defaultIndex), 1.0);  // Medium
  ASSERT_TRUE(kParams[kMidMs].steps == 3 && kParams[kMidMs].defaultIndex == 0);  // ST

  ASSERT_NEAR(valueOf(kLowFreq, kParams[kLowFreq].defaultIndex), 60.0);
  ASSERT_NEAR(valueOf(kAirFreq, kParams[kAirFreq].defaultIndex), 12000.0);
  ASSERT_NEAR(valueOf(kTilt, kParams[kTilt].defaultIndex), 0.0);
  ASSERT_NEAR(valueOf(kTube, kParams[kTube].defaultIndex), 2.0);
  ASSERT_NEAR(valueOf(kLow, kParams[kLow].defaultIndex), 0.0);
  ASSERT_NEAR(valueOf(kAir, kParams[kAir].defaultIndex), 0.0);

  ASSERT_NEAR(valueOf(kGlueThresh, kParams[kGlueThresh].defaultIndex), -10.0);
  ASSERT_NEAR(valueOf(kGlueAttack, kParams[kGlueAttack].defaultIndex), 3.0);
  ASSERT_NEAR(valueOf(kGlueRelease, kParams[kGlueRelease].defaultIndex), 0.0);  // Auto
  ASSERT_NEAR(valueOf(kGlueRatio, kParams[kGlueRatio].defaultIndex), 2.0);
  ASSERT_NEAR(valueOf(kGlueMix, kParams[kGlueMix].defaultIndex), 50.0);
  ASSERT_NEAR(valueOf(kGlueScHpf, kParams[kGlueScHpf].defaultIndex), 60.0);
  ASSERT_NEAR(valueOf(kClipDrive, kParams[kClipDrive].defaultIndex), 2.0);
  ASSERT_NEAR(valueOf(kLimitThresh, kParams[kLimitThresh].defaultIndex), -2.0);
  ASSERT_TRUE(kParams[kLimitRelease].steps == 9 && kParams[kLimitRelease].defaultIndex == 4);
  ASSERT_TRUE(kParams[kLimitStyle].steps == 3 && kParams[kLimitStyle].defaultIndex == 0);
  ASSERT_TRUE(kParams[kFinalMode].steps == 2 && kParams[kFinalMode].defaultIndex == 0);
  ASSERT_NEAR(valueOf(kFinalEffect, kParams[kFinalEffect].defaultIndex), 50.0);
  ASSERT_NEAR(valueOf(kFinalCurve, kParams[kFinalCurve].defaultIndex), 25.0);
  ASSERT_NEAR(valueOf(kFinalDrive, kParams[kFinalDrive].defaultIndex), 2.0);
  ASSERT_NEAR(valueOf(kPush, kParams[kPush].defaultIndex), 2.0);
  ASSERT_TRUE(kParams[kMetalOn].defaultIndex == 0);
  ASSERT_TRUE(kParams[kDynLow].defaultIndex == 0 && kParams[kDynAir].defaultIndex == 0);
  ASSERT_TRUE(kParams[kGlueWarm].defaultIndex == 0);
  ASSERT_TRUE(kParams[kFineOn].defaultIndex == 1 && kParams[kVibeOn].defaultIndex == 1);
}

void testTableShapes() {
  ASSERT_TRUE(Tables::kLoMidFreqs.size() == 10 && Tables::kMidFreqs.size() == 10 &&
              Tables::kHiMidFreqs.size() == 10);
  ASSERT_NEAR(Tables::kLoMidFreqs.front(), 100.0);
  ASSERT_NEAR(Tables::kLoMidFreqs.back(), 800.0);
  ASSERT_NEAR(Tables::kMidFreqs.front(), 400.0);
  ASSERT_NEAR(Tables::kMidFreqs.back(), 3150.0);
  ASSERT_NEAR(Tables::kHiMidFreqs.front(), 1600.0);
  ASSERT_NEAR(Tables::kHiMidFreqs.back(), 12500.0);
  ASSERT_TRUE(Tables::kEqGains.size() == 17);
  ASSERT_NEAR(Tables::kEqGains.front(), -4.0);
  ASSERT_NEAR(Tables::kEqGains.back(), 4.0);
  ASSERT_TRUE(Tables::kIoGain.size() == 49);
  ASSERT_TRUE(Tables::kLimitThresh.front() == 0.0 && Tables::kLimitThresh.back() == -4.0);
  ASSERT_TRUE(Tables::kGlueReleaseS.back() == 0.0);  // Auto
  ASSERT_TRUE(Tables::kScHpfHz.front() == 0.0);      // Off
}

void testAmountScale() {
  ASSERT_TRUE(Tables::kAmounts.size() == 11);
  ASSERT_NEAR(Tables::kAmounts.front(), 0.0);
  ASSERT_NEAR(Tables::kAmounts.back(), 10.0);
  ASSERT_NEAR(Tables::kAmounts[7], 7.0);
  ASSERT_TRUE(kParams[kLow].steps == 11 && kParams[kAir].steps == 11 && kParams[kTube].steps == 11);
  ASSERT_TRUE(valueTable(kLow).data == Tables::kAmounts.data());
  ASSERT_TRUE(valueTable(kLoudness).size == 21);
  ASSERT_TRUE(valueTable(kLoudness).data != Tables::kAmounts.data());
}

void testTuningTable() {
  ASSERT_NEAR(Tuning::kLimitReleaseBaseMs[0], 50.0);
  ASSERT_NEAR(Tuning::kLimitReleaseBaseMs[1], 100.0);
  ASSERT_NEAR(Tuning::kLimitReleaseBaseMs[2], 250.0);
  ASSERT_NEAR(Tuning::kSetThresholdDb, -8.0);
  ASSERT_NEAR(Tuning::kMatchDbPerStep, 0.75);
  ASSERT_NEAR(Tuning::kLoudCurve.clipDb, 1.5);
  ASSERT_NEAR(Tuning::kLoudCurve.limitDb, -3.0);
  ASSERT_NEAR(Tuning::kLoudCurve.final, 1.0);
  ASSERT_TRUE(Tuning::kDynThresholdDb < 0.0);
  ASSERT_TRUE(Tuning::kDynAttackMs > 0.0 && Tuning::kDynReleaseMs > Tuning::kDynAttackMs);
}

} // namespace

int main() {
  RUN_TEST(testStepsMatchTables);
  RUN_TEST(testStepsAndDefaultsInRange);
  RUN_TEST(testNamesUnique);
  RUN_TEST(testDefaultsRoundTrip);
  RUN_TEST(testNormEdges);
  RUN_TEST(testFrozenOrder);
  RUN_TEST(testHpSideScaleSameIndexRange);
  RUN_TEST(testDefaultValues);
  RUN_TEST(testTableShapes);
  RUN_TEST(testAmountScale);
  RUN_TEST(testTuningTable);
  std::printf("%d passed, %d failed\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
