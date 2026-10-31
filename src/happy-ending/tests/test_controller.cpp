/**
 * Fairy Tapes · Happy Ending — Unit tests for the Edit Controller (compiled in, no module needed): parameter
 * strings and typed values, MATCH as one host edit, the session parameters after a state load, and the
 * OVERSAMPLING change asking the host to restart (latency). Run via CTest; target happy_ending_controller_tests.
 */

#include "controller.h"
#include "display.h"
#include "loudness_macro.h"
#include "messages.h"
#include "state.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmessage.h"
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

// A minimal IMessage: only the id and one binary attribute (what the "Decor" message uses).
struct FakeAttrs : Steinberg::Vst::IAttributeList {
  std::vector<uint8_t> blob;
  bool has = false;
  Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void** o) override { *o = nullptr; return Steinberg::kNoInterface; }
  Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
  Steinberg::uint32 PLUGIN_API release() override { return 1; }
  Steinberg::tresult PLUGIN_API setInt(AttrID, Steinberg::int64) override { return Steinberg::kResultFalse; }
  Steinberg::tresult PLUGIN_API getInt(AttrID, Steinberg::int64&) override { return Steinberg::kResultFalse; }
  Steinberg::tresult PLUGIN_API setFloat(AttrID, double) override { return Steinberg::kResultFalse; }
  Steinberg::tresult PLUGIN_API getFloat(AttrID, double&) override { return Steinberg::kResultFalse; }
  Steinberg::tresult PLUGIN_API setString(AttrID, const Steinberg::Vst::TChar*) override { return Steinberg::kResultFalse; }
  Steinberg::tresult PLUGIN_API getString(AttrID, Steinberg::Vst::TChar*, Steinberg::uint32) override { return Steinberg::kResultFalse; }
  Steinberg::tresult PLUGIN_API setBinary(AttrID id, const void* d, Steinberg::uint32 n) override {
    if (std::strcmp(id, "decor") != 0) return Steinberg::kResultFalse;
    blob.assign(static_cast<const uint8_t*>(d), static_cast<const uint8_t*>(d) + n);
    has = true;
    return Steinberg::kResultOk;
  }
  Steinberg::tresult PLUGIN_API getBinary(AttrID id, const void*& d, Steinberg::uint32& n) override {
    if (std::strcmp(id, "decor") != 0 || !has) return Steinberg::kResultFalse;
    d = blob.data();
    n = static_cast<Steinberg::uint32>(blob.size());
    return Steinberg::kResultOk;
  }
};
struct FakeMessage : Steinberg::Vst::IMessage {
  std::string id;
  FakeAttrs attrs;
  Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void** o) override { *o = nullptr; return Steinberg::kNoInterface; }
  Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
  Steinberg::uint32 PLUGIN_API release() override { return 1; }
  Steinberg::FIDString PLUGIN_API getMessageID() override { return id.c_str(); }
  void PLUGIN_API setMessageID(Steinberg::FIDString s) override { id = s; }
  Steinberg::Vst::IAttributeList* PLUGIN_API getAttributes() override { return &attrs; }
};

bool sameDecor(const Decor& a, const Decor& b) {
  if (a.seed != b.seed || a.tapeTexts != b.tapeTexts) return false;
  for (int i = 0; i < kNumScrews; ++i)
    if (a.screwAngles[i] != b.screwAngles[i]) return false;
  return true;
}

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
  // a bare number on HI PASS FREQ is read on the scale of the current HI PASS M/S
  f.setIndex(kHpMs, 0);
  ASSERT_TRUE(parse(kHpFreq, "35", v) && indexFromNorm(kHpFreq, v) == 4);   // 35 Hz on ST
  f.setIndex(kHpMs, 2);
  ASSERT_TRUE(parse(kHpFreq, "60", v) && indexFromNorm(kHpFreq, v) == 1);   // 60 Hz on SIDE
  ASSERT_TRUE(parse(kHpFreq, "160", v) && indexFromNorm(kHpFreq, v) == 4);
  ASSERT_TRUE(parse(kHpFreq, "80 Hz", v) && indexFromNorm(kHpFreq, v) == 2);
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
  serializeState(norm, Decor{}, s.data);
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
  serializeState(norm, Decor{}, s.data);
  ASSERT_TRUE(f.c.setComponentState(&s) == Steinberg::kResultOk);
  ASSERT_TRUE(f.h.restarts.size() == 1 && f.h.restarts[0] == Steinberg::Vst::kLatencyChanged);  // exactly once
  s.pos = 0;
  ASSERT_TRUE(f.c.setComponentState(&s) == Steinberg::kResultOk);                                 // same value again
  ASSERT_TRUE(f.h.restarts.size() == 1);
  norm[kCeiling] = normFromIndex(kCeiling, 2);                                                   // other change, same OVERSAMPLING
  VecStream t;
  serializeState(norm, Decor{}, t.data);
  ASSERT_TRUE(f.c.setComponentState(&t) == Steinberg::kResultOk);
  ASSERT_TRUE(f.h.restarts.size() == 1);
  norm[kOversampling] = normFromIndex(kOversampling, 1);
  VecStream u;
  serializeState(norm, Decor{}, u.data);
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

void testDecorArrivesThroughNotify() {
  Fixture f;
  ASSERT_TRUE(f.c.decor().seed == 0 && f.c.decorRevision() == 0);   // nothing yet: the processor or a state provides it
  Decor sent;
  sent.seed = 4242;
  sent.screwAngles[1] = 33.f;
  sent.tapeTexts = {"IN", "OUT"};
  FakeMessage m;
  encodeDecorMessage(&m, sent);
  ASSERT_TRUE(f.c.notify(&m) == Steinberg::kResultOk);
  ASSERT_TRUE(sameDecor(f.c.decor(), sent));
  const std::uint32_t rev = f.c.decorRevision();
  ASSERT_TRUE(rev != 0);

  // a message with seed 0 never clears the seed; the other fields are taken
  Decor noSeed = sent;
  noSeed.seed = 0;
  noSeed.tapeTexts = {"B"};
  FakeMessage m2;
  encodeDecorMessage(&m2, noSeed);
  ASSERT_TRUE(f.c.notify(&m2) == Steinberg::kResultOk);
  ASSERT_TRUE(f.c.decor().seed == 4242 && f.c.decor().tapeTexts == std::vector<std::string>{"B"});
  ASSERT_TRUE(f.c.decorRevision() != rev);

  // another message, or a corrupt decor, changes nothing
  const std::uint32_t rev2 = f.c.decorRevision();
  FakeMessage other;
  other.id = "Oversampling";
  f.c.notify(&other);
  FakeMessage corrupt;
  encodeDecorMessage(&corrupt, sent);
  corrupt.attrs.blob.resize(corrupt.attrs.blob.size() - 1);
  f.c.notify(&corrupt);
  ASSERT_TRUE(f.c.decor().tapeTexts == std::vector<std::string>{"B"} && f.c.decorRevision() == rev2);
}

void testEditDecorKeepsSanitisesAndBumpsRevision() {
  Fixture f;
  Decor base;
  base.seed = 99;
  FakeMessage m;
  encodeDecorMessage(&m, base);
  f.c.notify(&m);
  const std::uint32_t rev = f.c.decorRevision();
  Decor edit;   // seed 0: keep the current one
  edit.screwAngles[0] = 400.f;
  edit.tapeTexts = {"  a very long tape text  ", "b"};
  f.c.editDecor(edit);
  const Decor got = f.c.decor();
  ASSERT_TRUE(got.seed == 99 && got.screwAngles[0] == 40.f);
  ASSERT_TRUE(got.tapeTexts.size() == 2 && got.tapeTexts[0] == "a very long ta" && got.tapeTexts[1] == "b");
  ASSERT_TRUE(f.c.decorRevision() != rev);

  Controller bare;   // no seed known: an edit gives it one
  bare.initialize(nullptr);
  bare.editDecor(Decor{});
  ASSERT_TRUE(bare.decor().seed != 0);
  bare.terminate();
}

void testComponentStateAdoptsTheDecor() {
  Fixture f;
  double norm[kNumParams];
  loadDefaults(norm);
  Decor d;
  d.seed = 777;
  d.screwAngles[2] = 90.f;
  d.tapeTexts = {"VOX"};
  VecStream s;
  serializeState(norm, d, s.data);
  ASSERT_TRUE(f.c.setComponentState(&s) == Steinberg::kResultOk);
  ASSERT_TRUE(sameDecor(f.c.decor(), d));

  // a v1 state (no seed) keeps the seed already known but drops the old texts and angles
  std::vector<uint8_t> v1;
  detail::putLE(v1, 1u, 4);
  for (int i = 0; i < kNumParams; ++i) {
    std::uint64_t bits;
    std::memcpy(&bits, &norm[i], sizeof bits);
    detail::putLE(v1, bits, 8);
  }
  VecStream s1;
  s1.data = v1;
  ASSERT_TRUE(f.c.setComponentState(&s1) == Steinberg::kResultOk);
  ASSERT_TRUE(f.c.decor().seed == 777 && f.c.decor().tapeTexts.empty() && f.c.decor().screwAngles[2] == 0.f);

  // a decor-corrupt v2 state still loads the parameters
  norm[kCeiling] = normFromIndex(kCeiling, 3);
  VecStream bad;
  serializeState(norm, d, bad.data);
  bad.data.resize(bad.data.size() - 1);
  ASSERT_TRUE(f.c.setComponentState(&bad) == Steinberg::kResultOk);
  ASSERT_TRUE(f.index(kCeiling) == 3 && f.c.decor().seed == 777 && f.c.decor().tapeTexts.empty());

  // a controller that knows no seed generates one for a v1 state
  Fixture g;
  VecStream s2;
  s2.data = v1;
  ASSERT_TRUE(g.c.setComponentState(&s2) == Steinberg::kResultOk);
  ASSERT_TRUE(g.c.decor().seed != 0);
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
  RUN_TEST(testDecorArrivesThroughNotify);
  RUN_TEST(testEditDecorKeepsSanitisesAndBumpsRevision);
  RUN_TEST(testComponentStateAdoptsTheDecor);
  std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
  return g_failed ? 1 : 0;
}
