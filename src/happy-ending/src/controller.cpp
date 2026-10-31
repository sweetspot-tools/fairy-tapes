/**
 * Fairy Tapes · Happy Ending — VST3 Edit Controller implementation.
 */

#include "controller.h"
#include "display.h"
#include "loudness_macro.h"
#include "messages.h"
#include "state.h"
#include "state_stream.h"
#include "ui/value_text.h"
#include "pluginterfaces/base/ibstream.h"
#include "public.sdk/source/vst/vstparameters.h"
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

// The whole table of normalized values with the current HI PASS M/S (the only one the texts read).
void textNorms(Steinberg::Vst::EditController& c, double* all) {
  loadDefaults(all);
  all[kHpMs] = c.getParamNormalized(kHpMs);
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
  // Stage values for the screen (read-only, after the triggers: the ids above keep their numbers)
  addDisplay("Display Glue In", 0, kDispGlueIn);
  addDisplay("Display Glue GR Before Mix", 0, kDispGlueGr);
  addDisplay("Display Limit In", 0, kDispLimitIn);
  addDisplay("Display Limit Threshold", 0, kDispLimitThresh);
  addDisplay("Display Clip In", 0, kDispClipIn);
  addDisplay("Display Final In", 0, kDispFinalIn);

  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Controller::setComponentState(Steinberg::IBStream* state) {
  std::vector<std::uint8_t> bytes;
  if (!readStateBytes(state, bytes))
    return Steinberg::kResultFalse;
  double norm[kNumParams];
  Decor stored;
  if (!deserializeState(bytes.data(), bytes.size(), norm, stored))
    return Steinberg::kResultFalse;
  const int osBefore = indexFromNorm(kOversampling, getParamNormalized(kOversampling));
  loadingState_ = true;  // one restart request after the whole load, not one per parameter
  for (int i = 0; i < static_cast<int>(kNumParams); ++i)
    setParamNormalized(static_cast<Steinberg::Vst::ParamID>(i), norm[i]);
  setParamNormalized(kListenBand, kDefaultListenNorm);  // session parameters are never stored
  setParamNormalized(kSetActive, kDefaultSetNorm);
  loadingState_ = false;
  // The processor rebuilds its zones only when the host restarts it: ask for that once if OVERSAMPLING changed.
  const int osAfter = indexFromNorm(kOversampling, getParamNormalized(kOversampling));
  if (osAfter != osBefore)
    oversamplingChanged(osAfter);

  // The decor: no seed in the state (v1, or a corrupt decor part): keep the one already known (usually the
  // processor's, sent from its setState); with none, generate one and tell the processor.
  Decor copy;
  bool generated = false;
  {
    std::lock_guard<std::mutex> lk(decorMutex_);
    if (stored.seed == 0)
      stored.seed = decor_.seed;
    generated = ensureSeed(stored);
    decor_ = std::move(stored);
    copy = decor_;
  }
  ++decorRevision_;
  if (generated)
    sendDecor(copy);
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Controller::notify(Steinberg::Vst::IMessage* message) {
  Decor d;
  if (!decodeDecorMessage(message, d))
    return EditController::notify(message);
  {
    std::lock_guard<std::mutex> lk(decorMutex_);
    if (d.seed == 0)
      d.seed = decor_.seed;   // never clears the seed
    decor_ = std::move(d);
  }
  ++decorRevision_;
  return Steinberg::kResultOk;
}

Decor Controller::decor() const {
  std::lock_guard<std::mutex> lk(decorMutex_);
  return decor_;
}

void Controller::editDecor(Decor d) {
  sanitizeDecor(d);
  {
    std::lock_guard<std::mutex> lk(decorMutex_);
    if (d.seed == 0)
      d.seed = decor_.seed;
    ensureSeed(d);
    decor_ = d;
  }
  ++decorRevision_;
  sendDecor(d);
  // Not a parameter: tell the host the project changed so the new decor gets saved.
  if (Steinberg::FUnknownPtr<Steinberg::Vst::IComponentHandler2> handler2{componentHandler})
    handler2->setDirty(true);
}

void Controller::sendDecor(const Decor& d) {
  if (!getPeer())
    return;
  if (Steinberg::Vst::IMessage* msg = allocateMessage()) {
    encodeDecorMessage(msg, d);
    sendMessage(msg);
    msg->release();
  }
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
  double all[kNumParams];
  textNorms(*this, all);
  asciiToStr128(Ui::formatValue(static_cast<ParamID>(tag), v, all), string);
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Controller::getParamValueByString(Steinberg::Vst::ParamID tag,
    Steinberg::Vst::TChar* string, Steinberg::Vst::ParamValue& valueNormalized) {
  if (tag >= kNumParams || !string)
    return EditController::getParamValueByString(tag, string, valueNormalized);
  std::string typed;
  for (const Steinberg::Vst::TChar* c = string; *c; ++c) typed.push_back(*c < 128 ? static_cast<char>(*c) : '?');
  double all[kNumParams];
  textNorms(*this, all);
  return Ui::parseValue(static_cast<ParamID>(tag), typed, all, valueNormalized) ? Steinberg::kResultOk : Steinberg::kResultFalse;
}

} // namespace FairyTapes::HappyEnding
