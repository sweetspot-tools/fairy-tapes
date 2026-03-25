/**
 * Fairy Tapes · The Stripper — VST3 Edit Controller implementation.
 * No custom UI: host (e.g. Reaper) shows standard parameter list.
 */

#include "controller.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstunits.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "base/source/fstreamer.h"
#include <cstring>
#include <cstdio>

namespace FairyTapes::TheStripper {

namespace {
constexpr int kStateVersion = 7;
constexpr int kNumParamsV5 = 32;   // param count before Drive params
constexpr int kNumParamsV6 = 35;   // v6 had kDriveAmount, kDriveOutput, kDriveMix
constexpr int kOldDriveOutputSlotV6 = 33;
constexpr int kOldDriveMixSlotV6 = 34;
constexpr int kNumParamsV2 = 18;
constexpr int kNumParamsV3 = 32;  // before Compressor Mode
constexpr int kNumParamsV4 = 33;  // before kEqType removal
constexpr int kOldEqTypeSlotV4 = 31;

void asciiToStr128(const char* src, Steinberg::Vst::String128 dst) {
  for (int i = 0; i < 127 && src[i]; ++i)
    dst[i] = static_cast<Steinberg::Vst::TChar>(static_cast<unsigned char>(src[i]));
  dst[127] = 0;
}
} // namespace

Controller::Controller() {
  FUNKNOWN_CTOR
  param_value_[kBypass] = kDefaultBypass;
  param_value_[kStripMode] = kDefaultStripMode;
  param_value_[kInputGain] = kDefaultInputGain;
  param_value_[kOutputGain] = kDefaultOutputGain;
  param_value_[kBypassGainStaging] = kDefaultBypassStage;
  param_value_[kBypassLPHP] = kDefaultBypassStage;
  param_value_[kBypassEQ] = kDefaultBypassStage;
  param_value_[kBypassCompression] = kDefaultBypassStage;
  param_value_[kBypassDrive] = kDefaultBypassStage;
  param_value_[kBypassLimiter] = kDefaultBypassStage;
  param_value_[kBypassGate] = kDefaultBypassStage;
  param_value_[kGainStagingTrim] = kDefaultGainStagingTrim;
  param_value_[kCompressAmount] = kDefaultCompressAmount;
  param_value_[kCompressMix] = kDefaultCompressMix;
  param_value_[kCompressorMode] = kDefaultCompressorMode;
  param_value_[kEqHPFFreq] = kDefaultEqHPFFreq;
  param_value_[kEqHPFOn] = kDefaultEqHPFOn;
  param_value_[kEqLPFFreq] = kDefaultEqLPFFreq;
  param_value_[kEqLPFOn] = kDefaultEqLPFOn;
  param_value_[kEqLFGain] = kDefaultEqLFGain;
  param_value_[kEqLFFreq] = kDefaultEqLFFreq;
  param_value_[kEqLFBell] = kDefaultEqLFBell;
  param_value_[kEqLMGain] = kDefaultEqLMGain;
  param_value_[kEqLMFreq] = kDefaultEqLMFreq;
  param_value_[kEqLMQ] = kDefaultEqLMQ;
  param_value_[kEqHMGain] = kDefaultEqHMGain;
  param_value_[kEqHMFreq] = kDefaultEqHMFreq;
  param_value_[kEqHMQ] = kDefaultEqHMQ;
  param_value_[kEqHFGain] = kDefaultEqHFGain;
  param_value_[kEqHFFreq] = kDefaultEqHFFreq;
  param_value_[kEqHFBell] = kDefaultEqHFBell;
  param_value_[kEqSaturation] = kDefaultEqSaturation;
  param_value_[kDriveAmount] = kDefaultDriveAmount;
}

IMPLEMENT_REFCOUNT(Controller)

Steinberg::tresult PLUGIN_API Controller::queryInterface(const Steinberg::TUID iid, void** obj) {
  QUERY_INTERFACE(iid, obj, Steinberg::FUnknown::iid, Steinberg::Vst::IEditController)
  QUERY_INTERFACE(iid, obj, Steinberg::IPluginBase::iid, Steinberg::IPluginBase)
  QUERY_INTERFACE(iid, obj, Steinberg::Vst::IEditController::iid, Steinberg::Vst::IEditController)
  *obj = nullptr;
  return Steinberg::kNoInterface;
}

Steinberg::tresult PLUGIN_API Controller::setComponentState(Steinberg::IBStream* state) {
  return setState(state);
}

Steinberg::tresult PLUGIN_API Controller::setState(Steinberg::IBStream* state) {
  if (!state)
    return Steinberg::kResultFalse;
  Steinberg::IBStreamer s(state, kLittleEndian);
  Steinberg::int32 version = 0;
  if (s.readInt32(version) == false)
    return Steinberg::kResultFalse;
  if (version == 4) {
    double buf[kNumParamsV4];
    for (int i = 0; i < kNumParamsV4; ++i) {
      if (s.readDouble(buf[i]) == false)
        return Steinberg::kResultFalse;
    }
    int dst = 0;
    for (int i = 0; i < kNumParamsV4 && dst < kNumParams; ++i) {
      if (i == kOldEqTypeSlotV4)
        continue;
      param_value_[dst++] = static_cast<Steinberg::Vst::ParamValue>(buf[i]);
    }
  } else if (version == 5) {
    for (int i = 0; i < kNumParamsV5; ++i) {
      double v = 0.;
      if (s.readDouble(v) == false)
        return Steinberg::kResultFalse;
      param_value_[i] = static_cast<Steinberg::Vst::ParamValue>(v);
    }
    param_value_[kDriveAmount] = kDefaultDriveAmount;
  } else if (version == 6) {
    double buf[kNumParamsV6];
    for (int i = 0; i < kNumParamsV6; ++i) {
      if (s.readDouble(buf[i]) == false)
        return Steinberg::kResultFalse;
    }
    int dst = 0;
    for (int i = 0; i < kNumParamsV6 && dst < kNumParams; ++i) {
      if (i == kOldDriveOutputSlotV6 || i == kOldDriveMixSlotV6)
        continue;
      param_value_[dst++] = static_cast<Steinberg::Vst::ParamValue>(buf[i]);
    }
  } else if (version >= 7) {
    for (int i = 0; i < kNumParams; ++i) {
      double v = 0.;
      if (s.readDouble(v) == false)
        return Steinberg::kResultFalse;
      param_value_[i] = static_cast<Steinberg::Vst::ParamValue>(v);
    }
  } else {
    const int n = (version == 1) ? 3 : (version == 2) ? kNumParamsV2 : kNumParamsV3;
    for (int i = 0; i < n; ++i) {
      double v = 0.;
      if (s.readDouble(v) == false)
        return Steinberg::kResultFalse;
      if (i < kNumParams)
        param_value_[i] = static_cast<Steinberg::Vst::ParamValue>(v);
    }
    if (version == 1) {
      for (int i = 3; i < kNumParams; ++i)
        param_value_[i] = 0.5;
      param_value_[kBypass] = 0.0;
      param_value_[kStripMode] = kDefaultStripMode;
      param_value_[kInputGain] = kDefaultInputGain;
      param_value_[kOutputGain] = kDefaultOutputGain;
      param_value_[kBypassGainStaging] = kDefaultBypassStage;
      param_value_[kBypassLPHP] = kDefaultBypassStage;
      param_value_[kBypassEQ] = kDefaultBypassStage;
      param_value_[kBypassCompression] = kDefaultBypassStage;
      param_value_[kBypassDrive] = kDefaultBypassStage;
      param_value_[kBypassLimiter] = kDefaultBypassStage;
      param_value_[kBypassGate] = kDefaultBypassStage;
      param_value_[kGainStagingTrim] = kDefaultGainStagingTrim;
      param_value_[kCompressAmount] = kDefaultCompressAmount;
      param_value_[kCompressMix] = kDefaultCompressMix;
      param_value_[kCompressorMode] = kDefaultCompressorMode;
    }
    if (version == 3) {
      for (int i = kNumParams - 1; i >= static_cast<int>(kCompressorMode) + 1; --i)
        param_value_[i] = param_value_[i - 1];
      param_value_[kCompressorMode] = kDefaultCompressorMode;
    }
    if (version == 2) {
      param_value_[kEqHPFFreq] = param_value_[14];
      param_value_[kEqHPFOn] = param_value_[15];
      param_value_[kEqLPFFreq] = param_value_[16];
      param_value_[kEqLPFOn] = param_value_[17];
      param_value_[kCompressorMode] = kDefaultCompressorMode;
    }
    for (int i = kNumParamsV2; i < kNumParams; ++i)
      param_value_[i] = 0.5;
    param_value_[kEqHPFOn] = kDefaultEqHPFOn;
    param_value_[kEqLPFOn] = kDefaultEqLPFOn;
    param_value_[kEqLFBell] = kDefaultEqLFBell;
    param_value_[kEqHFBell] = kDefaultEqHFBell;
    param_value_[kEqSaturation] = kDefaultEqSaturation;
    param_value_[kEqHPFFreq] = kDefaultEqHPFFreq;
    param_value_[kEqLPFFreq] = kDefaultEqLPFFreq;
    param_value_[kEqLFGain] = kDefaultEqLFGain;
    param_value_[kEqLFFreq] = kDefaultEqLFFreq;
    param_value_[kEqLMGain] = kDefaultEqLMGain;
    param_value_[kEqLMFreq] = kDefaultEqLMFreq;
    param_value_[kEqLMQ] = kDefaultEqLMQ;
    param_value_[kEqHMGain] = kDefaultEqHMGain;
    param_value_[kEqHMFreq] = kDefaultEqHMFreq;
    param_value_[kEqHMQ] = kDefaultEqHMQ;
    param_value_[kEqHFGain] = kDefaultEqHFGain;
    param_value_[kEqHFFreq] = kDefaultEqHFFreq;
  }
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Controller::getState(Steinberg::IBStream* state) {
  if (!state)
    return Steinberg::kResultFalse;
  Steinberg::IBStreamer s(state, kLittleEndian);
  if (s.writeInt32(kStateVersion) == false)
    return Steinberg::kResultFalse;
  for (int i = 0; i < kNumParams; ++i)
    if (s.writeDouble(static_cast<double>(param_value_[i])) == false)
      return Steinberg::kResultFalse;
  return Steinberg::kResultOk;
}

Steinberg::int32 PLUGIN_API Controller::getParameterCount() {
  return kNumParams;
}

Steinberg::tresult PLUGIN_API Controller::getParameterInfo(Steinberg::int32 index,
                                                           Steinberg::Vst::ParameterInfo& info) {
  if (index < 0 || index >= kNumParams)
    return Steinberg::kResultFalse;
  info.flags = Steinberg::Vst::ParameterInfo::kCanAutomate;
  info.stepCount = 0;
  info.defaultNormalizedValue = 0.5;
  info.unitId = Steinberg::Vst::kRootUnitId;

  switch (index) {
  case kBypass:
    info.stepCount = 1;
    info.defaultNormalizedValue = kDefaultBypass;
    info.flags |= Steinberg::Vst::ParameterInfo::kIsBypass;
    asciiToStr128("Bypass", info.title);
    asciiToStr128("Byp", info.shortTitle);
    asciiToStr128("", info.units);
    break;
  case kStripMode:
    info.stepCount = 1;
    info.defaultNormalizedValue = kDefaultStripMode;
    asciiToStr128("Strip Mode", info.title);
    asciiToStr128("Mode", info.shortTitle);
    asciiToStr128("", info.units);
    break;
  case kInputGain:
    info.defaultNormalizedValue = kDefaultInputGain;
    asciiToStr128("Input Gain", info.title);
    asciiToStr128("In", info.shortTitle);
    asciiToStr128("dB", info.units);
    break;
  case kOutputGain:
    info.defaultNormalizedValue = kDefaultOutputGain;
    asciiToStr128("Output Gain", info.title);
    asciiToStr128("Out", info.shortTitle);
    asciiToStr128("dB", info.units);
    break;
  case kBypassGainStaging:
    info.stepCount = 1;
    info.defaultNormalizedValue = kDefaultBypassStage;
    asciiToStr128("Gain Staging Bypass", info.title);
    asciiToStr128("GS Byp", info.shortTitle);
    asciiToStr128("", info.units);
    break;
  case kBypassLPHP:
    info.stepCount = 1;
    info.defaultNormalizedValue = kDefaultBypassStage;
    asciiToStr128("LP/HP Bypass", info.title);
    asciiToStr128("LPHP Byp", info.shortTitle);
    asciiToStr128("Both modes", info.units);  // Channel: 4K-EQ HPF/LPF; Mix bus: LP/HP active when feasible
    break;
  case kBypassEQ:
    info.stepCount = 1;
    info.defaultNormalizedValue = kDefaultBypassStage;
    asciiToStr128("EQ Bypass", info.title);
    asciiToStr128("EQ Byp", info.shortTitle);
    asciiToStr128("", info.units);
    break;
  case kBypassCompression:
    info.stepCount = 1;
    info.defaultNormalizedValue = kDefaultBypassStage;
    asciiToStr128("Compression Bypass", info.title);
    asciiToStr128("Comp Byp", info.shortTitle);
    asciiToStr128("", info.units);
    break;
  case kBypassDrive:
    info.stepCount = 1;
    info.defaultNormalizedValue = kDefaultBypassStage;
    asciiToStr128("Drive Bypass", info.title);
    asciiToStr128("Drv Byp", info.shortTitle);
    asciiToStr128("", info.units);
    break;
  case kBypassLimiter:
    info.stepCount = 1;
    info.defaultNormalizedValue = kDefaultBypassStage;
    asciiToStr128("Limiter Bypass", info.title);
    asciiToStr128("Lim Byp", info.shortTitle);
    asciiToStr128("", info.units);
    break;
  case kBypassGate:
    info.stepCount = 1;
    info.defaultNormalizedValue = kDefaultBypassStage;
    asciiToStr128("Gate Bypass", info.title);
    asciiToStr128("Gate Byp", info.shortTitle);
    asciiToStr128("", info.units);
    break;
  case kGainStagingTrim:
    info.defaultNormalizedValue = kDefaultGainStagingTrim;
    asciiToStr128("Gain Staging Trim", info.title);
    asciiToStr128("GS Trim", info.shortTitle);
    asciiToStr128("", info.units);
    break;
  case kCompressAmount:
    info.defaultNormalizedValue = kDefaultCompressAmount;
    asciiToStr128("Compress Amount", info.title);
    asciiToStr128("Amount", info.shortTitle);
    asciiToStr128("", info.units);
    break;
  case kCompressMix:
    info.defaultNormalizedValue = kDefaultCompressMix;
    asciiToStr128("Compress Mix", info.title);
    asciiToStr128("Mix", info.shortTitle);
    asciiToStr128("", info.units);
    break;
  case kCompressorMode:
    info.stepCount = 2;
    info.defaultNormalizedValue = kDefaultCompressorMode;
    asciiToStr128("Compressor Mode", info.title);
    asciiToStr128("Comp Mode", info.shortTitle);
    asciiToStr128("", info.units);
    break;
  case kEqHPFFreq:
    info.defaultNormalizedValue = kDefaultEqHPFFreq;
    asciiToStr128("EQ HPF Freq", info.title);
    asciiToStr128("HPF Hz", info.shortTitle);
    asciiToStr128("Hz", info.units);
    break;
  case kEqHPFOn:
    info.stepCount = 1;
    info.defaultNormalizedValue = kDefaultEqHPFOn;
    asciiToStr128("EQ HPF On", info.title);
    asciiToStr128("HPF", info.shortTitle);
    asciiToStr128("", info.units);
    break;
  case kEqLPFFreq:
    info.defaultNormalizedValue = kDefaultEqLPFFreq;
    asciiToStr128("EQ LPF Freq", info.title);
    asciiToStr128("LPF Hz", info.shortTitle);
    asciiToStr128("Hz", info.units);
    break;
  case kEqLPFOn:
    info.stepCount = 1;
    info.defaultNormalizedValue = kDefaultEqLPFOn;
    asciiToStr128("EQ LPF On", info.title);
    asciiToStr128("LPF", info.shortTitle);
    asciiToStr128("", info.units);
    break;
  case kEqLFGain:
    info.defaultNormalizedValue = kDefaultEqLFGain;
    asciiToStr128("EQ LF Gain", info.title);
    asciiToStr128("LF dB", info.shortTitle);
    asciiToStr128("dB", info.units);
    break;
  case kEqLFFreq:
    info.defaultNormalizedValue = kDefaultEqLFFreq;
    asciiToStr128("EQ LF Freq", info.title);
    asciiToStr128("LF Hz", info.shortTitle);
    asciiToStr128("Hz", info.units);
    break;
  case kEqLFBell:
    info.stepCount = 1;
    info.defaultNormalizedValue = kDefaultEqLFBell;
    asciiToStr128("EQ LF Bell", info.title);
    asciiToStr128("LF Bell", info.shortTitle);
    asciiToStr128("", info.units);
    break;
  case kEqLMGain:
    info.defaultNormalizedValue = kDefaultEqLMGain;
    asciiToStr128("EQ LM Gain", info.title);
    asciiToStr128("LM dB", info.shortTitle);
    asciiToStr128("dB", info.units);
    break;
  case kEqLMFreq:
    info.defaultNormalizedValue = kDefaultEqLMFreq;
    asciiToStr128("EQ LM Freq", info.title);
    asciiToStr128("LM Hz", info.shortTitle);
    asciiToStr128("Hz", info.units);
    break;
  case kEqLMQ:
    info.defaultNormalizedValue = kDefaultEqLMQ;
    asciiToStr128("EQ LM Q", info.title);
    asciiToStr128("LM Q", info.shortTitle);
    asciiToStr128("", info.units);
    break;
  case kEqHMGain:
    info.defaultNormalizedValue = kDefaultEqHMGain;
    asciiToStr128("EQ HM Gain", info.title);
    asciiToStr128("HM dB", info.shortTitle);
    asciiToStr128("dB", info.units);
    break;
  case kEqHMFreq:
    info.defaultNormalizedValue = kDefaultEqHMFreq;
    asciiToStr128("EQ HM Freq", info.title);
    asciiToStr128("HM Hz", info.shortTitle);
    asciiToStr128("Hz", info.units);
    break;
  case kEqHMQ:
    info.defaultNormalizedValue = kDefaultEqHMQ;
    asciiToStr128("EQ HM Q", info.title);
    asciiToStr128("HM Q", info.shortTitle);
    asciiToStr128("", info.units);
    break;
  case kEqHFGain:
    info.defaultNormalizedValue = kDefaultEqHFGain;
    asciiToStr128("EQ HF Gain", info.title);
    asciiToStr128("HF dB", info.shortTitle);
    asciiToStr128("dB", info.units);
    break;
  case kEqHFFreq:
    info.defaultNormalizedValue = kDefaultEqHFFreq;
    asciiToStr128("EQ HF Freq", info.title);
    asciiToStr128("HF Hz", info.shortTitle);
    asciiToStr128("Hz", info.units);
    break;
  case kEqHFBell:
    info.stepCount = 1;
    info.defaultNormalizedValue = kDefaultEqHFBell;
    asciiToStr128("EQ HF Bell", info.title);
    asciiToStr128("HF Bell", info.shortTitle);
    asciiToStr128("", info.units);
    break;
  case kEqSaturation:
    info.defaultNormalizedValue = kDefaultEqSaturation;
    asciiToStr128("EQ Saturation", info.title);
    asciiToStr128("Sat", info.shortTitle);
    asciiToStr128("%", info.units);
    break;
  case kDriveAmount:
    info.defaultNormalizedValue = kDefaultDriveAmount;
    asciiToStr128("Drive", info.title);
    asciiToStr128("Drive", info.shortTitle);
    asciiToStr128("", info.units);
    break;
  default:
    return Steinberg::kResultFalse;
  }
  info.id = static_cast<Steinberg::Vst::ParamID>(index);
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Controller::getParamStringByValue(Steinberg::Vst::ParamID tag,
    Steinberg::Vst::ParamValue valueNormalized, Steinberg::Vst::String128 string) {
  char buf[128];
  switch (tag) {
  case kBypass:
    asciiToStr128(valueNormalized >= 0.5 ? "On" : "Off", string);
    return Steinberg::kResultOk;
  case kStripMode:
    asciiToStr128(valueNormalized >= 0.5 ? "Mix bus" : "Channel", string);
    return Steinberg::kResultOk;
  case kInputGain:
  case kOutputGain: {
    double db = normalizedToDb(valueNormalized);
    snprintf(buf, sizeof(buf), "%+.1f dB", db);
    asciiToStr128(buf, string);
    return Steinberg::kResultOk;
  }
  case kBypassGainStaging:
  case kBypassLPHP:
  case kBypassEQ:
  case kBypassCompression:
  case kBypassDrive:
  case kBypassLimiter:
  case kBypassGate:
    asciiToStr128(valueNormalized >= 0.5 ? "Bypass" : "On", string);
    return Steinberg::kResultOk;
  case kGainStagingTrim:
    snprintf(buf, sizeof(buf), "%.0f%%", valueNormalized * 100.0);
    asciiToStr128(buf, string);
    return Steinberg::kResultOk;
  case kCompressAmount:
    snprintf(buf, sizeof(buf), "%.0f%%", valueNormalized * 100.0);
    asciiToStr128(buf, string);
    return Steinberg::kResultOk;
  case kCompressMix:
    snprintf(buf, sizeof(buf), "%.0f%%", valueNormalized * 100.0);
    asciiToStr128(buf, string);
    return Steinberg::kResultOk;
  case kCompressorMode: {
    const int step = (valueNormalized < 0.25f) ? 0 : (valueNormalized < 0.75f) ? 1 : 2;
    const char* label = (step == 0) ? "Voice" : (step == 1) ? "Drums" : "Classic";
    asciiToStr128(label, string);
    return Steinberg::kResultOk;
  }
  case kEqHPFFreq:
    snprintf(buf, sizeof(buf), "%.0f Hz", eqNormToHPFFreq(valueNormalized));
    asciiToStr128(buf, string);
    return Steinberg::kResultOk;
  case kEqHPFOn:
    asciiToStr128(valueNormalized >= 0.5 ? "On" : "Off", string);
    return Steinberg::kResultOk;
  case kEqLPFFreq:
    snprintf(buf, sizeof(buf), "%.0f Hz", eqNormToLPFFreq(valueNormalized));
    asciiToStr128(buf, string);
    return Steinberg::kResultOk;
  case kEqLPFOn:
    asciiToStr128(valueNormalized >= 0.5 ? "On" : "Off", string);
    return Steinberg::kResultOk;
  case kEqLFGain:
  case kEqLMGain:
  case kEqHMGain:
  case kEqHFGain:
    snprintf(buf, sizeof(buf), "%+.1f dB", eqNormToGainDb(valueNormalized));
    asciiToStr128(buf, string);
    return Steinberg::kResultOk;
  case kEqLFFreq:
    snprintf(buf, sizeof(buf), "%.0f Hz", eqNormToLFFreq(valueNormalized));
    asciiToStr128(buf, string);
    return Steinberg::kResultOk;
  case kEqLMFreq:
    snprintf(buf, sizeof(buf), "%.0f Hz", eqNormToLMFreq(valueNormalized));
    asciiToStr128(buf, string);
    return Steinberg::kResultOk;
  case kEqHMFreq:
    snprintf(buf, sizeof(buf), "%.0f Hz", eqNormToHMFreq(valueNormalized));
    asciiToStr128(buf, string);
    return Steinberg::kResultOk;
  case kEqHFFreq:
    snprintf(buf, sizeof(buf), "%.0f Hz", eqNormToHFFreq(valueNormalized));
    asciiToStr128(buf, string);
    return Steinberg::kResultOk;
  case kEqLFBell:
  case kEqHFBell:
    asciiToStr128(valueNormalized >= 0.5 ? "Bell" : "Shelf", string);
    return Steinberg::kResultOk;
  case kEqLMQ:
  case kEqHMQ:
    snprintf(buf, sizeof(buf), "%.2f", eqNormToQ(valueNormalized));
    asciiToStr128(buf, string);
    return Steinberg::kResultOk;
  case kEqSaturation:
    snprintf(buf, sizeof(buf), "%.0f%%", valueNormalized * 100.0);
    asciiToStr128(buf, string);
    return Steinberg::kResultOk;
  case kDriveAmount:
    snprintf(buf, sizeof(buf), "%.0f%%", valueNormalized * 100.0);
    asciiToStr128(buf, string);
    return Steinberg::kResultOk;
  default:
    string[0] = 0;
    return Steinberg::kResultFalse;
  }
}

Steinberg::tresult PLUGIN_API Controller::getParamValueByString(Steinberg::Vst::ParamID tag,
    Steinberg::Vst::TChar* string, Steinberg::Vst::ParamValue& valueNormalized) {
  (void)tag;
  (void)string;
  valueNormalized = 0.;
  return Steinberg::kResultFalse;
}

Steinberg::Vst::ParamValue PLUGIN_API Controller::normalizedParamToPlain(
    Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue valueNormalized) {
  if (tag == kInputGain || tag == kOutputGain)
    return static_cast<Steinberg::Vst::ParamValue>(normalizedToDb(valueNormalized));
  return valueNormalized;
}

Steinberg::Vst::ParamValue PLUGIN_API Controller::plainParamToNormalized(
    Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue plainValue) {
  if (tag == kInputGain || tag == kOutputGain)
    return dbToNormalized(static_cast<double>(plainValue));
  return plainValue;
}

Steinberg::Vst::ParamValue PLUGIN_API Controller::getParamNormalized(Steinberg::Vst::ParamID tag) {
  if (tag >= 0 && tag < kNumParams)
    return param_value_[tag];
  return 0.;
}

Steinberg::tresult PLUGIN_API Controller::setParamNormalized(Steinberg::Vst::ParamID tag,
    Steinberg::Vst::ParamValue value) {
  if (tag < 0 || tag >= kNumParams)
    return Steinberg::kResultFalse;
  if (tag == kCompressorMode) {
    value = (value < 0.25) ? 0.0 : (value < 0.75) ? 0.5 : 1.0;
  }
  param_value_[tag] = value;
  if (component_handler_) {
    component_handler_->beginEdit(tag);
    component_handler_->performEdit(tag, value);
    component_handler_->endEdit(tag);
  }
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Controller::setComponentHandler(
    Steinberg::Vst::IComponentHandler* handler) {
  component_handler_ = handler;
  return Steinberg::kResultOk;
}

Steinberg::IPlugView* PLUGIN_API Controller::createView(Steinberg::FIDString name) {
  (void)name;
  return nullptr;
}

Steinberg::tresult PLUGIN_API Controller::initialize(Steinberg::FUnknown* context) {
  (void)context;
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Controller::terminate() {
  component_handler_.reset();
  return Steinberg::kResultOk;
}

} // namespace FairyTapes::TheStripper
