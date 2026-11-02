/**
 * Fairy Tapes · The Stripper — VST3 Edit Controller implementation.
 * The editor is created in createView (src/ui/editor.cpp); the host's generic list shows the same parameters.
 */

#include "controller.h"
#include "pluginterfaces/base/funknownimpl.h"
#include "state.h"
#include "ui/editor.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstunits.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "base/source/fstreamer.h"
#include <cmath>
#include <cstring>
#include <random>
#include <cstdio>

namespace FairyTapes::TheStripper {

namespace {

void asciiToStr128(const char* src, Steinberg::Vst::String128 dst) {
  for (int i = 0; i < 127 && src[i]; ++i)
    dst[i] = static_cast<Steinberg::Vst::TChar>(static_cast<unsigned char>(src[i]));
  dst[127] = 0;
}
} // namespace

Controller::Controller() {
  FUNKNOWN_CTOR
  double defaults[kNumParams];
  fillDefaultParams(defaults);
  for (int i = 0; i < kNumParams; ++i)
    param_value_[i] = defaults[i];
}

IMPLEMENT_REFCOUNT(Controller)

Steinberg::tresult PLUGIN_API Controller::queryInterface(const Steinberg::TUID iid, void** obj) {
  QUERY_INTERFACE(iid, obj, Steinberg::FUnknown::iid, Steinberg::Vst::IEditController)
  QUERY_INTERFACE(iid, obj, Steinberg::IPluginBase::iid, Steinberg::IPluginBase)
  QUERY_INTERFACE(iid, obj, Steinberg::Vst::IEditController::iid, Steinberg::Vst::IEditController)
  QUERY_INTERFACE(iid, obj, Steinberg::Vst::IConnectionPoint::iid, Steinberg::Vst::IConnectionPoint)
  *obj = nullptr;
  return Steinberg::kNoInterface;
}

Steinberg::tresult PLUGIN_API Controller::setComponentState(Steinberg::IBStream* state) {
  // The processor's state: parameters and model only. The editor data (seed, tape) lives in the controller state.
  return applyState(state, false);
}

Steinberg::tresult PLUGIN_API Controller::setState(Steinberg::IBStream* state) { return applyState(state, true); }

Steinberg::tresult Controller::applyState(Steinberg::IBStream* state, bool withEditorData) {
  if (!state)
    return Steinberg::kResultFalse;
  Steinberg::IBStreamer s(state, kLittleEndian);
  PluginState st;
  if (!readState(s, st))
    return Steinberg::kResultFalse;
  for (int i = 0; i < kNumParams; ++i)
    param_value_[i] = static_cast<Steinberg::Vst::ParamValue>(st.params[i]);
  {
    std::lock_guard<std::mutex> lk(modelMutex_);
    modelPath_ = st.modelPath;
  }
  if (withEditorData) {
    std::lock_guard<std::mutex> lk(uiMutex_);
    if (st.wearSeed != 0)
      wearSeed_ = st.wearSeed;   // a state without a seed (older project) keeps the one made for this instance
    tapeTexts_ = st.tapeTexts;
    hasScrews_ = st.hasScrewAngles;
    for (int i = 0; i < 4; ++i) screws_[i] = st.screwAngles[i];
    recallMode_ = st.recallMode;
    if (recallMode_ == 0) {   // Auto: the marks are the values that were just loaded
      recallValid_ = true;
      for (int i = 0; i < kNumParams; ++i) recall_[i] = st.params[i];
    } else {                  // Manual: the marks stay as they were saved
      recallValid_ = st.recallValid;
      for (int i = 0; i < kNumParams; ++i) recall_[i] = st.recall[i];
    }
  }
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Controller::getState(Steinberg::IBStream* state) {
  if (!state)
    return Steinberg::kResultFalse;
  Steinberg::IBStreamer s(state, kLittleEndian);
  PluginState st;
  for (int i = 0; i < kNumParams; ++i)
    st.params[i] = static_cast<double>(param_value_[i]);
  st.modelPath = modelPath_;
  {
    std::lock_guard<std::mutex> lk(uiMutex_);
    st.wearSeed = wearSeed_;
    st.tapeTexts = tapeTexts_;
    st.hasScrewAngles = hasScrews_;
    for (int i = 0; i < 4; ++i) st.screwAngles[i] = screws_[i];
    st.recallValid = recallValid_;
    for (int i = 0; i < kNumParams; ++i) st.recall[i] = recall_[i];
    st.recallMode = recallMode_;
  }
  return writeState(s, st) ? Steinberg::kResultOk : Steinberg::kResultFalse;
}

std::uint32_t Controller::wearSeed() const {
  std::lock_guard<std::mutex> lk(uiMutex_);
  return wearSeed_;
}

TapeText Controller::tapeText(int index) const {
  std::lock_guard<std::mutex> lk(uiMutex_);
  if (index < 0 || static_cast<std::size_t>(index) >= tapeTexts_.size())
    return {};
  return tapeTexts_[static_cast<std::size_t>(index)];
}

bool Controller::screwAngles(double out[4]) const {
  std::lock_guard<std::mutex> lk(uiMutex_);
  if (!hasScrews_) return false;
  for (int i = 0; i < 4; ++i) out[i] = screws_[i];
  return true;
}

void Controller::setScrewAngles(const double in[4]) {
  std::lock_guard<std::mutex> lk(uiMutex_);
  hasScrews_ = true;
  for (int i = 0; i < 4; ++i) {
    const double a = std::isfinite(in[i]) ? std::fmod(in[i], 360.0) : 0.0;
    screws_[i] = a < 0.0 ? a + 360.0 : a;
  }
}

bool Controller::recallValid() const {
  std::lock_guard<std::mutex> lk(uiMutex_);
  return recallValid_;
}

double Controller::recallValue(Steinberg::Vst::ParamID id) const {
  std::lock_guard<std::mutex> lk(uiMutex_);
  return (recallValid_ && id < kNumParams) ? recall_[id] : 0.0;
}

void Controller::markRecallNow() {
  std::lock_guard<std::mutex> lk(uiMutex_);
  recallValid_ = true;
  for (int i = 0; i < kNumParams; ++i) recall_[i] = param_value_[i];
}

int Controller::recallMode() const {
  std::lock_guard<std::mutex> lk(uiMutex_);
  return recallMode_;
}

void Controller::setRecallMode(int mode) {
  std::lock_guard<std::mutex> lk(uiMutex_);
  recallMode_ = mode == 1 ? 1 : 0;
}

void Controller::rerollWear() {
  std::random_device rd;
  std::lock_guard<std::mutex> lk(uiMutex_);
  const std::uint32_t old = wearSeed_;
  do wearSeed_ = rd(); while (wearSeed_ == 0 || wearSeed_ == old);
  tapeTexts_.clear();
  hasScrews_ = false;   // the screws turn up at new angles, from the new seed
}

void Controller::setTapeText(int index, const std::string& text) {
  if (index < 0 || index >= kMaxTapePieces)
    return;
  std::string cut = text;
  if (cut.size() > static_cast<std::size_t>(kMaxTapeTextBytes)) {
    cut.resize(static_cast<std::size_t>(kMaxTapeTextBytes));
    while (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0xC0) == 0x80)   // not in the middle of a UTF-8 sequence
      cut.pop_back();
    if (!cut.empty() && static_cast<unsigned char>(cut.back()) >= 0xC0)
      cut.pop_back();
  }
  std::lock_guard<std::mutex> lk(uiMutex_);
  if (tapeTexts_.size() <= static_cast<std::size_t>(index))
    tapeTexts_.resize(static_cast<std::size_t>(index) + 1);
  tapeTexts_[static_cast<std::size_t>(index)] = TapeText{true, cut};
}

Steinberg::int32 PLUGIN_API Controller::getParameterCount() {
  return kNumParams + kNumMeters;   // the persistent parameters, then the read-only meter parameters
}

Steinberg::tresult PLUGIN_API Controller::getParameterInfo(Steinberg::int32 index,
                                                           Steinberg::Vst::ParameterInfo& info) {
  if (index >= kNumParams && index < kNumParams + kNumMeters) {
    // Meter parameters: hidden from the host; read-only, except kClearClips (a click on a clip LED writes it).
    const Steinberg::Vst::ParamID id = kMeterBase + static_cast<Steinberg::Vst::ParamID>(index - kNumParams);
    info.id = id;
    info.stepCount = 0;
    info.defaultNormalizedValue = 0.0;
    info.unitId = Steinberg::Vst::kRootUnitId;
    // The gain-reduction parameters are read-only but visible: SSL 360 Link lists them as assignable meter data.
    const bool gainReduction = id == kMeterGrComp || id == kMeterGrLimiter || id == kMeterGrGate;
    info.flags = (gainReduction ? 0 : Steinberg::Vst::ParameterInfo::kIsHidden) |
                 (id == kClearClips ? 0 : Steinberg::Vst::ParameterInfo::kIsReadOnly);
    static const char* const kTitles[] = {"Meter In L", "Meter In R", "Meter Out L", "Meter Out R", "Compressor Gain Reduction", "Limiter Gain Reduction",
                                          "Clip Input", "Clip EQ", "Clip Comp", "Clip Drive", "Clip Limiter", "Clip Output", "Clear Clips", "Gate Gain Reduction"};
    asciiToStr128(kTitles[id - kMeterBase], info.title);
    asciiToStr128(kTitles[id - kMeterBase], info.shortTitle);
    asciiToStr128("", info.units);
    return Steinberg::kResultOk;
  }
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
    asciiToStr128("", info.units);  // HPF/LPF of the 4K-EQ, in both strip modes
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
  case kLimiterPound:
    info.defaultNormalizedValue = kDefaultLimiterPound;
    asciiToStr128("Limiter Pound", info.title);
    asciiToStr128("Pound", info.shortTitle);
    asciiToStr128("", info.units);
    break;
  case kDriveNamOn:
    info.stepCount = 1;
    info.defaultNormalizedValue = kDefaultDriveNamOn;
    asciiToStr128("Drive NAM", info.title);
    asciiToStr128("NAM", info.shortTitle);
    asciiToStr128("", info.units);
    break;
  case kGateThreshold:
    info.defaultNormalizedValue = kDefaultGateThreshold;
    asciiToStr128("Gate Threshold", info.title);
    asciiToStr128("Thresh", info.shortTitle);
    asciiToStr128("", info.units);
    break;
  case kDriveTapeOn:
    info.stepCount = 1;
    info.defaultNormalizedValue = kDefaultDriveTapeOn;
    asciiToStr128("Drive Tape", info.title);
    asciiToStr128("Tape", info.shortTitle);
    asciiToStr128("", info.units);
    break;
  case kDriveTapeAmount:
    info.defaultNormalizedValue = kDefaultDriveTapeAmount;
    asciiToStr128("Tape", info.title);
    asciiToStr128("Tape", info.shortTitle);
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
  if (tag >= kMeterBase && tag < kNumMeterIDs) {
    snprintf(buf, sizeof(buf), "%.0f%%", valueNormalized * 100.0);
    asciiToStr128(buf, string);
    return Steinberg::kResultOk;
  }
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
  case kLimiterPound:
    snprintf(buf, sizeof(buf), "%.1f", valueNormalized * 10.0);
    asciiToStr128(buf, string);
    return Steinberg::kResultOk;
  case kDriveNamOn:
  case kDriveTapeOn:
    asciiToStr128(valueNormalized >= 0.5 ? "On" : "Off", string);
    return Steinberg::kResultOk;
  case kDriveTapeAmount:
    snprintf(buf, sizeof(buf), "%.1f", valueNormalized * 10.0);
    asciiToStr128(buf, string);
    return Steinberg::kResultOk;
  case kGateThreshold:
    snprintf(buf, sizeof(buf), "%.0f dB", -72.0 + valueNormalized * 72.0);
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
  if (tag >= kMeterBase && tag < kNumMeterIDs)
    return meter_value_[tag - kMeterBase];
  if (tag >= 0 && tag < kNumParams)
    return param_value_[tag];
  return 0.;
}

Steinberg::tresult PLUGIN_API Controller::setParamNormalized(Steinberg::Vst::ParamID tag,
    Steinberg::Vst::ParamValue value) {
  if (tag >= kMeterBase && tag < kNumMeterIDs) {
    // Values written by the processor (or the clear request written by the editor). They never feed back into the
    // host as an edit, except the explicit click on a clip LED.
    meter_value_[tag - kMeterBase] = value;
    if (tag == kClearClips && value >= 0.5 && component_handler_) {
      component_handler_->beginEdit(tag);
      component_handler_->performEdit(tag, value);
      component_handler_->endEdit(tag);
    }
    return Steinberg::kResultOk;
  }
  if (tag < 0 || tag >= kNumParams)
    return Steinberg::kResultFalse;
  if (tag == kCompressorMode) {
    value = (value < 0.25) ? 0.0 : (value < 0.75) ? 0.5 : 1.0;
  }
  // Host -> controller (automation, presets, undo): store only. Telling the host back would loop and could
  // overwrite recorded automation; user gestures in the editor go through beginEdit/performEdit/endEdit.
  param_value_[tag] = value;
  return Steinberg::kResultOk;
}

void Controller::beginEdit(Steinberg::Vst::ParamID tag) {
  if (tag < kNumParams && component_handler_)
    component_handler_->beginEdit(tag);
}

void Controller::performEdit(Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value) {
  if (tag >= kNumParams)
    return;
  if (tag == kCompressorMode)
    value = (value < 0.25) ? 0.0 : (value < 0.75) ? 0.5 : 1.0;
  param_value_[tag] = value;
  if (component_handler_)
    component_handler_->performEdit(tag, value);
}

void Controller::endEdit(Steinberg::Vst::ParamID tag) {
  if (tag < kNumParams && component_handler_)
    component_handler_->endEdit(tag);
}

Steinberg::tresult PLUGIN_API Controller::setComponentHandler(
    Steinberg::Vst::IComponentHandler* handler) {
  component_handler_ = handler;
  return Steinberg::kResultOk;
}

Steinberg::IPlugView* PLUGIN_API Controller::createView(Steinberg::FIDString name) {
  if (name && std::strcmp(name, Steinberg::Vst::ViewType::kEditor) == 0)
    return new Ui::Editor(this);
  return nullptr;
}

Steinberg::tresult PLUGIN_API Controller::initialize(Steinberg::FUnknown* context) {
  {
    std::lock_guard<std::mutex> lk(uiMutex_);
    if (wearSeed_ == 0) {   // a fresh instance gets its own wear, saved with the project from now on
      std::random_device rd;
      while (wearSeed_ == 0) wearSeed_ = rd();
    }
  }
  host_ = Steinberg::U::cast<Steinberg::Vst::IHostApplication>(context);   // needed to allocate messages
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Controller::terminate() {
  peer_ = nullptr;
  host_ = nullptr;
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Controller::connect(Steinberg::Vst::IConnectionPoint* other) {
  peer_ = other;
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Controller::disconnect(Steinberg::Vst::IConnectionPoint*) {
  peer_ = nullptr;
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Controller::notify(Steinberg::Vst::IMessage* message) {
  ModelStatus st;
  if (!decodeStatusMessage(message, st))
    return Steinberg::kResultFalse;
  std::lock_guard<std::mutex> lk(modelMutex_);
  modelStatus_ = st;
  return Steinberg::kResultOk;
}

void Controller::requestModelLoad(const std::string& path) {
  {
    std::lock_guard<std::mutex> lk(modelMutex_);
    modelPath_ = path;
    modelStatus_.state = path.empty() ? 0 : 1;   // "loading" until the processor answers
    modelStatus_.gpu = false;
    modelStatus_.error.clear();
    if (path.empty())
      modelStatus_.name.clear();
  }
  if (!peer_ || !host_)
    return;
  if (Steinberg::Vst::IMessage* msg = Steinberg::Vst::allocateMessage(host_)) {
    encodeLoadMessage(msg, path);
    peer_->notify(msg);
    msg->release();
  }
}

std::string Controller::modelPath() const { std::lock_guard<std::mutex> lk(modelMutex_); return modelPath_; }
std::string Controller::modelName() const { std::lock_guard<std::mutex> lk(modelMutex_); return modelStatus_.name; }
std::string Controller::modelError() const { std::lock_guard<std::mutex> lk(modelMutex_); return modelStatus_.error; }
bool Controller::modelGpu() const { std::lock_guard<std::mutex> lk(modelMutex_); return modelStatus_.state == 2 && modelStatus_.gpu; }
int Controller::modelState() const { std::lock_guard<std::mutex> lk(modelMutex_); return modelStatus_.state; }

} // namespace FairyTapes::TheStripper
