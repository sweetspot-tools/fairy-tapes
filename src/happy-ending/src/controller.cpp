/**
 * Fairy Tapes · Happy Ending — VST3 Edit Controller implementation.
 */

#include "controller.h"
#include "display.h"
#include "loudness_macro.h"
#include "messages.h"
#include "state.h"
#include "state_stream.h"
#include "pluginterfaces/base/ibstream.h"
#include "public.sdk/source/vst/vstparameters.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace FairyTapes::HappyEnding {

namespace {
using Steinberg::Vst::ParameterInfo;

std::vector<Steinberg::Vst::TChar> toTChars(const char* s) {
  std::vector<Steinberg::Vst::TChar> out;
  for (; *s; ++s) out.push_back(static_cast<Steinberg::Vst::TChar>(static_cast<unsigned char>(*s)));
  out.push_back(0);
  return out;
}

void asciiToStr128(const std::string& src, Steinberg::Vst::String128 dst) {
  std::size_t i = 0;
  for (; i < 127 && i < src.size(); ++i) dst[i] = static_cast<Steinberg::Vst::TChar>(static_cast<unsigned char>(src[i]));
  dst[i] = 0;
}

std::string fmt(const char* f, double v) {
  char buf[64];
  std::snprintf(buf, sizeof buf, f, v);
  return buf;
}

std::string freqText(double hz) {
  if (hz < 1000.0) return fmt("%g Hz", hz);
  return fmt("%g kHz", hz / 1000.0);
}

bool isSwitch(ParamID id) { return kParams[id].steps == 2 && id != kFinalMode; }

// The text of position `index`. hpMs: the current HI PASS M/S position (SIDE reads the mono-bass scale).
std::string paramText(ParamID id, int index, int hpMs) {
  const int last = kParams[id].steps - 1;
  index = std::clamp(index, 0, last);
  if (isSwitch(id)) return index ? "On" : "Off";
  const ValueTable t = valueTable(id);
  const double v = t.size ? t.data[index] : 0.0;
  switch (id) {
    case kInputGain: case kOutputGain:
    case kLsGain: case kLmGain: case kMidGain: case kHmGain: case kHsGain:
    case kTilt:
      return fmt("%+.1f dB", v);
    case kCeiling: case kGlueMakeup: case kClipDrive: case kLimitThresh: case kFinalDrive: case kPush:
      return fmt("%.1f dB", v);
    case kGlueThresh:
      return fmt("%.0f dB", v);
    case kLoudness:
      return fmt("%.1f", v);
    case kLow: case kAir: case kTube:
      return fmt("%.0f", v);
    case kTarget:
      return fmt("%.0f LUFS", v);
    case kOversampling: {
      static const char* const names[3] = {"Off", "2x", "4x"};
      return names[index];
    }
    case kHpMs: case kLsMs: case kLmMs: case kMidMs: case kHmMs: case kHsMs: {
      static const char* const names[3] = {"Stereo", "Mid", "Side"};
      return names[index];
    }
    case kHpFreq: {
      const double hz = hpFreqHz(hpMs == 2, index);
      return hz <= 0.0 ? "Off" : freqText(hz);
    }
    case kLsFreq: case kLmFreq: case kMidFreq: case kHmFreq: case kHsFreq:
    case kLowFreq: case kAirFreq:
      return freqText(v);
    case kLmQ: case kMidQ: case kHmQ: {
      static const char* const names[4] = {"Wide", "Medium", "Narrow", "Surgical"};
      return names[index];
    }
    case kGlueAttack:  return fmt("%g ms", v);
    case kGlueRelease: return v <= 0.0 ? "Auto" : fmt("%g s", v);
    case kGlueRatio:   return fmt("%g:1", v);
    case kGlueMix: case kFinalEffect: case kFinalCurve:
      return fmt("%.0f %%", v);
    case kGlueScHpf:   return v <= 0.0 ? "Off" : freqText(v);
    case kLimitStyle: {
      static const char* const names[3] = {"Modern", "Punch", "Dyn"};
      return names[index];
    }
    case kLimitRelease: return index == 4 ? "Style" : fmt("%+.0f", static_cast<double>(index - 4));
    case kFinalMode:   return index ? "Clip" : "Inflate";
    default:           return fmt("%.0f", static_cast<double>(index));
  }
}

std::string lowered(std::string s) {
  s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c); }), s.end());
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
} // namespace

Steinberg::tresult PLUGIN_API Controller::initialize(Steinberg::FUnknown* context) {
  auto result = EditController::initialize(context);
  if (result != Steinberg::kResultOk)
    return result;

  // Registered in ParamID order so that index == id.
  for (int i = 0; i < static_cast<int>(kNumParams); ++i) {
    const auto id = static_cast<ParamID>(i);
    const ParamSpec& spec = kParams[id];
    Steinberg::int32 flags = isAutomatable(id) ? ParameterInfo::kCanAutomate : 0;
    if (id == kBypass) flags |= ParameterInfo::kIsBypass;
    const auto title = toTChars(spec.name);
    parameters.addParameter(title.data(), nullptr, spec.steps - 1, normFromIndex(id, spec.defaultIndex), flags, id);
  }

  // Session parameters: hidden, not automatable, not stored.
  parameters.addParameter(STR16("Listen"), nullptr, kListenSteps - 1, kDefaultListenNorm, ParameterInfo::kIsHidden, kListenBand);
  parameters.addParameter(STR16("Set"), nullptr, 1, kDefaultSetNorm, ParameterInfo::kIsHidden, kSetActive);

  // Display parameters (processor -> editor): hidden and read-only.
  const auto display = ParameterInfo::kIsReadOnly | ParameterInfo::kIsHidden;
  const auto addDisplay = [&](const char* name, Steinberg::int32 steps, Steinberg::Vst::ParamID id) {
    const auto title = toTChars(name);
    parameters.addParameter(title.data(), nullptr, steps, 0.0, display, id);
  };
  addDisplay("Display In L", 0, kDispInL);
  addDisplay("Display In R", 0, kDispInR);
  addDisplay("Display Out L", 0, kDispOutL);
  addDisplay("Display Out R", 0, kDispOutR);
  static const char* const grNames[kNumGrRows] = {"Glue", "Clip", "Limit", "Final", "Metalhead"};
  for (int r = 0; r < kNumGrRows; ++r)
    addDisplay((std::string("Display GR ") + grNames[r]).c_str(), 0, kDispGr0 + static_cast<Steinberg::Vst::ParamID>(r));
  static const char* const ledNames[kNumLeds] = {"In", "Fine EQ", "Vibe EQ", "Glue", "Clip", "Limit", "Final", "Metalhead", "Out"};
  for (int l = 0; l < kNumLeds; ++l)
    addDisplay((std::string("Display Clip ") + ledNames[l]).c_str(), 1, kDispClip0 + static_cast<Steinberg::Vst::ParamID>(l));
  addDisplay("Display LUFS Short", 0, kDispLufsShort);
  addDisplay("Display LUFS Integrated", 0, kDispLufsInt);
  addDisplay("Display Dyn Low", 0, kDispDynLow);
  addDisplay("Display Dyn Air", 0, kDispDynAir);
  addDisplay("Display Latency", 0, kDispLatency);
  // Writable (no kIsReadOnly): the editor sets 1, the processor acts and writes 0 back.
  parameters.addParameter(STR16("Clear Clips"), nullptr, 1, 0.0, ParameterInfo::kIsHidden, kClearClips);
  parameters.addParameter(STR16("Reset Integrated"), nullptr, 1, 0.0, ParameterInfo::kIsHidden, kResetIntegrated);

  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Controller::setComponentState(Steinberg::IBStream* state) {
  std::vector<std::uint8_t> bytes;
  if (!readStateBytes(state, bytes))
    return Steinberg::kResultFalse;
  double norm[kNumParams];
  if (!deserializeState(bytes.data(), bytes.size(), norm))
    return Steinberg::kResultFalse;
  loadingState_ = true;  // a load is not a user change of OVERSAMPLING: no restart request
  for (int i = 0; i < static_cast<int>(kNumParams); ++i)
    setParamNormalized(static_cast<Steinberg::Vst::ParamID>(i), norm[i]);
  setParamNormalized(kListenBand, kDefaultListenNorm);  // session parameters are never stored
  setParamNormalized(kSetActive, kDefaultSetNorm);
  loadingState_ = false;
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Controller::setParamNormalized(Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value) {
  if (tag != kOversampling)
    return EditController::setParamNormalized(tag, value);
  const int before = indexFromNorm(kOversampling, getParamNormalized(kOversampling));
  const auto result = EditController::setParamNormalized(tag, value);
  const int after = indexFromNorm(kOversampling, getParamNormalized(kOversampling));
  if (!loadingState_ && after != before)
    oversamplingChanged(after);
  return result;
}

void Controller::oversamplingChanged(int index) {
  if (getPeer()) {
    if (Steinberg::Vst::IMessage* msg = allocateMessage()) {
      encodeOversamplingMessage(msg, index);
      sendMessage(msg);
      msg->release();
    }
  }
  // The zones are rebuilt when the host re-activates the processor; the host re-reads the latency then.
  if (componentHandler)
    componentHandler->restartComponent(Steinberg::Vst::kLatencyChanged);
}

void Controller::doMatch() {
  const double lufs = lufsFromNorm(getParamNormalized(kDispLufsShort));
  const int current = indexFromNorm(kLoudness, getParamNormalized(kLoudness));
  const int target = indexFromNorm(kTarget, getParamNormalized(kTarget));
  const int next = matchLoudnessIndex(current, lufs, Tables::kTargets[static_cast<std::size_t>(target)]);
  if (next == current)
    return;
  const double norm = normFromIndex(kLoudness, next);
  beginEdit(kLoudness);
  setParamNormalized(kLoudness, norm);
  performEdit(kLoudness, norm);
  endEdit(kLoudness);
}

Steinberg::tresult PLUGIN_API Controller::getParamStringByValue(Steinberg::Vst::ParamID tag,
    Steinberg::Vst::ParamValue v, Steinberg::Vst::String128 string) {
  if (tag >= kNumParams)
    return EditController::getParamStringByValue(tag, v, string);
  const auto id = static_cast<ParamID>(tag);
  const int hpMs = indexFromNorm(kHpMs, getParamNormalized(kHpMs));
  asciiToStr128(paramText(id, indexFromNorm(id, v), hpMs), string);
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Controller::getParamValueByString(Steinberg::Vst::ParamID tag,
    Steinberg::Vst::TChar* string, Steinberg::Vst::ParamValue& valueNormalized) {
  if (tag >= kNumParams || !string)
    return EditController::getParamValueByString(tag, string, valueNormalized);
  const auto id = static_cast<ParamID>(tag);
  std::string typed;
  for (const Steinberg::Vst::TChar* c = string; *c; ++c) typed.push_back(*c < 128 ? static_cast<char>(*c) : '?');
  const std::string want = lowered(typed);
  const int hpMs = indexFromNorm(kHpMs, getParamNormalized(kHpMs));
  const int steps = kParams[id].steps;
  for (int i = 0; i < steps; ++i)
    if (lowered(paramText(id, i, hpMs)) == want) {
      valueNormalized = normFromIndex(id, i);
      return Steinberg::kResultOk;
    }
  // A bare number picks the nearest value of the table (e.g. "-3" for a gain)
  const ValueTable t = valueTable(id);
  char* end = nullptr;
  const double num = std::strtod(typed.c_str(), &end);
  if (t.size && end != typed.c_str()) {
    int best = 0;
    for (int i = 1; i < steps; ++i)
      if (std::fabs(t.data[i] - num) < std::fabs(t.data[best] - num)) best = i;
    valueNormalized = normFromIndex(id, best);
    return Steinberg::kResultOk;
  }
  return Steinberg::kResultFalse;
}

} // namespace FairyTapes::HappyEnding
