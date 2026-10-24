/**
 * Fairy Tapes · Hotline — Unit tests for params and state.
 * Run via CTest; build target: hotline_tests.
 */

#include "params.h"
#include "state.h"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
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

namespace {

constexpr double kEps = 1e-6;
int g_failed = 0;
int g_passed = 0;

bool approx(double a, double b, double eps = kEps) { return std::fabs(a - b) <= eps; }

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

using namespace FairyTapes::Hotline;

void paramIdsAreFrozen() {
  ASSERT_TRUE(kBypass == 0);
  ASSERT_TRUE(kDevice == 1);
  ASSERT_TRUE(kMorph == 2);
  ASSERT_TRUE(kGain == 3);
  ASSERT_TRUE(kMono == 4);
  ASSERT_TRUE(kDrive == 5);
  ASSERT_TRUE(kTone == 6);
  ASSERT_TRUE(kNoise == 7);
  ASSERT_TRUE(kRoom == 8);
  ASSERT_TRUE(kMix == 9);
  ASSERT_TRUE(kCharacterIn == 10);
  ASSERT_TRUE(kNumParams == 11);
  ASSERT_TRUE(kNumDevices == 6);
  ASSERT_TRUE(kMeterBase == 1000);
  ASSERT_TRUE(kMeterInL == 1000 && kClipClear == 1007 && kNumDisplayIDs == 1008);
}

void deviceIndexRoundsOverSixEntries() {
  ASSERT_TRUE(deviceIndexFromNorm(0.0) == 0);
  ASSERT_TRUE(deviceIndexFromNorm(0.5) == 3);
  ASSERT_TRUE(deviceIndexFromNorm(1.0) == 5);
  ASSERT_TRUE(deviceIndexFromNorm(-1.0) == 0);
  ASSERT_TRUE(deviceIndexFromNorm(2.0) == 5);
}

void mappingsHitSpecValues() {
  ASSERT_APPROX(gainDbFromNorm(0.0), -20.0);
  ASSERT_APPROX(gainDbFromNorm(0.5), 0.0);
  ASSERT_APPROX(gainDbFromNorm(1.0), 20.0);
  ASSERT_APPROX(amountFromNorm(0.0), 0.0);
  ASSERT_APPROX(amountFromNorm(0.5), 1.0);
  ASSERT_APPROX(amountFromNorm(1.0), 2.0);
  ASSERT_APPROX(toneLpOctavesFromNorm(1.0), 1.0);
  ASSERT_APPROX(toneLpOctavesFromNorm(0.5), 0.0);
  ASSERT_APPROX(toneLpOctavesFromNorm(0.0), -1.0);
  ASSERT_APPROX(toneHpOctavesFromNorm(1.0), -0.5);
  ASSERT_APPROX(toneHpOctavesFromNorm(0.5), 0.0);
  ASSERT_APPROX(toneHpOctavesFromNorm(0.0), 0.5);
}

void defaultsMatchPlan() {
  ASSERT_APPROX(kDefaultDevice, 0.0);
  ASSERT_APPROX(kDefaultMorph, 1.0);
  ASSERT_APPROX(kDefaultGain, 0.5);
  ASSERT_APPROX(kDefaultMono, 0.0);
  ASSERT_APPROX(kDefaultDrive, 0.5);
  ASSERT_APPROX(kDefaultTone, 0.5);
  ASSERT_APPROX(kDefaultNoise, 0.5);
  ASSERT_APPROX(kDefaultRoom, 0.5);
  ASSERT_APPROX(kDefaultMix, 1.0);
  ASSERT_APPROX(kDefaultCharacterIn, 1.0);
  ASSERT_APPROX(kDefaultBypass, 0.0);
}

void stateRoundTrip() {
  double in[kNumParams];
  for (int i = 0; i < (int)kNumParams; ++i) in[i] = (i + 1) / 12.0;
  std::vector<uint8_t> blob;
  serializeState(in, blob);
  ASSERT_TRUE(blob.size() == kStateSize);
  double out[kNumParams];
  for (double& v : out) v = -7.0;
  ASSERT_TRUE(deserializeState(blob.data(), blob.size(), out));
  for (int i = 0; i < (int)kNumParams; ++i) ASSERT_APPROX(out[i], in[i]);
}

void stateRejectsUnknownVersionShortDataAndNaN() {
  double in[kNumParams];
  for (double& v : in) v = 0.25;
  std::vector<uint8_t> blob;
  serializeState(in, blob);

  double out[kNumParams];
  auto fill = [&] { for (int i = 0; i < (int)kNumParams; ++i) out[i] = 0.125 + i; };
  auto intact = [&] {
    for (int i = 0; i < (int)kNumParams; ++i)
      if (out[i] != 0.125 + i) return false;
    return true;
  };

  fill();
  std::vector<uint8_t> bad = blob;
  bad[0] = 99;  // unknown version
  ASSERT_TRUE(!deserializeState(bad.data(), bad.size(), out));
  ASSERT_TRUE(intact());

  fill();
  ASSERT_TRUE(!deserializeState(blob.data(), blob.size() - 1, out));
  ASSERT_TRUE(intact());
  ASSERT_TRUE(!deserializeState(blob.data(), 0, out));
  ASSERT_TRUE(!deserializeState(nullptr, 0, out));
  ASSERT_TRUE(intact());

  double withNaN[kNumParams];
  for (double& v : withNaN) v = 0.5;
  withNaN[4] = std::numeric_limits<double>::quiet_NaN();
  std::vector<uint8_t> nanBlob;
  serializeState(withNaN, nanBlob);
  fill();
  ASSERT_TRUE(!deserializeState(nanBlob.data(), nanBlob.size(), out));
  ASSERT_TRUE(intact());
}

void stateClampsOutOfRange() {
  double in[kNumParams];
  for (int i = 0; i < (int)kNumParams; ++i) in[i] = (i % 2 == 0) ? 2.0 : -1.0;
  std::vector<uint8_t> blob;
  serializeState(in, blob);
  double out[kNumParams];
  ASSERT_TRUE(deserializeState(blob.data(), blob.size(), out));
  for (int i = 0; i < (int)kNumParams; ++i) ASSERT_APPROX(out[i], (i % 2 == 0) ? 1.0 : 0.0);
}

}  // namespace

int main() {
  RUN_TEST(paramIdsAreFrozen);
  RUN_TEST(deviceIndexRoundsOverSixEntries);
  RUN_TEST(mappingsHitSpecValues);
  RUN_TEST(defaultsMatchPlan);
  RUN_TEST(stateRoundTrip);
  RUN_TEST(stateRejectsUnknownVersionShortDataAndNaN);
  RUN_TEST(stateClampsOutOfRange);
  std::printf("%d tests run, %d failed\n", g_passed + g_failed, g_failed);
  return g_failed ? 1 : 0;
}
