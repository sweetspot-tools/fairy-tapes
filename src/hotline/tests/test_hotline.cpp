/**
 * Fairy Tapes · Hotline — Unit tests for params, state, devices, DSP blocks and engine.
 * Run via CTest; build target: hotline_tests.
 */

#include "devices.h"
#include "engine.h"
#include "dsp/band.h"
#include "dsp/compander.h"
#include "dsp/cone.h"
#include "dsp/crush.h"
#include "dsp/drive.h"
#include "dsp/noise.h"
#include "dsp/room.h"
#include "params.h"
#include "state.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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


// ---- DSP blocks (Task 3) ----
namespace D = FairyTapes::Hotline::DSP;

// Steady-state peak of a sine through f(x); skips the first 0.5 s.
template <class F>
double sinePeak(double fs, double hz, double amp, F f) {
  constexpr double kPi = 3.14159265358979323846;
  const int n = (int)(fs * 1.5), skip = (int)(fs * 0.5);
  double pk = 0;
  for (int i = 0; i < n; ++i) {
    const float y = f((float)(amp * std::sin(2 * kPi * hz * i / fs)));
    if (i >= skip) pk = std::max(pk, (double)std::fabs(y));
  }
  return pk;
}
double toDb(double r) { return 20.0 * std::log10(r); }

struct Lcg {
  uint32_t s = 12345;
  float next() { s = s * 1664525u + 1013904223u; return (float)((s >> 8) / 8388608.0 - 1.0); }
};

void bandTelephoneAttenuates() {
  const double fs = 48000;
  D::Band b; b.prepare(fs); b.set(300, 3400);
  auto run = [&](double hz) { b.clear(); return toDb(sinePeak(fs, hz, 0.5, [&](float x) { return b.process(x); }) / 0.5); };
  ASSERT_TRUE(run(100) <= -18.0);
  ASSERT_TRUE(run(8000) <= -18.0);
  ASSERT_TRUE(std::fabs(run(1000)) <= 1.5);
}

void bandNeutralIsExactPassthrough() {
  D::Band b; b.prepare(48000); b.set(20, 20000);
  ASSERT_TRUE(b.neutral());
  Lcg r;
  for (int i = 0; i < 1000; ++i) { const float x = r.next(); ASSERT_TRUE(b.process(x) == x); }
  b.set(300, 3400);
  ASSERT_TRUE(!b.neutral());
}

void bandClampsBelowNyquist() {
  D::Band b; b.prepare(8000); b.set(5, 7000);
  Lcg r;
  for (int i = 0; i < 8000; ++i) ASSERT_TRUE(std::isfinite(b.process(r.next())));
}

// Runs a 1 kHz sine through a filter that is changed mid-stream (`change`) and through an
// untouched twin (`ref`); returns the max deviation of the two over the 100 samples after the
// change. Small if the change preserved filter state, large if it reset it.
template <class F, class R, class C>
double deviationAfterChange(double fs, F f, R ref, C change) {
  constexpr double kPi = 3.14159265358979323846;
  const int n0 = (int)(fs * 0.5) + 3, n = n0 + 100;
  double dev = 0;
  for (int i = 0; i < n; ++i) {
    if (i == n0) change();
    const float x = (float)std::sin(2 * kPi * 1000.0 * i / fs);
    const float y = f(x), r = ref(x);
    if (i >= n0) dev = std::max(dev, (double)std::fabs(y - r));
  }
  return dev;
}

void bandSetKeepsState() {
  const double fs = 48000;
  D::Band ref; ref.prepare(fs); ref.set(300, 3400);
  D::Band b; b.prepare(fs); b.set(300, 3400);
  const double keep = deviationAfterChange(fs, [&](float x) { return b.process(x); },
                                           [&](float x) { return ref.process(x); }, [&] { b.set(305, 3450); });
  D::Band ref2; ref2.prepare(fs); ref2.set(300, 3400);
  D::Band c; c.prepare(fs); c.set(300, 3400);
  const double cleared = deviationAfterChange(fs, [&](float x) { return c.process(x); },
                                              [&](float x) { return ref2.process(x); }, [&] { c.set(305, 3450); c.clear(); });
  ASSERT_TRUE(keep < 0.05);
  ASSERT_TRUE(cleared > 4 * keep && cleared > 0.1);  // sensitivity: clearing is detected
}

void coneSetKeepsState() {
  const double fs = 48000;
  D::Cone ref; ref.prepare(fs); ref.set(1000, 6, 2, 0);
  D::Cone a; a.prepare(fs); a.set(1000, 6, 2, 0);
  const double keep = deviationAfterChange(fs, [&](float x) { return a.process(x); },
                                           [&](float x) { return ref.process(x); }, [&] { a.set(1000, 5.5, 2, 0); });
  D::Cone ref2; ref2.prepare(fs); ref2.set(1000, 6, 2, 0);
  D::Cone c; c.prepare(fs); c.set(1000, 6, 2, 0);
  const double cleared = deviationAfterChange(fs, [&](float x) { return c.process(x); },
                                              [&](float x) { return ref2.process(x); }, [&] { c.set(1000, 5.5, 2, 0); c.clear(); });
  ASSERT_TRUE(keep < 0.1);
  ASSERT_TRUE(cleared > 4 * keep && cleared > 0.1);
}

// While neutral, process() bypasses the filters; leaving neutral must not replay stale state.
template <class F>
void feedLoud(F f) {
  constexpr double kPi = 3.14159265358979323846;
  for (int i = 0; i < 4800; ++i) f((float)(std::sin(2 * kPi * 1000.0 * i / 48000.0)));
}

void bandNoTransientAfterNeutral() {
  constexpr double kPi = 3.14159265358979323846;
  D::Band b; b.prepare(48000); b.set(300, 3400);
  feedLoud([&](float x) { return b.process(x); });     // leaves loud state in the filters
  b.set(20, 20000);                                     // neutral: bypass, state frozen
  feedLoud([&](float x) { return b.process(x); });
  b.set(300, 3400);
  double pk = 0;
  for (int i = 0; i < 200; ++i)
    pk = std::max(pk, (double)std::fabs(b.process((float)(0.1 * std::sin(2 * kPi * 5000.0 * i / 48000.0)))));
  ASSERT_TRUE(pk <= 0.1 * 1.5);  // 5 kHz sits in the stopband region edge; no 1.0-scale ring
}

void coneNoTransientAfterNeutral() {
  D::Cone c; c.prepare(48000); c.set(1800, 6, 2, 0);
  feedLoud([&](float x) { return c.process(x); });
  c.set(1800, 0, 2, 0);
  feedLoud([&](float x) { return c.process(x); });
  c.set(1800, 6, 2, 0);
  double pk = 0;
  for (int i = 0; i < 200; ++i) pk = std::max(pk, (double)std::fabs(c.process(0.f)));
  ASSERT_TRUE(pk <= 1e-6);  // silence in, silence out: any stale state would ring here
}

void driveIsContinuousNearZero() {
  for (int i = -100; i <= 100; ++i) {
    const float x = i * 0.01f;
    ASSERT_TRUE(std::fabs(D::shape(x, 1e-4f) - x) < 1e-3);
  }
  float prev = D::shape(0.9f, 0.f);
  for (int i = 1; i <= 200; ++i) {
    const float y = D::shape(0.9f, i * 0.001f);
    ASSERT_TRUE(std::fabs(y - prev) <= 0.01);
    prev = y;
  }
  for (float d : {0.1f, 0.5f, 0.99f, 1.f})
    for (int i = -400; i <= 400; ++i) ASSERT_TRUE(std::fabs(D::shape(i * 0.01f, d)) <= 1.05f);
}

void driveZeroIsIdentity() {
  for (float x : {-3.f, -1.f, -0.25f, 0.f, 0.3f, 1.f, 2.5f}) ASSERT_TRUE(D::shape(x, 0.f) == x);
}

void driveIsBoundedAndAsymmetric() {
  for (int i = -400; i <= 400; ++i) ASSERT_TRUE(std::fabs(D::shape(i * 0.01f, 0.5f)) <= 1.05f);
  double pos = 0, neg = 0;
  constexpr double kPi = 3.14159265358979323846;
  for (int i = 0; i < 48; ++i) {
    const float y = D::shape((float)std::sin(2 * kPi * i / 48.0), 0.5f);
    pos = std::max(pos, (double)y); neg = std::max(neg, (double)-y);
  }
  ASSERT_TRUE(std::fabs(pos - neg) > 0.01);
  for (float d : {0.f, 0.5f, 1.f}) ASSERT_TRUE(std::isfinite(D::shape(1e6f, d)) && std::isfinite(D::shape(-1e6f, d)));
}

void coneBoostsAtCenter() {
  const double fs = 48000;
  D::Cone c; c.prepare(fs); c.set(1800, 6, 2, 0);
  ASSERT_TRUE(!c.neutral());
  const double g = toDb(sinePeak(fs, 1800, 0.1, [&](float x) { return c.process(x); }) / 0.1);
  ASSERT_TRUE(std::fabs(g - 6.0) <= 1.0);
}

void coneNeutralIsExactPassthrough() {
  D::Cone c; c.prepare(48000); c.set(1800, 0, 2, 0);
  ASSERT_TRUE(c.neutral());
  Lcg r;
  for (int i = 0; i < 1000; ++i) { const float x = r.next(); ASSERT_TRUE(c.process(x) == x); }
}

void blocksStableAtExtremeSampleRates() {
  for (double fs : {8000.0, 192000.0}) {
    D::Band b; b.prepare(fs); b.set(10, 20000);
    D::Band b2; b2.prepare(fs); b2.set(2000, 100);
    D::Cone c; c.prepare(fs); c.set(20000, 12, 10, 12);
    D::Cone c2; c2.prepare(fs); c2.set(20, -12, 0.1, -12);
    Lcg r;
    for (int i = 0; i < (int)fs; ++i) {
      const float x = r.next();
      ASSERT_TRUE(std::isfinite(b.process(x)));
      ASSERT_TRUE(std::isfinite(b2.process(x)));
      ASSERT_TRUE(std::isfinite(c.process(D::shape(x, 1.f))));
      ASSERT_TRUE(std::isfinite(c2.process(x)));
    }
  }
}


// ---- DSP blocks (Task 4): crush, compander, noise, room ----
constexpr double kPiT = 3.14159265358979323846;
const float kNaN = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();

void crushOffIsExactPassthrough() {
  D::Crush c; c.prepare(48000); c.set(192000, 24);
  ASSERT_TRUE(c.neutral());
  Lcg r;
  for (int i = 0; i < 1000; ++i) { const float x = r.next(); ASSERT_TRUE(c.process(x) == x); }
  c.set(48000, 24);  // rate == fs is neutral too
  ASSERT_TRUE(c.neutral());
  for (int i = 0; i < 1000; ++i) { const float x = r.next(); ASSERT_TRUE(c.process(x) == x); }
  c.set(8000, 24);
  ASSERT_TRUE(!c.neutral());
  c.set(48000, 8);
  ASSERT_TRUE(!c.neutral());
}

void crushQuantizes() {
  D::Crush c; c.prepare(48000); c.set(48000, 8);
  std::set<float> seen;
  for (int i = 0; i < 4096; ++i) seen.insert(c.process((float)(-1.0 + 2.0 * i / 4096.0)));
  ASSERT_TRUE(seen.size() <= 256);
  ASSERT_TRUE(seen.size() >= 200);  // and it really uses the levels
}

void crushHoldsSamples() {
  D::Crush c; c.prepare(48000); c.set(12000, 24);  // fs/4
  Lcg r;
  float held = 0;
  for (int i = 0; i < 400; ++i) {
    const float x = r.next(), y = c.process(x);
    if (i % 4 == 0) { held = y; ASSERT_TRUE(y == x); }  // 24 bit: a new sample is taken as is
    else ASSERT_TRUE(y == held);
  }
}

void crushIsContinuousTowardNeutral() {
  // bits -> 24 at rate == fs: error bounded by one quantiser step 2^(1-bits)
  for (double bits : {8.0, 12.0, 16.0, 20.0, 23.0, 23.9}) {
    D::Crush c; c.prepare(48000); c.set(48000, bits);
    double err = 0;
    for (int i = 0; i < 4800; ++i) {
      const float x = (float)(0.9 * std::sin(2 * kPiT * 440.0 * i / 48000.0));
      err = std::max(err, (double)std::fabs(c.process(x) - x));
    }
    ASSERT_TRUE(err <= std::ldexp(1.0, 1 - (int)std::ceil(bits)) + 1e-7);
  }
  // rate -> fs at 24 bit: a held sample repeats at most every 1/(1-ratio) samples; for a
  // 100 Hz sine at 0.5 the repeat error is at most one sample step, 0.5*2*pi*100/48000 = 0.0065
  for (double ratio : {0.9, 0.99, 0.999, 0.9999}) {
    D::Crush c; c.prepare(48000); c.set(48000 * ratio, 24);
    double err = 0;
    for (int i = 0; i < 48000; ++i) {
      const float x = (float)(0.5 * std::sin(2 * kPiT * 100.0 * i / 48000.0));
      err = std::max(err, (double)std::fabs(c.process(x) - x));
    }
    ASSERT_TRUE(err <= 0.01);
  }
}

void crushNoStaleHoldAfterNeutral() {
  D::Crush c; c.prepare(48000); c.set(12000, 24);
  for (int i = 0; i < 4802; ++i) c.process((float)std::sin(2 * kPiT * 1000.0 * i / 48000.0));  // loud hold
  c.set(192000, 24);                                    // neutral: process() bypasses the hold
  for (int i = 0; i < 4800; ++i) c.process((float)std::sin(2 * kPiT * 1000.0 * i / 48000.0));
  c.set(12000, 24);                                     // back to active: hold must start fresh
  ASSERT_TRUE(c.process(0.f) == 0.f);
  ASSERT_TRUE(c.process(0.f) == 0.f);
  // active -> active keeps the hold (no spurious clear)
  D::Crush d; d.prepare(48000); d.set(12000, 24);
  d.process(0.5f); d.process(0.25f);                    // hold = 0.5, phase mid-group
  d.set(12000, 24);
  ASSERT_TRUE(d.process(0.125f) == 0.5f);
}

void crushRejectsNonFinite() {
  D::Crush c; c.prepare(48000); c.set(12000, 8);
  D::Crush ref; ref.prepare(48000); ref.set(12000, 8);
  c.set((double)kNaN, (double)kNaN);                   // ignored
  c.set(std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity());
  Lcg r;
  for (int i = 0; i < 200; ++i) { const float x = r.next(); ASSERT_TRUE(c.process(x) == ref.process(x)); }
  for (float bad : {kNaN, kInf, -kInf}) ASSERT_TRUE(std::isfinite(c.process(bad)));
  for (int i = 0; i < 100; ++i) ASSERT_TRUE(std::isfinite(c.process(r.next())));
  // a NaN sampled into the hold must not stay there
  D::Crush q; q.prepare(48000); q.set(1000, 8);
  q.process(kNaN);
  for (int i = 0; i < 200; ++i) ASSERT_TRUE(std::isfinite(q.process(0.3f)));
}

void companderIdentityAtRatioOne() {
  D::Compander c; c.prepare(48000); c.set(1.0, -120.0);
  ASSERT_TRUE(c.neutral());
  Lcg r;
  for (int i = 0; i < 1000; ++i) { const float x = r.next(); ASSERT_TRUE(c.process(x) == x); }
  c.set(0.5, -130.0);  // below 1 / below -120 is still neutral
  ASSERT_TRUE(c.neutral());
  c.set(6.0, -120.0);
  ASSERT_TRUE(!c.neutral());
  c.set(1.0, -50.0);
  ASSERT_TRUE(!c.neutral());
}

void companderReducesLoudSignal() {
  const double fs = 48000, amp = std::pow(10.0, -6.0 / 20.0);
  D::Compander a; a.prepare(fs); a.set(6.0, -120.0);
  D::Compander b; b.prepare(fs); b.set(1.0, -120.0);
  const double pa = sinePeak(fs, 1000, amp, [&](float x) { return a.process(x); });
  const double pb = sinePeak(fs, 1000, amp, [&](float x) { return b.process(x); });
  ASSERT_TRUE(toDb(pa / pb) <= -6.0);
}

void companderSquelchesQuiet() {
  const double fs = 48000;
  D::Compander c; c.prepare(fs); c.set(1.0, -50.0);
  const double amp = std::pow(10.0, -70.0 / 20.0);
  ASSERT_TRUE(toDb(sinePeak(fs, 1000, amp, [&](float x) { return c.process(x); }) / amp) <= -20.0);
  D::Compander d; d.prepare(fs); d.set(1.0, -50.0);
  const double loud = std::pow(10.0, -30.0 / 20.0);
  ASSERT_TRUE(std::fabs(toDb(sinePeak(fs, 1000, loud, [&](float x) { return d.process(x); }) / loud)) <= 1.0);
}

void companderIsContinuousTowardNeutral() {
  // ratio -> 1: gain reduction is at most 24 dB * (1 - 1/R) < 3*(R-1) in amplitude for a 0 dB sine
  for (double R : {1.0001, 1.001, 1.01, 1.1}) {
    D::Compander c; c.prepare(48000); c.set(R, -120.0);
    double err = 0;
    for (int i = 0; i < 48000; ++i) {
      const float x = (float)std::sin(2 * kPiT * 1000.0 * i / 48000.0);
      err = std::max(err, (double)std::fabs(c.process(x) - x));
    }
    ASSERT_TRUE(err <= 3.0 * (R - 1.0));
  }
  // squelch -> -120: a -60 dB signal is far above any gate with squelch <= -100 (gain exactly 1)
  for (double sq : {-119.0, -110.0, -100.0}) {
    D::Compander c; c.prepare(48000); c.set(1.0, sq);
    double err = 0;
    for (int i = 0; i < 48000; ++i) {
      const float x = (float)(0.001 * std::sin(2 * kPiT * 1000.0 * i / 48000.0));
      if (i > 4800) err = std::max(err, (double)std::fabs(c.process(x) - x)); else c.process(x);
    }
    ASSERT_TRUE(err <= 1e-9);
  }
}

void companderNoTransientAfterNeutral() {
  D::Compander c; c.prepare(48000); c.set(6.0, -120.0);
  feedLoud([&](float x) { return c.process(x); });     // envelope tracks a 0 dB signal
  c.set(1.0, -120.0);                                   // neutral: process() bypasses the envelope
  feedLoud([&](float x) { return c.process(x); });
  c.set(6.0, -120.0);
  // quiet signal below the -24 dB threshold: a fresh envelope leaves it alone, a stale one
  // would keep compressing it for the release time
  double err = 0;
  for (int i = 0; i < 480; ++i) {
    const float x = (float)(0.001 * std::sin(2 * kPiT * 1000.0 * i / 48000.0));
    err = std::max(err, (double)std::fabs(c.process(x) - x));
  }
  ASSERT_TRUE(err <= 1e-6);
  // active -> active keeps the envelope (no clear): compression carries on across a set()
  D::Compander d; d.prepare(48000); d.set(6.0, -120.0);
  feedLoud([&](float x) { return d.process(x); });
  d.set(6.0, -120.0);
  ASSERT_TRUE(std::fabs(d.process(0.001f) - 0.001f) > 1e-5);
}

void companderRejectsNonFinite() {
  D::Compander c; c.prepare(48000); c.set(6.0, -50.0);
  D::Compander ref; ref.prepare(48000); ref.set(6.0, -50.0);
  c.set((double)kNaN, (double)kNaN);
  c.set(std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity());
  Lcg r;
  for (int i = 0; i < 200; ++i) { const float x = r.next(); ASSERT_TRUE(c.process(x) == ref.process(x)); }
  for (float bad : {kNaN, kInf, -kInf}) ASSERT_TRUE(std::isfinite(c.process(bad)));
  for (int i = 0; i < 2000; ++i) ASSERT_TRUE(std::isfinite(c.process(r.next())));
  // the envelope must have recovered: a loud signal is compressed again
  D::Compander a; a.prepare(48000); a.set(6.0, -120.0);
  a.process(kNaN);
  const double pk = sinePeak(48000, 1000, 0.5, [&](float x) { return a.process(x); });
  ASSERT_TRUE(pk < 0.5 && pk > 0.0);
}

void noiseIsDeterministicPerSeed() {
  D::Noise a, b, c;
  a.prepare(48000, 7); b.prepare(48000, 7); c.prepare(48000, 8);
  a.set(-30); b.set(-30); c.set(-30);
  bool diff = false;
  for (int i = 0; i < 4800; ++i) {
    const float x = a.next(), y = b.next(), z = c.next();
    ASSERT_TRUE(x == y);
    if (x != z) diff = true;
  }
  ASSERT_TRUE(diff);
  D::Noise z0; z0.prepare(48000, 0); z0.set(-30);  // seed 0 must not lock the xorshift at 0
  double s = 0;
  for (int i = 0; i < 4800; ++i) s += std::fabs(z0.next());
  ASSERT_TRUE(s > 0.0);
}

double noiseRmsDb(double fs, uint32_t seed, double level) {
  D::Noise n; n.prepare(fs, seed); n.set(level);
  const int len = (int)fs;
  double acc = 0;
  for (int i = 0; i < len; ++i) { const double v = n.next(); acc += v * v; }
  return 10.0 * std::log10(acc / len);
}

void noiseLevelIsRms() {
  for (double fs : {48000.0, 44100.0, 96000.0, 192000.0})
    for (uint32_t seed : {1u, 2u, 3u, 12345u})
      ASSERT_TRUE(std::fabs(noiseRmsDb(fs, seed, -40.0) + 40.0) <= 2.0);
  ASSERT_TRUE(std::fabs(noiseRmsDb(48000, 5, -60.0) + 60.0) <= 2.0);
  ASSERT_TRUE(std::fabs(noiseRmsDb(48000, 5, -20.0) + 20.0) <= 2.0);
}

void noiseOffIsExactZero() {
  D::Noise n; n.prepare(48000, 3);
  for (double lv : {-120.0, -121.0, -200.0, -1e9}) {
    n.set(lv);
    for (int i = 0; i < 2000; ++i) ASSERT_TRUE(n.next() == 0.f);
  }
  n.set(-60.0);
  double s = 0;
  for (int i = 0; i < 2000; ++i) s += std::fabs(n.next());
  ASSERT_TRUE(s > 0.0);
}

void noiseIsContinuousTowardOff() {
  // just above -120 dB the output is already ~1e-6 (peak incl. crackle < 1e-4): no step at the switch-off
  D::Noise n; n.prepare(48000, 9); n.set(-119.99);
  double pk = 0;
  for (int i = 0; i < 48000; ++i) pk = std::max(pk, (double)std::fabs(n.next()));
  ASSERT_TRUE(pk > 0.0 && pk <= 1e-4);
}

void noiseRejectsNonFiniteLevel() {
  D::Noise n; n.prepare(48000, 4); n.set(-40.0);
  n.set((double)kNaN);
  n.set(std::numeric_limits<double>::infinity());
  n.set(-std::numeric_limits<double>::infinity());
  double acc = 0;
  for (int i = 0; i < 48000; ++i) { const double v = n.next(); ASSERT_TRUE(std::isfinite(v)); acc += v * v; }
  ASSERT_TRUE(std::fabs(10.0 * std::log10(acc / 48000) + 40.0) <= 2.0);
  D::Noise m; m.prepare(48000, 4); m.set(40.0);  // absurdly hot: clamped to 0 dB RMS
  double a2 = 0;
  for (int i = 0; i < 48000; ++i) { const double v = m.next(); a2 += v * v; }
  ASSERT_TRUE(10.0 * std::log10(a2 / 48000) <= 2.0);
}

void roomZeroIsExactDry() {
  D::Room r; r.prepare(48000); r.set(0.0);
  ASSERT_TRUE(r.neutral());
  Lcg l;
  for (int i = 0; i < 1000; ++i) { const float x = l.next(); ASSERT_TRUE(r.process(x) == x); }
  r.set(-1.0);  // clamped to 0
  ASSERT_TRUE(r.neutral());
  r.set(0.3);
  ASSERT_TRUE(!r.neutral());
}

void roomTailDecays() {
  for (double fs : {8000.0, 44100.0, 48000.0, 192000.0}) {
    D::Room r; r.prepare(fs); r.set(1.0);
    const int tail = r.tailSamples();
    ASSERT_TRUE(tail > (int)(0.05 * fs) && tail < (int)(5.0 * fs));
    const int n = tail + (int)(2.0 * fs);
    double peak = 0, wetPeak = 0, after = 0, wetAfter = 0;
    for (int i = 0; i < n; ++i) {
      const float x = i == 0 ? 1.f : 0.f, y = r.process(x);
      const double w = i == 0 ? y - 1.0 : y;  // wet part (the dry impulse is exactly 1.0)
      peak = std::max(peak, (double)std::fabs(y));
      wetPeak = std::max(wetPeak, std::fabs(w));
      if (i >= tail) { after = std::max(after, (double)std::fabs(y)); wetAfter = std::max(wetAfter, std::fabs(w)); }
    }
    ASSERT_TRUE(wetPeak > 0.01);
    ASSERT_TRUE(after < peak * 1e-3);          // below -60 dB re the output peak
    ASSERT_TRUE(wetAfter < wetPeak * 1e-3);    // and re the wet response itself
  }
}

void roomWetScalesWithAmount() {
  D::Room a; a.prepare(48000); a.set(1.0);
  D::Room b; b.prepare(48000); b.set(0.5);
  for (int i = 0; i < 4800; ++i) {
    const float x = i == 0 ? 1.f : 0.f;
    const double wa = a.process(x) - x, wb = b.process(x) - x;
    ASSERT_TRUE(std::fabs(wa - 2.0 * wb) <= 1e-6);
  }
}

void roomDoesNotAllocateAfterPrepare() {
  D::Room r; r.prepare(192000);  // may allocate
  Lcg l;
  g_allocs = 0;
  g_countAllocs = true;
  r.set(0.7);
  float acc = 0;
  for (int i = 0; i < 20000; ++i) acc += r.process(l.next());
  r.set(0.0);
  r.process(0.5f);
  r.set(0.4);
  r.clear();
  acc += (float)r.tailSamples();
  r.set(0.2);
  for (int i = 0; i < 2000; ++i) acc += r.process(l.next());
  g_countAllocs = false;
  ASSERT_TRUE(std::isfinite(acc));
  ASSERT_TRUE(g_allocs == 0);
}

void roomIsContinuousTowardDry() {
  // y = x + amount * wet, |wet| <= 1 for a +-1 input: the deviation from dry is <= amount * 1
  for (double amount : {1e-6, 1e-4, 1e-2}) {
    D::Room r; r.prepare(48000); r.set(amount);
    Lcg l;
    double err = 0;
    for (int i = 0; i < 48000; ++i) { const float x = l.next(); err = std::max(err, (double)std::fabs(r.process(x) - x)); }
    ASSERT_TRUE(err <= amount * 2.0);
  }
}

void roomNoStaleStateAfterNeutral() {
  D::Room r; r.prepare(48000); r.set(1.0);
  Lcg l;
  for (int i = 0; i < 9600; ++i) r.process(l.next());   // loud wet state in the buffers
  r.set(0.0);                                            // neutral: process() bypasses the buffers
  for (int i = 0; i < 9600; ++i) r.process(l.next());
  r.set(1.0);
  for (int i = 0; i < 9600; ++i) ASSERT_TRUE(r.process(0.f) == 0.f);
  // active -> active keeps the tail
  D::Room k; k.prepare(48000); k.set(1.0);
  for (int i = 0; i < 9600; ++i) k.process(l.next());
  k.set(0.9);
  double pk = 0;
  for (int i = 0; i < 9600; ++i) pk = std::max(pk, (double)std::fabs(k.process(0.f)));
  ASSERT_TRUE(pk > 1e-3);
}

void roomRejectsNonFinite() {
  D::Room r; r.prepare(48000); r.set(0.6);
  D::Room ref; ref.prepare(48000); ref.set(0.6);
  r.set((double)kNaN);
  r.set(std::numeric_limits<double>::infinity());
  Lcg l;
  for (int i = 0; i < 500; ++i) { const float x = l.next(); ASSERT_TRUE(r.process(x) == ref.process(x)); }
  for (float bad : {kNaN, kInf, -kInf}) ASSERT_TRUE(std::isfinite(r.process(bad)));
  for (int i = 0; i < 20000; ++i) ASSERT_TRUE(std::isfinite(r.process(i == 0 ? 1.f : 0.f)));
  // the reverb tail must have recovered from the bad samples and then decay to silence
  for (int i = 0; i < 48000 * 6; ++i) r.process(0.f);
  ASSERT_TRUE(std::fabs(r.process(0.f)) < 1e-6);
}

void newBlocksStableAtExtremeSampleRates() {
  for (double fs : {8000.0, 192000.0}) {
    D::Crush c; c.prepare(fs); c.set(5000, 4);
    D::Compander p; p.prepare(fs); p.set(8.0, -40.0);
    D::Noise n; n.prepare(fs, 1); n.set(-30.0);
    D::Room r; r.prepare(fs); r.set(1.0);
    Lcg l;
    for (int i = 0; i < (int)fs; ++i) {
      const float x = l.next();
      ASSERT_TRUE(std::isfinite(c.process(x)));
      ASSERT_TRUE(std::isfinite(p.process(x)));
      ASSERT_TRUE(std::isfinite(n.next()));
      const float y = r.process(x);
      ASSERT_TRUE(std::isfinite(y) && std::fabs(y) < 20.f);
    }
  }
}


// ---- Engine (Task 5) ----
std::vector<float> whiteNoise(int n, uint32_t seed, float amp) {
  Lcg l; l.s = seed;
  std::vector<float> v((size_t)n);
  for (auto& x : v) x = amp * l.next();
  return v;
}

std::vector<float> sineWave(int n, double fs, double hz, double amp) {
  std::vector<float> v((size_t)n);
  for (int i = 0; i < n; ++i) v[(size_t)i] = (float)(amp * std::sin(2 * kPiT * hz * i / fs));
  return v;
}

// Defaults of the parameter table, then device and morph.
Settings engineSettings(int device, float morph) {
  double v[kNumParams];
  loadDefaults(v);
  Settings s = mapParams(v);
  s.device = device;
  s.morph = morph;
  return s;
}

struct Out { std::vector<float> L, R; };

// Feeds the whole input in calls whose lengths cycle through `blocks` (0 = an empty call).
Out runEngine(Engine& e, const std::vector<float>& inL, const std::vector<float>& inR,
              const std::vector<int>& blocks = {512}) {
  const int n = (int)inL.size();
  Out o{std::vector<float>((size_t)n), std::vector<float>((size_t)n)};
  int pos = 0;
  size_t k = 0, idle = 0;
  while (pos < n && idle < 1000) {
    const int len = std::min(blocks[k++ % blocks.size()], n - pos);
    e.process(inL.data() + pos, inR.data() + pos, o.L.data() + pos, o.R.data() + pos, len);
    pos += len;
    idle = len ? 0 : idle + 1;
  }
  return o;
}

double maxJump(const std::vector<float>& v, int from, int to) {
  double j = 0;
  for (int i = std::max(from, 1); i < to; ++i) j = std::max(j, (double)std::fabs(v[(size_t)i] - v[(size_t)i - 1]));
  return j;
}

double rmsOf(const std::vector<float>& v, int from, int to) {
  double s = 0;
  for (int i = from; i < to; ++i) s += (double)v[(size_t)i] * v[(size_t)i];
  return std::sqrt(s / std::max(1, to - from));
}

bool sameBits(const std::vector<float>& a, const std::vector<float>& b, int from, int to) {
  return std::memcmp(a.data() + from, b.data() + from, sizeof(float) * (size_t)(to - from)) == 0;
}

void loadDefaultsMatchesTable() {
  double v[kNumParams];
  for (auto& x : v) x = -7.0;
  loadDefaults(v);
  ASSERT_TRUE(v[kBypass] == kDefaultBypass && v[kDevice] == kDefaultDevice && v[kMorph] == kDefaultMorph);
  ASSERT_TRUE(v[kGain] == kDefaultGain && v[kMono] == kDefaultMono && v[kDrive] == kDefaultDrive);
  ASSERT_TRUE(v[kTone] == kDefaultTone && v[kNoise] == kDefaultNoise && v[kRoom] == kDefaultRoom);
  ASSERT_TRUE(v[kMix] == kDefaultMix && v[kCharacterIn] == kDefaultCharacterIn);
}

void mapParamsDefaults() {
  double v[kNumParams];
  loadDefaults(v);
  const Settings s = mapParams(v);
  ASSERT_TRUE(s.device == 0);
  ASSERT_APPROX(s.morph, 1.0);
  ASSERT_APPROX(s.gainDb, 0.0);
  ASSERT_APPROX(s.drive, 1.0);
  ASSERT_APPROX(s.tone, 0.0);
  ASSERT_APPROX(s.noise, 1.0);
  ASSERT_APPROX(s.room, 1.0);
  ASSERT_APPROX(s.mix, 1.0);
  ASSERT_TRUE(!s.mono && !s.bypass && s.characterIn);
}

void mapParamsNonFiniteAndClamp() {
  double v[kNumParams];
  for (auto& x : v) x = std::numeric_limits<double>::quiet_NaN();
  const Settings d = mapParams(v);  // every value falls back to its default
  ASSERT_TRUE(d.device == 0 && !d.mono && !d.bypass && d.characterIn);
  ASSERT_APPROX(d.morph, 1.0);
  ASSERT_APPROX(d.gainDb, 0.0);
  ASSERT_APPROX(d.drive, 1.0);
  ASSERT_APPROX(d.mix, 1.0);
  for (auto& x : v) x = std::numeric_limits<double>::infinity();
  const Settings i = mapParams(v);
  ASSERT_APPROX(i.gainDb, 0.0);  // inf is non-finite: default, not "clamped to 1"
  for (auto& x : v) x = 5.0;
  const Settings hi = mapParams(v);
  ASSERT_TRUE(hi.device == kNumDevices - 1 && hi.mono && hi.bypass && hi.characterIn);
  ASSERT_APPROX(hi.morph, 1.0);
  ASSERT_APPROX(hi.gainDb, 20.0);
  ASSERT_APPROX(hi.drive, 2.0);
  ASSERT_APPROX(hi.tone, 1.0);
  ASSERT_APPROX(hi.noise, 2.0);
  ASSERT_APPROX(hi.room, 2.0);
  ASSERT_APPROX(hi.mix, 1.0);
  for (auto& x : v) x = -5.0;
  v[kCharacterIn] = 1.0;
  const Settings lo = mapParams(v);
  ASSERT_TRUE(lo.device == 0 && !lo.mono && !lo.bypass);
  ASSERT_APPROX(lo.morph, 0.0);
  ASSERT_APPROX(lo.gainDb, -20.0);
  ASSERT_APPROX(lo.drive, 0.0);
  ASSERT_APPROX(lo.tone, -1.0);
  ASSERT_APPROX(lo.mix, 0.0);
}

void mapParamsCharacterInOff() {
  double v[kNumParams];
  loadDefaults(v);
  v[kDevice] = 0.4; v[kMorph] = 0.3; v[kGain] = 0.75;
  v[kDrive] = 1.0; v[kTone] = 1.0; v[kNoise] = 1.0; v[kRoom] = 1.0; v[kMix] = 0.0;
  v[kCharacterIn] = 0.0;
  const Settings s = mapParams(v);
  ASSERT_TRUE(!s.characterIn);
  ASSERT_TRUE(s.device == 2);
  ASSERT_APPROX(s.morph, 0.3);
  ASSERT_APPROX(s.gainDb, 10.0);
  ASSERT_APPROX(s.drive, 0.0);
  ASSERT_APPROX(s.tone, 0.0);
  ASSERT_APPROX(s.noise, 0.0);
  ASSERT_APPROX(s.room, 0.0);
  ASSERT_APPROX(s.mix, 1.0);
}

void targetParamsUserControls() {
  const BlockParams& mega = kDevices[0].p;
  Settings s = engineSettings(0, 1.f);
  ASSERT_TRUE(sameParams(targetParams(mega, s), mega));  // multipliers 1, tone 0: the device row
  s.drive = 2.f; s.room = 2.f; s.noise = 2.f; s.tone = 1.f;
  BlockParams p = targetParams(mega, s);
  ASSERT_APPROX(p.drive, 1.0);                               // 0.5 * 2
  ASSERT_APPROX(p.room, 0.1);                                // 0.05 * 2
  ASSERT_APPROX(p.noiseDb, mega.noiseDb + 20 * std::log10(2.0));
  ASSERT_APPROX(p.lpHz, 9000.0);                             // +1 octave
  ASSERT_APPROX(p.hpHz, 400.0 / std::sqrt(2.0));             // -1/2 octave
  s.drive = 0.f; s.room = 0.f; s.noise = 0.f; s.tone = -1.f;
  p = targetParams(mega, s);
  ASSERT_TRUE(p.drive == 0.0 && p.room == 0.0 && p.noiseDb == -120.0);
  ASSERT_APPROX(p.lpHz, 2250.0);
  ASSERT_APPROX(p.hpHz, 400.0 * std::sqrt(2.0));
  // corners stay in the band's valid range
  const BlockParams& cheap = kDevices[5].p;
  s = engineSettings(5, 1.f); s.tone = 1.f;
  ASSERT_TRUE(targetParams(cheap, s).lpHz <= 20000.0);
  // Morph 0 is neutral whatever the user controls say (tone shift scales with morph)
  for (int d = 0; d < kNumDevices; ++d)
    for (float a : {0.f, 1.f, 2.f})
      for (float t : {-1.f, 0.f, 1.f}) {
        Settings z = engineSettings(d, 0.f);
        z.drive = z.noise = z.room = a; z.tone = t;
        ASSERT_TRUE(sameParams(targetParams(kDevices[d].p, z), neutralParams()));
      }
}

// Ruling 6: crackle peaks grow with sqrt(fs), so the noise floor never goes above -20 dB.
void noiseLevelIsCapped() {
  BlockParams loud = kDevices[2].p;
  loud.noiseDb = -10.0;
  Settings s = engineSettings(2, 1.f);
  for (float a : {0.5f, 1.f, 2.f}) { s.noise = a; ASSERT_TRUE(targetParams(loud, s).noiseDb <= -20.0); }
  loud.noiseDb = -30.0; s.noise = 2.f;
  ASSERT_APPROX(targetParams(loud, s).noiseDb, -30.0 + 20 * std::log10(2.0));  // under the cap: untouched
  for (int d = 0; d < kNumDevices; ++d) { Settings t = engineSettings(d, 1.f); t.noise = 2.f; ASSERT_TRUE(targetParams(kDevices[d].p, t).noiseDb <= -20.0); }
}

void morphZeroEqualsInput() {
  const int n = 48000;
  const auto L = whiteNoise(n, 1, 0.9f), R = whiteNoise(n, 2, 0.9f);
  for (int d = 0; d < kNumDevices; ++d)
    for (float a : {1.f, 2.f})
      for (float t : {-1.f, 0.f, 1.f}) {
        Engine e; e.prepare(48000, 512);
        Settings s = engineSettings(d, 0.f);
        s.drive = s.noise = s.room = a; s.tone = t;
        e.setSettings(s);
        const Out o = runEngine(e, L, R);
        for (int i = 0; i < n; ++i) {
          ASSERT_TRUE(std::fabs(o.L[(size_t)i] - L[(size_t)i]) <= 1e-6);
          ASSERT_TRUE(std::fabs(o.R[(size_t)i] - R[(size_t)i]) <= 1e-6);
        }
      }
}

// Morph 1 -> 0: the smoothed block values must land EXACTLY on neutral (snap), so every block
// takes its neutral fast path again. Without the snap the noise level stays at ~-120 dB + eps and
// the crackle (peaks ~31x the RMS) alone exceeds 1e-6.
void morphBackToZeroIsExact() {
  const int n = 3 * 24000;
  const auto L = whiteNoise(n, 3, 0.8f), R = whiteNoise(n, 4, 0.8f);
  for (int d = 0; d < kNumDevices; ++d) {
    Engine e; e.prepare(48000, 512);
    Settings s = engineSettings(d, 1.f);
    s.noise = 2.f; s.room = 2.f;
    e.setSettings(s);
    std::vector<float> oL((size_t)n), oR((size_t)n);
    for (int pos = 0; pos < n; pos += 480) {
      if (pos == 24000) { s.morph = 0.f; e.setSettings(s); }
      e.process(L.data() + pos, R.data() + pos, oL.data() + pos, oR.data() + pos, 480);
    }
    ASSERT_TRUE(std::fabs(oL[1000] - L[1000]) > 1e-3);  // the device was really on
    for (int i = 48000; i < n; ++i) {
      ASSERT_TRUE(std::fabs(oL[(size_t)i] - L[(size_t)i]) <= 1e-6);
      ASSERT_TRUE(std::fabs(oR[(size_t)i] - R[(size_t)i]) <= 1e-6);
    }
  }
}

void bypassIsBitExact() {
  const int n = 48000;
  const auto L = whiteNoise(n, 5, 0.9f), R = whiteNoise(n, 6, 0.9f);
  for (int d = 0; d < kNumDevices; ++d) {
    Engine e; e.prepare(48000, 256);
    Settings s = engineSettings(d, 1.f);
    e.setSettings(s);
    std::vector<float> oL((size_t)n), oR((size_t)n);
    const int on = 9600, settled = on + 4800;  // bypass after 200 ms, check from 100 ms later
    for (int pos = 0; pos < n; pos += 240) {
      if (pos == on) { s.bypass = true; e.setSettings(s); }
      e.process(L.data() + pos, R.data() + pos, oL.data() + pos, oR.data() + pos, 240);
    }
    ASSERT_TRUE(!sameBits(oL, L, 0, on));
    ASSERT_TRUE(std::fabs(oL[on + 1] - L[on + 1]) > 0.f || std::fabs(oL[on + 2] - L[on + 2]) > 0.f);  // a ramp, not a cut
    ASSERT_TRUE(sameBits(oL, L, settled, n));
    ASSERT_TRUE(sameBits(oR, R, settled, n));
  }
  // bypassed from the start: exact straight away
  Engine e; e.prepare(48000, 256);
  Settings s = engineSettings(2, 1.f); s.bypass = true;
  e.setSettings(s);
  const Out o = runEngine(e, L, R);
  ASSERT_TRUE(sameBits(o.L, L, 0, n) && sameBits(o.R, R, 0, n));
}

void monoMakesChannelsIdentical() {
  const int n = 48000;
  const auto L = sineWave(n, 48000, 440, 0.5);
  const auto R = whiteNoise(n, 7, 0.5f);
  Engine e; e.prepare(48000, 512);
  Settings s = engineSettings(0, 1.f);  // Megaphone: noise and room on, so they must follow Mono too
  e.setSettings(s);
  std::vector<float> oL((size_t)n), oR((size_t)n);
  const int on = 4800;
  for (int pos = 0; pos < n; pos += 480) {
    if (pos == on) { s.mono = true; e.setSettings(s); }
    e.process(L.data() + pos, R.data() + pos, oL.data() + pos, oR.data() + pos, 480);
  }
  ASSERT_TRUE(!sameBits(oL, oR, 0, on));
  ASSERT_TRUE(!sameBits(oL, oR, on, on + 200));  // a 10 ms ramp, not a switch
  const int settled = on + 480 + 64;             // 10 ms ramp + one update interval of margin
  ASSERT_TRUE(sameBits(oL, oR, settled, n));
  ASSERT_TRUE(rmsOf(oL, settled, n) > 0.01);
}

// Mono off: noise is decorrelated between the channels even on identical (silent) input.
void noiseIsDecorrelatedWhenStereo() {
  const int n = 24000;
  const std::vector<float> z((size_t)n, 0.f), z2((size_t)n, 0.f);
  Engine e; e.prepare(48000, 512);
  Settings s = engineSettings(2, 1.f);  // Walkie-talkie: noise -40 dB
  s.room = 0.f;
  e.setSettings(s);
  const Out o = runEngine(e, z, z2);
  ASSERT_TRUE(rmsOf(o.L, 0, n) > 1e-4 && rmsOf(o.R, 0, n) > 1e-4);
  double c = 0;
  for (int i = 0; i < n; ++i) c += (double)o.L[(size_t)i] * o.R[(size_t)i];
  c /= n * rmsOf(o.L, 0, n) * rmsOf(o.R, 0, n);
  ASSERT_TRUE(std::fabs(c) < 0.2);
}

void telephoneBandOnEngine() {
  // Drive and Room off as the brief says; Noise off too: the -54 dB floor and its crackle would
  // otherwise dominate the level of the heavily attenuated tones being measured.
  auto level = [](double hz) {
    Engine e; e.prepare(48000, 512);
    Settings s = engineSettings(1, 1.f);
    s.mix = 1.f; s.drive = 0.f; s.room = 0.f; s.noise = 0.f;
    e.setSettings(s);
    const auto x = sineWave(72000, 48000, hz, 0.5);
    const Out o = runEngine(e, x, x);
    return rmsOf(o.L, 24000, 72000);
  };
  const double ref = level(1000);
  ASSERT_TRUE(ref > 0.01);
  ASSERT_TRUE(toDb(level(100) / ref) <= -15.0);
  ASSERT_TRUE(toDb(level(8000) / ref) <= -15.0);
}

void extremeParametersStayFinite() {
  const int n = 48000;
  std::vector<float> L((size_t)n), R((size_t)n);
  Lcg l;
  for (int i = 0; i < n; ++i) {
    L[(size_t)i] = 10.f * l.next();
    R[(size_t)i] = i % 2 ? 10.f : -10.f;
    if (i % 97 == 0) L[(size_t)i] = kNaN;
    if (i % 89 == 0) R[(size_t)i] = kInf;
    if (i % 83 == 0) L[(size_t)i] = -kInf;
  }
  for (int d = 0; d < kNumDevices; ++d)
    for (int variant = 0; variant < 4; ++variant) {
      double v[kNumParams];
      for (auto& x : v) x = variant == 1 ? 0.0 : 1.0;
      v[kBypass] = 0.0;
      v[kDevice] = d / double(kNumDevices - 1);
      if (variant == 2) v[kMix] = 0.0;   // dry path at +20 dB
      if (variant == 3) v[kMono] = 0.0;
      Engine e; e.prepare(48000, 512);
      e.setSettings(mapParams(v));
      const Out o = runEngine(e, L, R);
      for (int i = 0; i < n; ++i) {
        ASSERT_TRUE(std::isfinite(o.L[(size_t)i]) && std::isfinite(o.R[(size_t)i]));
        ASSERT_TRUE(std::fabs(o.L[(size_t)i]) <= 16.f && std::fabs(o.R[(size_t)i]) <= 16.f);
      }
    }
}

void zeroLatency() {
  std::vector<float> x(256, 0.f);
  x[0] = 1.f;
  Engine e; e.prepare(48000, 256);
  e.setSettings(engineSettings(1, 0.f));
  Out o = runEngine(e, x, x);
  ASSERT_TRUE(o.L[0] == 1.f && o.R[0] == 1.f);
  // with the device on, the response still starts on sample 0 (no lookahead anywhere)
  Engine a; a.prepare(48000, 256);
  Settings s = engineSettings(4, 1.f); s.noise = 0.f;
  a.setSettings(s);
  o = runEngine(a, x, x);
  ASSERT_TRUE(std::fabs(o.L[0]) > 1e-4);
}

void noiseIsDeterministic() {
  const int n = 24000;
  const auto L = whiteNoise(n, 8, 0.3f), R = whiteNoise(n, 9, 0.3f);
  Settings s = engineSettings(2, 1.f); s.noise = 2.f;
  Engine a; a.prepare(48000, 512); a.setSettings(s);
  Engine b; b.prepare(48000, 512); b.setSettings(s);
  const Out oa = runEngine(a, L, R), ob = runEngine(b, L, R);
  ASSERT_TRUE(sameBits(oa.L, ob.L, 0, n) && sameBits(oa.R, ob.R, 0, n));
}

void resetMatchesFreshEngine() {
  const int n = 24000;
  const auto L = whiteNoise(n, 10, 0.5f), R = whiteNoise(n, 11, 0.5f);
  Settings s = engineSettings(4, 1.f); s.noise = 2.f;
  Engine a; a.prepare(48000, 512); a.setSettings(s);
  runEngine(a, L, R);
  a.reset();
  const Out oa = runEngine(a, L, R);
  Engine b; b.prepare(48000, 512); b.setSettings(s);
  const Out ob = runEngine(b, L, R);
  ASSERT_TRUE(sameBits(oa.L, ob.L, 0, n) && sameBits(oa.R, ob.R, 0, n));
}

void processDoesNotAllocate() {
  const int n = 512;
  std::vector<float> L = whiteNoise(n, 12, 0.5f), R = whiteNoise(n, 13, 0.5f), oL((size_t)n), oR((size_t)n);
  L[100] = kNaN;
  Engine e; e.prepare(48000, 256);
  Settings s = engineSettings(0, 1.f);
  e.setSettings(s);
  g_allocs = 0;
  g_countAllocs = true;
  for (int k = 0; k < 400; ++k) {
    s.device = k % kNumDevices;
    s.morph = (k / 7) % 2 ? 1.f : 0.f;
    s.mono = (k / 11) % 2;
    s.bypass = (k / 13) % 2;
    s.characterIn = (k / 17) % 2;
    e.setSettings(s);
    if (k % 5 == 0) e.process(L.data(), nullptr, oL.data(), oR.data(), n);  // mono host track
    else if (k % 5 == 1) e.process(L.data(), R.data(), oL.data(), nullptr, n);
    else e.process(L.data(), R.data(), oL.data(), oR.data(), n);
    (void)e.lastDisplay();
    if (k == 200) { e.reset(); e.clearClips(); }
  }
  g_countAllocs = false;
  ASSERT_TRUE(g_allocs == 0);
}

void sampleRateExtremes() {
  for (double fs : {8000.0, 44100.0, 192000.0}) {
    const int n = (int)fs;
    const auto L = whiteNoise(n, 14, 0.9f), R = whiteNoise(n, 15, 0.9f);
    for (int d = 0; d < kNumDevices; ++d) {
      Engine e; e.prepare(fs, 512);
      Settings s = engineSettings(d, 1.f);
      s.drive = s.noise = s.room = 2.f; s.tone = 1.f;
      e.setSettings(s);
      const Out o = runEngine(e, L, R);
      for (int i = 0; i < n; ++i) ASSERT_TRUE(std::isfinite(o.L[(size_t)i]) && std::isfinite(o.R[(size_t)i]));
      ASSERT_TRUE(rmsOf(o.L, n / 2, n) > 1e-3);
      // Morph 0 stays transparent at every rate (the band must not stay on just because its
      // LPF is clamped below Nyquist at low rates)
      Engine z; z.prepare(fs, 512);
      z.setSettings(engineSettings(d, 0.f));
      const Out oz = runEngine(z, L, R);
      for (int i = 0; i < n; ++i) ASSERT_TRUE(std::fabs(oz.L[(size_t)i] - L[(size_t)i]) <= 1e-6);
    }
  }
}

// Same output whatever the host's block sizes, also across a settings change (made at the same
// sample in every run): the 32-sample update grid runs on its own sample counter.
void blockSizesAgree() {
  const int n = 48000, maxBlock = 512, at = 12000 + 5;
  const auto L = whiteNoise(n, 16, 0.7f), R = sineWave(n, 48000, 330, 0.7);
  for (int d = 0; d < kNumDevices; ++d) {
    Settings s = engineSettings(d, 1.f); s.noise = 2.f; s.room = 2.f;
    Settings t = s; t.device = (d + 2) % kNumDevices; t.morph = 0.6f; t.mono = true;
    Engine ref; ref.prepare(48000, maxBlock); ref.setSettings(s);
    std::vector<float> rL((size_t)n), rR((size_t)n);
    ref.process(L.data(), R.data(), rL.data(), rR.data(), at);  // one call each side, split internally
    ref.setSettings(t);
    ref.process(L.data() + at, R.data() + at, rL.data() + at, rR.data() + at, n - at);
    for (const std::vector<int>& pattern : {std::vector<int>{0, 1, 7, maxBlock + 100}, std::vector<int>{1},
                                            std::vector<int>{maxBlock + 100}, std::vector<int>{33, 0, 31}}) {
      Engine e; e.prepare(48000, maxBlock); e.setSettings(s);
      const std::vector<float> L1(L.begin(), L.begin() + at), R1(R.begin(), R.begin() + at);
      const std::vector<float> L2(L.begin() + at, L.end()), R2(R.begin() + at, R.end());
      const Out o1 = runEngine(e, L1, R1, pattern);
      e.setSettings(t);
      const Out o2 = runEngine(e, L2, R2, pattern);
      ASSERT_TRUE(sameBits(o1.L, rL, 0, at) && sameBits(o1.R, rR, 0, at));
      ASSERT_TRUE(std::memcmp(o2.L.data(), rL.data() + at, sizeof(float) * (size_t)(n - at)) == 0);
      ASSERT_TRUE(std::memcmp(o2.R.data(), rR.data() + at, sizeof(float) * (size_t)(n - at)) == 0);
    }
  }
}

void monoHostTrack() {
  const int n = 9600;
  const auto x = whiteNoise(n, 17, 0.5f);
  std::vector<float> oL((size_t)n), oR((size_t)n), oL2((size_t)n), oR2((size_t)n), oL3((size_t)n);
  Settings s = engineSettings(0, 1.f); s.noise = 2.f;  // noise would differ per channel on a stereo track
  Engine a; a.prepare(48000, 512); a.setSettings(s);
  a.process(x.data(), nullptr, oL.data(), oR.data(), n);
  ASSERT_TRUE(sameBits(oL, oR, 0, n));
  Engine b; b.prepare(48000, 512); b.setSettings(s);
  b.process(x.data(), x.data(), oL2.data(), oR2.data(), n);
  ASSERT_TRUE(sameBits(oL2, oR2, 0, n));
  ASSERT_TRUE(sameBits(oL, oL2, 0, n));
  Engine c; c.prepare(48000, 512); c.setSettings(s);
  c.process(x.data(), nullptr, oL3.data(), nullptr, n);
  ASSERT_TRUE(sameBits(oL3, oL, 0, n));
  // in place: output buffer == input buffer
  std::vector<float> io = x;
  Engine d; d.prepare(48000, 512); d.setSettings(s);
  d.process(io.data(), io.data(), io.data(), nullptr, n);
  ASSERT_TRUE(sameBits(io, oL, 0, n));
}

// Morph 0 -> 1 mid-block on a loud 440 Hz sine. The sine's own step is 2*pi*440/48000*0.9 =
// 0.052 and the devices at steady state stay below ~0.3 (measured: walkie's drive slope); an
// unsmoothed jump (block values or the band's cold start) steps by ~|x| (up to 0.9). Noise is off:
// crackle is impulsive by design and is not a parameter click.
void morphJumpHasNoClick() {
  const int n = 48000, at = 24000 + 13;
  const auto x = sineWave(n, 48000, 440, 0.9);
  for (int d = 0; d < kNumDevices; ++d) {
    Engine e; e.prepare(48000, 512);
    Settings s = engineSettings(d, 0.f); s.noise = 0.f;
    e.setSettings(s);
    std::vector<float> oL((size_t)n), oR((size_t)n);
    e.process(x.data(), x.data(), oL.data(), oR.data(), at);
    s.morph = 1.f; e.setSettings(s);
    e.process(x.data() + at, x.data() + at, oL.data() + at, oR.data() + at, n - at);
    ASSERT_TRUE(maxJump(oL, 1, n) <= 0.5);
    for (float y : oL) ASSERT_TRUE(std::isfinite(y));
  }
}

// Morph automated 0 -> 1 over 1 s (the intro use case), loud 440 Hz sine, noise off. The output
// blends from the dry sine (step 0.052) to the device's steady state, so no step may exceed the
// larger of the two by more than 0.05. The band's cold start where it leaves neutral would add
// ~0.3..0.5 here (measured with the band crossfade removed: 0.37..0.58 on 5 of 6 devices).
void morphAutomationHasNoClick() {
  const int n = 96000, from = 24000;
  const auto x = sineWave(n, 48000, 440, 0.9);
  const double dryStep = 2 * kPiT * 440 / 48000 * 0.9;
  for (int d = 0; d < kNumDevices; ++d) {
    Engine e; e.prepare(48000, 512);
    Settings s = engineSettings(d, 0.f); s.noise = 0.f;
    e.setSettings(s);
    std::vector<float> oL((size_t)n), oR((size_t)n);
    for (int pos = 0; pos < n; pos += 48) {  // host automation: one value per 1 ms block
      if (pos >= from) { s.morph = std::min(1.f, (pos - from) / 48000.f); e.setSettings(s); }
      e.process(x.data() + pos, x.data() + pos, oL.data() + pos, oR.data() + pos, 48);
    }
    const double steady = std::max(dryStep, maxJump(oL, n - 4800, n));
    ASSERT_TRUE(maxJump(oL, from, n) <= steady + 0.05);
  }
}

// PA with its room at full, an impulse, then Device changes 20 ms into the tail (as the brief
// says) and 60 ms in (the first comb echo only arrives after ~25 ms at 48 kHz, so at 20 ms the
// tail has barely started). Sample steps stay <= 0.5, and over the 64 samples after the change
// the output departs from an unswitched twin by at most a quarter of the tail's level: the
// smoothed values have moved ~12% by then. Cutting the room (or any instant jump) departs by
// about the whole tail.
void deviceSwitchDuringRoomTail() {
  const int n = 48000;
  std::vector<float> x((size_t)n, 0.f);
  x[0] = 1.f;
  for (int at : {960, 2880 + 7})
  for (int to = 0; to < kNumDevices; ++to) {
    if (to == 4) continue;
    Settings s = engineSettings(4, 1.f); s.room = 2.f; s.noise = 0.f;
    Engine e; e.prepare(48000, 512); e.setSettings(s);
    Engine ref; ref.prepare(48000, 512); ref.setSettings(s);
    const Out r = runEngine(ref, x, x);
    std::vector<float> oL((size_t)n), oR((size_t)n);
    e.process(x.data(), x.data(), oL.data(), oR.data(), at);
    s.device = to; e.setSettings(s);
    e.process(x.data() + at, x.data() + at, oL.data() + at, oR.data() + at, n - at);
    for (float y : oL) ASSERT_TRUE(std::isfinite(y));
    ASSERT_TRUE(maxJump(oL, at, n) <= 0.5);
    if (at < 1200) continue;  // 20 ms: before the first echo, nothing more to compare
    // the tail is there at the switch, and the switch bends it, it does not cut it
    double dev = 0, tail = 0;
    for (int i = at; i < at + 64; ++i) {
      dev = std::max(dev, (double)std::fabs(oL[(size_t)i] - r.L[(size_t)i]));
      tail = std::max(tail, (double)std::fabs(r.L[(size_t)i]));
    }
    ASSERT_TRUE(tail > 1e-2);
    ASSERT_TRUE(dev <= 0.25 * tail);
  }
}

// Device change is a ~40 ms crossfade: early it is still close to the old device, then it lands
// on the new one exactly (same output as an engine that started on it, once the state converged).
void deviceSwitchSettles() {
  const int n = 48000, at = 4800;
  const auto x = sineWave(n, 48000, 700, 0.5);
  // AM radio -> Cheap speaker: no Crush on either side (a sample-and-hold phase never converges
  // between two instances, so it would hide the comparison with a fresh engine)
  Settings s = engineSettings(3, 1.f); s.noise = 0.f; s.room = 0.f;
  Engine e; e.prepare(48000, 480); e.setSettings(s);
  Settings t = s; t.device = 5;
  Engine fresh; fresh.prepare(48000, 480); fresh.setSettings(t);
  Engine old; old.prepare(48000, 480); old.setSettings(s);
  const Out f = runEngine(fresh, x, x), o0 = runEngine(old, x, x);
  std::vector<float> oL((size_t)n), oR((size_t)n);
  e.process(x.data(), x.data(), oL.data(), oR.data(), at);
  e.setSettings(t);
  e.process(x.data() + at, x.data() + at, oL.data() + at, oR.data() + at, n - at);
  double early = 0, oldDiff = 0, late = 0;
  for (int i = at; i < at + 96; ++i) {
    early = std::max(early, (double)std::fabs(oL[(size_t)i] - f.L[(size_t)i]));
    oldDiff = std::max(oldDiff, (double)std::fabs(oL[(size_t)i] - o0.L[(size_t)i]));
  }
  for (int i = n - 4800; i < n; ++i) late = std::max(late, (double)std::fabs(oL[(size_t)i] - f.L[(size_t)i]));
  ASSERT_TRUE(oldDiff < early);   // 2 ms after the change it is still nearer the old device
  ASSERT_TRUE(late < 1e-3);       // and it ends on the new one
}

void characterInOffIgnoresMix() {
  const int n = 24000;
  const auto L = whiteNoise(n, 18, 0.5f), R = whiteNoise(n, 19, 0.5f);
  double v[kNumParams];
  loadDefaults(v);
  v[kDevice] = 0.2; v[kCharacterIn] = 0.0;
  v[kMix] = 0.0;
  Engine a; a.prepare(48000, 512); a.setSettings(mapParams(v));
  v[kMix] = 1.0;
  Engine b; b.prepare(48000, 512); b.setSettings(mapParams(v));
  const Out oa = runEngine(a, L, R), ob = runEngine(b, L, R);
  ASSERT_TRUE(sameBits(oa.L, ob.L, 0, n) && sameBits(oa.R, ob.R, 0, n));
  ASSERT_TRUE(!sameBits(oa.L, L, 0, n));  // the device itself still colours the sound
  // Settings handed straight to the engine get the same treatment
  Settings s = engineSettings(1, 1.f); s.characterIn = false; s.mix = 0.f; s.drive = 2.f;
  Engine c; c.prepare(48000, 512); c.setSettings(s);
  const Out oc = runEngine(c, L, R);
  ASSERT_TRUE(sameBits(oc.L, ob.L, 0, n));
}

// Ruling 7: the asymmetric shaper adds DC; the 10 Hz blocker after it removes it.
void driveDcIsBlocked() {
  const int n = 72000;
  const auto x = sineWave(n, 48000, 1000, 0.9);
  Settings s = engineSettings(0, 1.f);  // Megaphone, drive 0.5
  s.noise = 0.f; s.room = 0.f;
  ASSERT_APPROX(targetParams(kDevices[0].p, s).drive, 0.5);
  Engine e; e.prepare(48000, 512); e.setSettings(s);
  const Out o = runEngine(e, x, x);
  double mean = 0;
  for (int i = 24000; i < n; ++i) mean += o.L[(size_t)i];
  mean /= (n - 24000);
  ASSERT_TRUE(std::fabs(mean) < 0.01);
  ASSERT_TRUE(rmsOf(o.L, 24000, n) > 0.05);
}

void nonFiniteSettingsIgnored() {
  const int n = 9600, step = 1920;
  const auto L = whiteNoise(n, 20, 0.5f), R = whiteNoise(n, 21, 0.5f);
  const Settings s = engineSettings(3, 0.7f);
  Engine a; a.prepare(48000, 512); a.setSettings(s);
  Engine b; b.prepare(48000, 512); b.setSettings(s);
  Settings bad[4] = {s, s, s, s};
  bad[0].morph = kNaN;
  bad[1].gainDb = kInf;
  bad[2].mix = -kInf; bad[2].device = 0;
  bad[3].tone = kNaN; bad[3].noise = 2.f;
  std::vector<float> aL((size_t)n), aR((size_t)n), bL((size_t)n), bR((size_t)n);
  for (int k = 0; k * step < n; ++k) {  // each bad set is followed by audio, so each one counts
    if (k < 4) b.setSettings(bad[k]);
    a.process(L.data() + k * step, R.data() + k * step, aL.data() + k * step, aR.data() + k * step, step);
    b.process(L.data() + k * step, R.data() + k * step, bL.data() + k * step, bR.data() + k * step, step);
  }
  ASSERT_TRUE(sameBits(aL, bL, 0, n) && sameBits(aR, bR, 0, n));
}

// A NaN on L clears L's chain (the room tail stops) and leaves R untouched.
void nonFiniteInputClearsChannel() {
  const int n = 24000, bad = 4800;
  std::vector<float> L = whiteNoise(n, 22, 0.8f), R = L;
  for (int i = bad + 1; i < n; ++i) L[(size_t)i] = R[(size_t)i] = 0.f;
  std::vector<float> Lbad = L;
  Lbad[bad] = kNaN;
  Settings s = engineSettings(4, 1.f); s.noise = 0.f; s.room = 2.f;  // PA: long tail, no noise
  Engine a; a.prepare(48000, 512); a.setSettings(s);
  Engine b; b.prepare(48000, 512); b.setSettings(s);
  const Out oa = runEngine(a, L, R), ob = runEngine(b, Lbad, R);
  ASSERT_TRUE(sameBits(oa.R, ob.R, 0, n));
  ASSERT_TRUE(sameBits(oa.L, ob.L, 0, bad));
  for (float y : ob.L) ASSERT_TRUE(std::isfinite(y));
  ASSERT_TRUE(ob.L[bad] == 0.f);
  ASSERT_TRUE(rmsOf(oa.L, bad + 1, bad + 2400) > 1e-3);  // the clean run has a tail here
  ASSERT_TRUE(rmsOf(ob.L, bad + 1, n) == 0.0);           // the cleared chain is silent
}

void displayPeaksAndClipHold() {
  Engine e; e.prepare(48000, 512);
  Settings s = engineSettings(0, 0.f);
  e.setSettings(s);
  std::vector<float> z(4800, 0.f), o1(4800), o2(4800);
  e.process(z.data(), z.data(), o1.data(), o2.data(), 4800);
  DisplayValues d = e.lastDisplay();
  ASSERT_TRUE(d.inPeakDb[0] <= -119.f && d.outPeakDb[1] <= -119.f && !d.clipIn && !d.clipOut && !d.clipDrive);
  const auto half = sineWave(4800, 48000, 1000, 0.5);
  const auto quiet = sineWave(4800, 48000, 1000, 0.25);
  e.process(half.data(), quiet.data(), o1.data(), o2.data(), 4800);
  d = e.lastDisplay();
  ASSERT_TRUE(std::fabs(d.inPeakDb[0] - (-6.02f)) < 0.1f && std::fabs(d.inPeakDb[1] - (-12.04f)) < 0.1f);
  ASSERT_TRUE(std::fabs(d.outPeakDb[0] - (-6.02f)) < 0.1f && !d.clipIn);
  const auto hot = sineWave(4800, 48000, 1000, 1.2);
  e.process(hot.data(), hot.data(), o1.data(), o2.data(), 4800);
  ASSERT_TRUE(e.lastDisplay().clipIn && e.lastDisplay().clipOut);
  for (int k = 0; k < 18; ++k) e.process(z.data(), z.data(), o1.data(), o2.data(), 4800);  // 1.8 s
  ASSERT_TRUE(e.lastDisplay().clipIn && e.lastDisplay().clipOut);
  for (int k = 0; k < 3; ++k) e.process(z.data(), z.data(), o1.data(), o2.data(), 4800);   // 2.1 s
  ASSERT_TRUE(!e.lastDisplay().clipIn && !e.lastDisplay().clipOut);
  e.process(hot.data(), hot.data(), o1.data(), o2.data(), 4800);
  e.clearClips();
  ASSERT_TRUE(!e.lastDisplay().clipIn && !e.lastDisplay().clipOut);
  // DRV: the shaper is driven onto its rail
  Engine w; w.prepare(48000, 512);
  Settings t = engineSettings(2, 1.f); t.drive = 2.f; t.noise = 0.f;
  w.setSettings(t);
  const auto big = sineWave(4800, 48000, 1000, 4.0);
  w.process(big.data(), big.data(), o1.data(), o2.data(), 4800);
  ASSERT_TRUE(w.lastDisplay().clipDrive);
  Engine q; q.prepare(48000, 512);
  q.setSettings(t);
  q.process(half.data(), half.data(), o1.data(), o2.data(), 4800);
  ASSERT_TRUE(!q.lastDisplay().clipDrive);
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
  RUN_TEST(bandTelephoneAttenuates);
  RUN_TEST(bandNeutralIsExactPassthrough);
  RUN_TEST(bandClampsBelowNyquist);
  RUN_TEST(bandSetKeepsState);
  RUN_TEST(coneSetKeepsState);
  RUN_TEST(bandNoTransientAfterNeutral);
  RUN_TEST(coneNoTransientAfterNeutral);
  RUN_TEST(driveIsContinuousNearZero);
  RUN_TEST(driveZeroIsIdentity);
  RUN_TEST(driveIsBoundedAndAsymmetric);
  RUN_TEST(coneBoostsAtCenter);
  RUN_TEST(coneNeutralIsExactPassthrough);
  RUN_TEST(blocksStableAtExtremeSampleRates);
  RUN_TEST(crushOffIsExactPassthrough);
  RUN_TEST(crushQuantizes);
  RUN_TEST(crushHoldsSamples);
  RUN_TEST(crushIsContinuousTowardNeutral);
  RUN_TEST(crushNoStaleHoldAfterNeutral);
  RUN_TEST(crushRejectsNonFinite);
  RUN_TEST(companderIdentityAtRatioOne);
  RUN_TEST(companderReducesLoudSignal);
  RUN_TEST(companderSquelchesQuiet);
  RUN_TEST(companderIsContinuousTowardNeutral);
  RUN_TEST(companderNoTransientAfterNeutral);
  RUN_TEST(companderRejectsNonFinite);
  RUN_TEST(noiseIsDeterministicPerSeed);
  RUN_TEST(noiseLevelIsRms);
  RUN_TEST(noiseOffIsExactZero);
  RUN_TEST(noiseIsContinuousTowardOff);
  RUN_TEST(noiseRejectsNonFiniteLevel);
  RUN_TEST(roomZeroIsExactDry);
  RUN_TEST(roomTailDecays);
  RUN_TEST(roomWetScalesWithAmount);
  RUN_TEST(roomDoesNotAllocateAfterPrepare);
  RUN_TEST(roomIsContinuousTowardDry);
  RUN_TEST(roomNoStaleStateAfterNeutral);
  RUN_TEST(roomRejectsNonFinite);
  RUN_TEST(newBlocksStableAtExtremeSampleRates);
  RUN_TEST(loadDefaultsMatchesTable);
  RUN_TEST(mapParamsDefaults);
  RUN_TEST(mapParamsNonFiniteAndClamp);
  RUN_TEST(mapParamsCharacterInOff);
  RUN_TEST(targetParamsUserControls);
  RUN_TEST(noiseLevelIsCapped);
  RUN_TEST(morphZeroEqualsInput);
  RUN_TEST(morphBackToZeroIsExact);
  RUN_TEST(bypassIsBitExact);
  RUN_TEST(monoMakesChannelsIdentical);
  RUN_TEST(noiseIsDecorrelatedWhenStereo);
  RUN_TEST(telephoneBandOnEngine);
  RUN_TEST(extremeParametersStayFinite);
  RUN_TEST(zeroLatency);
  RUN_TEST(noiseIsDeterministic);
  RUN_TEST(resetMatchesFreshEngine);
  RUN_TEST(processDoesNotAllocate);
  RUN_TEST(sampleRateExtremes);
  RUN_TEST(blockSizesAgree);
  RUN_TEST(monoHostTrack);
  RUN_TEST(morphJumpHasNoClick);
  RUN_TEST(morphAutomationHasNoClick);
  RUN_TEST(deviceSwitchDuringRoomTail);
  RUN_TEST(deviceSwitchSettles);
  RUN_TEST(characterInOffIgnoresMix);
  RUN_TEST(driveDcIsBlocked);
  RUN_TEST(nonFiniteSettingsIgnored);
  RUN_TEST(nonFiniteInputClearsChannel);
  RUN_TEST(displayPeaksAndClipHold);
  std::printf("%d tests run, %d failed\n", g_passed + g_failed, g_failed);
  return g_failed ? 1 : 0;
}
