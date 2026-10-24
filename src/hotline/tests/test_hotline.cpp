/**
 * Fairy Tapes · Hotline — Unit tests for params and state.
 * Run via CTest; build target: hotline_tests.
 */

#include "devices.h"
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
  std::printf("%d tests run, %d failed\n", g_passed + g_failed, g_failed);
  return g_failed ? 1 : 0;
}
