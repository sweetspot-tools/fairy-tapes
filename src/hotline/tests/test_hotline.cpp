/**
 * Fairy Tapes · Hotline — Unit tests for params and state.
 * Run via CTest; build target: hotline_tests.
 */

#include "devices.h"
#include "params.h"
#include "state.h"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <set>
#include <string>
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

bool sameParams(const BlockParams& a, const BlockParams& b) {
  return a.hpHz == b.hpHz && a.lpHz == b.lpHz && a.drive == b.drive && a.coneHz == b.coneHz &&
         a.coneGainDb == b.coneGainDb && a.coneQ == b.coneQ && a.hornGainDb == b.hornGainDb &&
         a.crushRateHz == b.crushRateHz && a.crushBits == b.crushBits && a.compRatio == b.compRatio &&
         a.squelchDb == b.squelchDb && a.noiseDb == b.noiseDb && a.room == b.room;
}

void morphZeroIsNeutral() {
  for (int i = 0; i < kNumDevices; ++i)
    ASSERT_TRUE(sameParams(morphParams(kDevices[i].p, 0.0), neutralParams()));
}

void morphOneIsDevice() {
  for (int i = 0; i < kNumDevices; ++i)
    ASSERT_TRUE(sameParams(morphParams(kDevices[i].p, 1.0), kDevices[i].p));
}

void morphLogMidpoint() {
  const BlockParams m = morphParams(kDevices[1].p, 0.5);  // Telephone
  ASSERT_APPROX(m.hpHz, std::sqrt(20.0 * 300.0));
  ASSERT_APPROX(m.lpHz, std::sqrt(20000.0 * 3400.0));
  ASSERT_APPROX(m.coneGainDb, 1.5);   // dB are linear
  ASSERT_APPROX(m.noiseDb, -87.0);
  ASSERT_APPROX(m.drive, 0.125);      // linear
}

void morphClampsAmount() {
  for (int i = 0; i < kNumDevices; ++i) {
    ASSERT_TRUE(sameParams(morphParams(kDevices[i].p, -1.0), neutralParams()));
    ASSERT_TRUE(sameParams(morphParams(kDevices[i].p, 2.0), kDevices[i].p));
    ASSERT_TRUE(sameParams(morphParams(kDevices[i].p, std::numeric_limits<double>::quiet_NaN()), neutralParams()));
  }
}

void deviceTableIsConsistent() {
  std::set<std::string> buttons, names;
  for (int i = 0; i < kNumDevices; ++i) {
    const DeviceSpec& d = kDevices[i];
    ASSERT_TRUE(d.button && d.button[0] && d.name && d.name[0]);
    buttons.insert(d.button);
    names.insert(d.name);
    const BlockParams& p = d.p;
    ASSERT_TRUE(p.hpHz < p.lpHz);
    const double all[] = {p.hpHz, p.lpHz, p.drive, p.coneHz, p.coneGainDb, p.coneQ, p.hornGainDb,
                          p.crushRateHz, p.crushBits, p.compRatio, p.squelchDb, p.noiseDb, p.room};
    for (double v : all) ASSERT_TRUE(std::isfinite(v));
  }
  ASSERT_TRUE(buttons.size() == static_cast<size_t>(kNumDevices));
  ASSERT_TRUE(names.size() == static_cast<size_t>(kNumDevices));
  ASSERT_TRUE(std::string(kDevices[3].button) == "AM RADIO");
  ASSERT_TRUE(std::string(kDevices[2].name) == "Walkie-talkie");
}

void describeTelephone() {
  const std::string s = describeDevice(1);
  ASSERT_TRUE(s.find("300 Hz") != std::string::npos);
  ASSERT_TRUE(s.find("3.4 kHz") != std::string::npos);
  ASSERT_TRUE(s.find("HIGH PASS") != std::string::npos);
  ASSERT_TRUE(s.find("HORN") == std::string::npos);
  ASSERT_TRUE(s.find("ROOM") == std::string::npos);
}

void describeCoversAllDevices() {
  for (int i = 0; i < kNumDevices; ++i) ASSERT_TRUE(!describeDevice(i).empty());
  ASSERT_TRUE(describeDevice(0).find("HORN") != std::string::npos);
  ASSERT_TRUE(describeDevice(-1).empty());           // out-of-range must not crash
  ASSERT_TRUE(describeDevice(kNumDevices).empty());
}

bool hasLine(const std::string& text, const std::string& line) {
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t e = text.find('\n', pos);
    if (e == std::string::npos) e = text.size();
    if (text.compare(pos, e - pos, line) == 0 && e - pos == line.size()) return true;
    pos = e + 1;
  }
  return false;
}

void describeFrequenciesAreExact() {
  const std::string w = describeDevice(2);  // Walkie-talkie
  ASSERT_TRUE(w.find("11.025 kHz") != std::string::npos);
  ASSERT_TRUE(hasLine(w, "CRUSH 11.025 kHz 8 bit"));
  ASSERT_TRUE(hasLine(w, "COMPANDER 6:1 squelch -50 dB"));
  ASSERT_TRUE(hasLine(describeDevice(1), "CRUSH 8 kHz 10 bit"));
  ASSERT_TRUE(hasLine(describeDevice(1), "LOW PASS 3.4 kHz"));
  ASSERT_TRUE(hasLine(describeDevice(3), "CONE 900 Hz +2 dB Q 0.8"));
}

void describeShowsConeQ() {
  ASSERT_TRUE(describeDevice(0).find("CONE 1.8 kHz +6 dB Q 2") != std::string::npos);
  ASSERT_TRUE(describeDevice(3).find("Q 0.8") != std::string::npos);
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
  RUN_TEST(morphZeroIsNeutral);
  RUN_TEST(morphOneIsDevice);
  RUN_TEST(morphLogMidpoint);
  RUN_TEST(morphClampsAmount);
  RUN_TEST(deviceTableIsConsistent);
  RUN_TEST(describeTelephone);
  RUN_TEST(describeCoversAllDevices);
  RUN_TEST(describeFrequenciesAreExact);
  RUN_TEST(describeShowsConeQ);
  std::printf("%d tests run, %d failed\n", g_passed + g_failed, g_failed);
  return g_failed ? 1 : 0;
}
