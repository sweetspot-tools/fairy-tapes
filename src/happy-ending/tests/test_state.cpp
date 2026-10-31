/**
 * Fairy Tapes · Happy Ending — Unit tests for the versioned plug-in state (pure, no SDK libs).
 * Run via CTest; build target: happy_ending_state_tests.
 */

#include "params.h"
#include "state.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

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

#define RUN_TEST(fn) do { \
  const int before = g_failed; \
  fn(); \
  if (g_failed == before) { ++g_passed; std::printf("PASS %s\n", #fn); } \
  else std::printf("FAIL %s\n", #fn); \
} while(0)

constexpr std::size_t kHeaderBytes = 4;
constexpr std::size_t kFullBytes = kHeaderBytes + sizeof(double) * kNumParams;

// Every parameter one step away from its default (wrapping), as a normalized value.
void nonDefaultNorms(double* norm) {
  for (int i = 0; i < kNumParams; ++i) {
    const auto id = static_cast<ParamID>(i);
    norm[i] = normFromIndex(id, (kParams[id].defaultIndex + 1) % kParams[id].steps);
  }
}

bool sameNorms(const double* a, const double* b) {
  return std::memcmp(a, b, sizeof(double) * kNumParams) == 0;
}

void testLoadDefaults() {
  double d[kNumParams];
  loadDefaults(d);
  for (int i = 0; i < kNumParams; ++i) {
    const auto id = static_cast<ParamID>(i);
    ASSERT_TRUE(d[i] == normFromIndex(id, kParams[id].defaultIndex));
    ASSERT_TRUE(indexFromNorm(id, d[i]) == kParams[id].defaultIndex);
  }
}

void testRoundTrip() {
  double in[kNumParams], out[kNumParams];
  nonDefaultNorms(in);
  for (int i = 0; i < kNumParams; ++i) {
    const auto id = static_cast<ParamID>(i);
    ASSERT_TRUE(indexFromNorm(id, in[i]) != kParams[id].defaultIndex);  // really not the default
  }
  std::vector<uint8_t> bytes;
  serializeState(in, bytes);
  ASSERT_TRUE(bytes.size() == kFullBytes);
  loadDefaults(out);
  ASSERT_TRUE(deserializeState(bytes.data(), bytes.size(), out));
  ASSERT_TRUE(sameNorms(in, out));
}

void testRoundTripKeepsOversamplingAndService() {
  // OVERSAMPLING is a project setting: it is stored (spec 9)
  double in[kNumParams], out[kNumParams];
  loadDefaults(in);
  in[kOversampling] = normFromIndex(kOversampling, 2);
  in[kCeiling] = normFromIndex(kCeiling, 3);
  std::vector<uint8_t> bytes;
  serializeState(in, bytes);
  ASSERT_TRUE(deserializeState(bytes.data(), bytes.size(), out));
  ASSERT_TRUE(indexFromNorm(kOversampling, out[kOversampling]) == 2);
  ASSERT_TRUE(indexFromNorm(kCeiling, out[kCeiling]) == 3);
}

void testRejectsUnknownVersion() {
  double in[kNumParams];
  nonDefaultNorms(in);
  std::vector<uint8_t> bytes;
  serializeState(in, bytes);
  for (uint32_t bad : {0u, 2u, 99u, 0xFFFFFFFFu}) {
    std::vector<uint8_t> b = bytes;
    for (int k = 0; k < 4; ++k) b[static_cast<std::size_t>(k)] = static_cast<uint8_t>(bad >> (8 * k));
    double out[kNumParams];
    loadDefaults(out);
    double before[kNumParams];
    std::memcpy(before, out, sizeof out);
    ASSERT_TRUE(!deserializeState(b.data(), b.size(), out));
    ASSERT_TRUE(sameNorms(before, out));  // untouched on refusal
  }
  double out[kNumParams];
  ASSERT_TRUE(!deserializeState(nullptr, 0, out));
  ASSERT_TRUE(!deserializeState(bytes.data(), 3, out));  // not even a version
}

void testListenAndSetNotStored() {
  // The blob is exactly the version and kNumParams values: nothing for the hidden session parameters,
  // whose IDs lie beyond the table.
  ASSERT_TRUE(static_cast<int>(kListenBand) >= kNumParams && static_cast<int>(kSetActive) >= kNumParams);
  double in[kNumParams];
  loadDefaults(in);
  std::vector<uint8_t> bytes;
  serializeState(in, bytes);
  ASSERT_TRUE(bytes.size() == kHeaderBytes + sizeof(double) * kNumParams);
  // The session parameters start at LISTEN off (-1) and SET off.
  ASSERT_TRUE(listenFromNorm(normFromListen(-1)) == -1);
  ASSERT_TRUE(kDefaultListenNorm == normFromListen(-1) && kDefaultSetNorm == 0.0);
  for (int band = -1; band <= 2; ++band) ASSERT_TRUE(listenFromNorm(normFromListen(band)) == band);
}

void testTruncatedStateKeepsDefaults() {
  // An older build had fewer parameters (the table only grows at the end): what is there loads,
  // the rest keeps its default.
  double in[kNumParams];
  nonDefaultNorms(in);
  std::vector<uint8_t> bytes;
  serializeState(in, bytes);
  const int kept = 10;
  double out[kNumParams], defaults[kNumParams];
  loadDefaults(defaults);
  loadDefaults(out);
  const std::size_t cut = kHeaderBytes + sizeof(double) * kept + 3;  // also cut inside a value
  ASSERT_TRUE(deserializeState(bytes.data(), cut, out));
  for (int i = 0; i < kNumParams; ++i) ASSERT_TRUE(out[i] == (i < kept ? in[i] : defaults[i]));

  // only the version: everything default
  for (int i = 0; i < kNumParams; ++i) out[i] = 0.123;
  ASSERT_TRUE(deserializeState(bytes.data(), kHeaderBytes, out));
  ASSERT_TRUE(sameNorms(defaults, out));
}

void testExtraBytesIgnoredAndValuesSanitized() {
  double in[kNumParams], out[kNumParams], defaults[kNumParams];
  loadDefaults(defaults);
  loadDefaults(in);
  in[kInputGain] = 7.5;                   // out of range: clamped to 1
  in[kOutputGain] = -3.0;                 // clamped to 0
  in[kCeiling] = std::nan("");            // not finite: that parameter keeps its default
  in[kLoudness] = std::numeric_limits<double>::infinity();
  std::vector<uint8_t> bytes;
  serializeState(in, bytes);
  bytes.insert(bytes.end(), 16, 0xAB);    // future data after the known layout
  ASSERT_TRUE(deserializeState(bytes.data(), bytes.size(), out));
  ASSERT_TRUE(out[kInputGain] == 1.0);
  ASSERT_TRUE(out[kOutputGain] == 0.0);
  ASSERT_TRUE(out[kCeiling] == defaults[kCeiling]);
  ASSERT_TRUE(out[kLoudness] == defaults[kLoudness]);
  for (int i = 0; i < kNumParams; ++i) ASSERT_TRUE(std::isfinite(out[i]) && out[i] >= 0.0 && out[i] <= 1.0);
}

void testLayoutIsLittleEndianVersionOne() {
  double in[kNumParams];
  loadDefaults(in);
  std::vector<uint8_t> bytes;
  serializeState(in, bytes);
  ASSERT_TRUE(kStateVersion == 1);
  ASSERT_TRUE(bytes[0] == 1 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 0);
  double first;
  std::memcpy(&first, bytes.data() + kHeaderBytes, sizeof first);  // little-endian host assumed by the test only
  ASSERT_TRUE(first == in[0]);
}

} // namespace

int main() {
  RUN_TEST(testLoadDefaults);
  RUN_TEST(testRoundTrip);
  RUN_TEST(testRoundTripKeepsOversamplingAndService);
  RUN_TEST(testRejectsUnknownVersion);
  RUN_TEST(testListenAndSetNotStored);
  RUN_TEST(testTruncatedStateKeepsDefaults);
  RUN_TEST(testExtraBytesIgnoredAndValuesSanitized);
  RUN_TEST(testLayoutIsLittleEndianVersionOne);
  std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
  return g_failed ? 1 : 0;
}
