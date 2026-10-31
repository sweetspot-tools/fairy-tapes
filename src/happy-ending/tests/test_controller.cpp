/**
 * Fairy Tapes · Happy Ending — Unit tests for the Edit Controller (compiled in, no module needed): parameter
 * strings and typed values, MATCH as one host edit, the session parameters after a state load, and the
 * OVERSAMPLING change asking the host to restart (latency). Run via CTest; target happy_ending_controller_tests.
 */

#include "controller.h"
#include "display.h"
#include "loudness_macro.h"
#include "state.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace FairyTapes::HappyEnding;
using Steinberg::Vst::ParamValue;
using PID = Steinberg::Vst::ParamID;   // (FairyTapes::HappyEnding has an enum of the same name)

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

struct Handler : Steinberg::Vst::IComponentHandler {
  std::vector<std::string> log;  // "begin:id", "perform:id", "end:id"
  std::vector<int> restarts;
  std::vector<double> performed;
  Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void** o) override { *o = nullptr; return Steinberg::kNoInterface; }
  Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
  Steinberg::uint32 PLUGIN_API release() override { return 1; }
  Steinberg::tresult PLUGIN_API beginEdit(PID id) override { log.push_back("begin:" + std::to_string(id)); return Steinberg::kResultOk; }
  Steinberg::tresult PLUGIN_API performEdit(PID id, ParamValue v) override {
    log.push_back("perform:" + std::to_string(id)); performed.push_back(v); return Steinberg::kResultOk;
  }
  Steinberg::tresult PLUGIN_API endEdit(PID id) override { log.push_back("end:" + std::to_string(id)); return Steinberg::kResultOk; }
  Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 flags) override { restarts.push_back(flags); return Steinberg::kResultOk; }
};

struct VecStream : Steinberg::IBStream {
  std::vector<uint8_t> data;
  Steinberg::int64 pos = 0;
  Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void** o) override { *o = nullptr; return Steinberg::kNoInterface; }
  Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
  Steinberg::uint32 PLUGIN_API release() override { return 1; }
  Steinberg::tresult PLUGIN_API read(void* b, Steinberg::int32 n, Steinberg::int32* got) override {
    const Steinberg::int64 left = static_cast<Steinberg::int64>(data.size()) - pos;
    const Steinberg::int32 k = static_cast<Steinberg::int32>(left < n ? left : n);
    if (k > 0) std::memcpy(b, data.data() + pos, static_cast<size_t>(k));
    pos += k;
    if (got) *got = k;
    return Steinberg::kResultOk;
  }
  Steinberg::tresult PLUGIN_API write(void*, Steinberg::int32, Steinberg::int32*) override { return Steinberg::kResultFalse; }
  Steinberg::tresult PLUGIN_API seek(Steinberg::int64, Steinberg::int32, Steinberg::int64*) override { return Steinberg::kResultFalse; }
  Steinberg::tresult PLUGIN_API tell(Steinberg::int64* p) override { if (p) *p = pos; return Steinberg::kResultOk; }
};

struct Fixture {
  Controller c;
  Handler h;
  Fixture() {
    c.initialize(nullptr);
    c.setComponentHandler(&h);
  }
  ~Fixture() { c.setComponentHandler(nullptr); c.terminate(); }
  std::string text(ParamID id, int index) {
    Steinberg::Vst::String128 s;
    c.getParamStringByValue(id, normFromIndex(id, index), s);
    std::string out;
    for (int i = 0; s[i]; ++i) out.push_back(static_cast<char>(s[i]));
    return out;
  }
  void setIndex(ParamID id, int index) { c.setParamNormalized(id, normFromIndex(id, index)); }
  int index(ParamID id) { return indexFromNorm(id, c.getParamNormalized(id)); }
  void setShortLufs(double lufs) { c.setParamNormalized(kDispLufsShort, normFromLufs(lufs)); }
};

void testParameterStrings() {
  Fixture f;
  ASSERT_TRUE(f.text(kInputGain, 0) == "-12.0 dB");
  ASSERT_TRUE(f.text(kInputGain, 24) == "+0.0 dB");
  ASSERT_TRUE(f.text(kCeiling, 1) == "-0.3 dB");
  ASSERT_TRUE(f.text(kTarget, 3) == "-7 LUFS");
  ASSERT_TRUE(f.text(kOversampling, 0) == "Off" && f.text(kOversampling, 2) == "4x");
  ASSERT_TRUE(f.text(kBypass, 0) == "Off" && f.text(kBypass, 1) == "On");
  ASSERT_TRUE(f.text(kHmOn, 1) == "On");
  ASSERT_TRUE(f.text(kLmMs, 1) == "Mid" && f.text(kLmMs, 2) == "Side");
  ASSERT_TRUE(f.text(kMidFreq, 4) == "1 kHz" && f.text(kLmFreq, 4) == "250 Hz" && f.text(kHsFreq, 6) == "20 kHz");
  ASSERT_TRUE(f.text(kLmQ, 3) == "Surgical");
  ASSERT_TRUE(f.text(kGlueRelease, 4) == "Auto" && f.text(kGlueRelease, 1) == "0.3 s");
  ASSERT_TRUE(f.text(kGlueRatio, 2) == "4:1" && f.text(kGlueMix, 10) == "50 %");
  ASSERT_TRUE(f.text(kFinalMode, 0) == "Inflate" && f.text(kFinalMode, 1) == "Clip");
  ASSERT_TRUE(f.text(kLimitStyle, 1) == "Punch" && f.text(kLimitRelease, 4) == "Style" && f.text(kLimitRelease, 6) == "+2");
  ASSERT_TRUE(f.text(kLoudness, 7) == "3.5" && f.text(kTilt, 0) == "-3.0 dB");
}

void testHiPassFrequencyFollowsItsMsPosition() {
  Fixture f;
  ASSERT_TRUE(f.text(kHpFreq, 0) == "Off" && f.text(kHpFreq, 1) == "20 Hz");
  f.setIndex(kHpMs, 2);  // SIDE: the mono-bass scale
  ASSERT_TRUE(f.text(kHpFreq, 1) == "60 Hz" && f.text(kHpFreq, 5) == "200 Hz");
  f.setIndex(kHpMs, 1);
  ASSERT_TRUE(f.text(kHpFreq, 1) == "20 Hz");
}

void testTypedValues() {
  Fixture f;
  const auto parse = [&](ParamID id, const char* s, double& out) {
    Steinberg::Vst::TChar t[64];
    int i = 0;
    for (; s[i]; ++i) t[i] = static_cast<Steinberg::Vst::TChar>(s[i]);
    t[i] = 0;
    return f.c.getParamValueByString(id, t, out) == Steinberg::kResultOk;
  };
  double v = -1;
  ASSERT_TRUE(parse(kLmQ, "surgical", v) && indexFromNorm(kLmQ, v) == 3);
  ASSERT_TRUE(parse(kInputGain, "+3.0 dB", v) && indexFromNorm(kInputGain, v) == 30);
  ASSERT_TRUE(parse(kInputGain, "-3", v) && indexFromNorm(kInputGain, v) == 18);   // nearest table value
  ASSERT_TRUE(parse(kBypass, "on", v) && v == 1.0);
  ASSERT_TRUE(!parse(kLmQ, "bogus", v));
}

void testParametersReadBackAtTheSameIndex() {
  // every position survives the controller's parameter object and reads back as the same index
  Fixture f;
  for (int i = 0; i < kNumParams; ++i) {
    const auto id = static_cast<ParamID>(i);
    for (int s = 0; s < kParams[id].steps; ++s) {
      f.c.setParamNormalized(id, normFromIndex(id, s));
      ASSERT_TRUE(indexFromNorm(id, f.c.getParamNormalized(id)) == s);
    }
    f.c.setParamNormalized(id, normFromIndex(id, kParams[id].defaultIndex));
  }
}

void testMatchMovesLoudnessOnceAsOneHostEdit() {
  Fixture f;
  f.setIndex(kTarget, 1);          // -11 LUFS
  f.setIndex(kLoudness, 4);
  f.setShortLufs(-14.0);           // 3 LU under the target
  const int expected = matchLoudnessIndex(4, -14.0, -11.0);
  ASSERT_TRUE(expected > 4);
  f.c.doMatch();
  ASSERT_TRUE(f.index(kLoudness) == expected);
  ASSERT_TRUE(f.h.log.size() == 3);
  ASSERT_TRUE(f.h.log[0] == "begin:" + std::to_string(kLoudness));
  ASSERT_TRUE(f.h.log[1] == "perform:" + std::to_string(kLoudness));
  ASSERT_TRUE(f.h.log[2] == "end:" + std::to_string(kLoudness));
  ASSERT_TRUE(f.h.performed.size() == 1 && indexFromNorm(kLoudness, f.h.performed[0]) == expected);
  ASSERT_TRUE(f.h.restarts.empty());
}

void testMatchDoesNothingWithoutMeasurementOrWhenCentred() {
  Fixture f;
  f.setIndex(kLoudness, 6);
  f.c.doMatch();                   // short-term still "no measurement" (display value 0)
  ASSERT_TRUE(f.index(kLoudness) == 6 && f.h.log.empty());
  f.setIndex(kTarget, 0);
  f.setShortLufs(-14.0);           // right on the target
  f.c.doMatch();
  ASSERT_TRUE(f.index(kLoudness) == 6 && f.h.log.empty());
}

void testMatchClampsAtTheEndsOfTheKnob() {
  Fixture f;
  f.setIndex(kTarget, 3);          // -7 LUFS, far above
  f.setIndex(kLoudness, 18);
  f.setShortLufs(-30.0);
  f.c.doMatch();
  ASSERT_TRUE(f.index(kLoudness) == kLoudnessMaxIndex);
  f.setIndex(kTarget, 0);
  f.setIndex(kLoudness, 8);
  f.setShortLufs(-2.0);            // 12 LU above -14: 16 steps down from 8, stops at 0
  f.c.doMatch();
  ASSERT_TRUE(f.index(kLoudness) == 0);
}

void testComponentStateLoadsParamsAndResetsSessionParameters() {
  Fixture f;
  f.c.setParamNormalized(kListenBand, normFromListen(1));
  f.c.setParamNormalized(kSetActive, 1.0);
  double norm[kNumParams];
  loadDefaults(norm);
  norm[kCeiling] = normFromIndex(kCeiling, 3);
  norm[kOversampling] = normFromIndex(kOversampling, 2);
  VecStream s;
  serializeState(norm, s.data);
  ASSERT_TRUE(f.c.setComponentState(&s) == Steinberg::kResultOk);
  ASSERT_TRUE(f.index(kCeiling) == 3 && f.index(kOversampling) == 2);
  ASSERT_TRUE(listenFromNorm(f.c.getParamNormalized(kListenBand)) == -1);
  ASSERT_TRUE(f.c.getParamNormalized(kSetActive) == 0.0);
  ASSERT_TRUE(f.h.restarts.size() == 1 && f.h.restarts[0] == Steinberg::Vst::kLatencyChanged);  // OVERSAMPLING 2x -> 4x

  VecStream bad;
  bad.data = s.data;
  bad.data[0] = 7;
  ASSERT_TRUE(f.c.setComponentState(&bad) == Steinberg::kResultFalse);
  ASSERT_TRUE(f.index(kCeiling) == 3);
}

void testStateLoadRestartsOnlyWhenOversamplingDiffers() {
  Fixture f;
  double norm[kNumParams];
  loadDefaults(norm);
  norm[kOversampling] = normFromIndex(kOversampling, 2);
  VecStream s;
  serializeState(norm, s.data);
  ASSERT_TRUE(f.c.setComponentState(&s) == Steinberg::kResultOk);
  ASSERT_TRUE(f.h.restarts.size() == 1 && f.h.restarts[0] == Steinberg::Vst::kLatencyChanged);  // exactly once
  s.pos = 0;
  ASSERT_TRUE(f.c.setComponentState(&s) == Steinberg::kResultOk);                                 // same value again
  ASSERT_TRUE(f.h.restarts.size() == 1);
  norm[kCeiling] = normFromIndex(kCeiling, 2);                                                   // other change, same OVERSAMPLING
  VecStream t;
  serializeState(norm, t.data);
  ASSERT_TRUE(f.c.setComponentState(&t) == Steinberg::kResultOk);
  ASSERT_TRUE(f.h.restarts.size() == 1);
  norm[kOversampling] = normFromIndex(kOversampling, 1);
  VecStream u;
  serializeState(norm, u.data);
  ASSERT_TRUE(f.c.setComponentState(&u) == Steinberg::kResultOk);
  ASSERT_TRUE(f.h.restarts.size() == 2);
}

void testOversamplingChangeAsksForALatencyRestart() {
  Fixture f;
  f.setIndex(kOversampling, 2);
  ASSERT_TRUE(f.h.restarts.size() == 1 && f.h.restarts[0] == Steinberg::Vst::kLatencyChanged);
  f.setIndex(kOversampling, 2);        // same value: nothing
  ASSERT_TRUE(f.h.restarts.size() == 1);
  f.setIndex(kOversampling, 0);
  ASSERT_TRUE(f.h.restarts.size() == 2);
  f.setIndex(kCeiling, 2);             // other parameters never restart
  f.setIndex(kLimitStyle, 1);
  ASSERT_TRUE(f.h.restarts.size() == 2);
}

void testNoRestartWithoutHandler() {
  Controller c;
  c.initialize(nullptr);
  c.setParamNormalized(kOversampling, 1.0);   // no handler, no peer: must not crash
  ASSERT_TRUE(indexFromNorm(kOversampling, c.getParamNormalized(kOversampling)) == 2);
  c.terminate();
}

} // namespace

int main() {
  RUN_TEST(testParameterStrings);
  RUN_TEST(testHiPassFrequencyFollowsItsMsPosition);
  RUN_TEST(testTypedValues);
  RUN_TEST(testParametersReadBackAtTheSameIndex);
  RUN_TEST(testMatchMovesLoudnessOnceAsOneHostEdit);
  RUN_TEST(testMatchDoesNothingWithoutMeasurementOrWhenCentred);
  RUN_TEST(testMatchClampsAtTheEndsOfTheKnob);
  RUN_TEST(testComponentStateLoadsParamsAndResetsSessionParameters);
  RUN_TEST(testStateLoadRestartsOnlyWhenOversamplingDiffers);
  RUN_TEST(testOversamplingChangeAsksForALatencyRestart);
  RUN_TEST(testNoRestartWithoutHandler);
  std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
  return g_failed ? 1 : 0;
}
