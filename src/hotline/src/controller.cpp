/**
 * Fairy Tapes · Hotline — VST3 Edit Controller implementation.
 */

#include "controller.h"
#include "devices.h"
#include "engine.h"
#include "format.h"
#include "messages.h"
#include "state.h"
#include "state_stream.h"
#include "ui/editor.h"
#include "pluginterfaces/base/ibstream.h"
#include "public.sdk/source/vst/vstparameters.h"
#include <cstdint>
#include <cstring>
#include <vector>

namespace FairyTapes::Hotline {

namespace {
using Steinberg::Vst::ParameterInfo;

void asciiToStr128(const char* src, Steinberg::Vst::String128 dst) {
  int i = 0;
  for (; i < 127 && src[i]; ++i)
    dst[i] = static_cast<Steinberg::Vst::TChar>(static_cast<unsigned char>(src[i]));
  dst[i] = 0;
}
} // namespace

Steinberg::tresult PLUGIN_API Controller::initialize(Steinberg::FUnknown* context) {
  auto result = EditController::initialize(context);
  if (result != Steinberg::kResultOk)
    return result;

  double d[kNumParams];
  loadDefaults(d);
  const auto automate = ParameterInfo::kCanAutomate;
  // Registered in ParamID order so that index == id.
  parameters.addParameter(STR16("Bypass"), nullptr, 1, d[kBypass], automate | ParameterInfo::kIsBypass, kBypass);

  auto* device = new Steinberg::Vst::StringListParameter(STR16("Device"), kDevice);
  for (int i = 0; i < kNumDevices; ++i) {
    Steinberg::Vst::String128 name;
    asciiToStr128(kDevices[i].name, name);
    device->appendString(name);
  }
  device->getInfo().defaultNormalizedValue = d[kDevice];
  device->setNormalized(d[kDevice]);
  parameters.addParameter(device);

  parameters.addParameter(STR16("Morph"), STR16("%"), 0, d[kMorph], automate, kMorph);
  parameters.addParameter(STR16("Gain"), STR16("dB"), 0, d[kGain], automate, kGain);
  parameters.addParameter(STR16("Mono"), nullptr, 1, d[kMono], automate, kMono);
  parameters.addParameter(STR16("Drive"), STR16("%"), 0, d[kDrive], automate, kDrive);
  parameters.addParameter(STR16("Tone"), nullptr, 0, d[kTone], automate, kTone);
  parameters.addParameter(STR16("Noise"), STR16("%"), 0, d[kNoise], automate, kNoise);
  parameters.addParameter(STR16("Room"), STR16("%"), 0, d[kRoom], automate, kRoom);
  parameters.addParameter(STR16("Mix"), STR16("%"), 0, d[kMix], automate, kMix);
  parameters.addParameter(STR16("Character In"), nullptr, 1, d[kCharacterIn], automate, kCharacterIn);

  const auto display = ParameterInfo::kIsReadOnly | ParameterInfo::kIsHidden;
  parameters.addParameter(STR16("Meter In L"), nullptr, 0, 0.0, display, kMeterInL);
  parameters.addParameter(STR16("Meter In R"), nullptr, 0, 0.0, display, kMeterInR);
  parameters.addParameter(STR16("Meter Out L"), nullptr, 0, 0.0, display, kMeterOutL);
  parameters.addParameter(STR16("Meter Out R"), nullptr, 0, 0.0, display, kMeterOutR);
  parameters.addParameter(STR16("Clip In"), nullptr, 1, 0.0, display, kClipIn);
  parameters.addParameter(STR16("Clip Drive"), nullptr, 1, 0.0, display, kClipDrive);
  parameters.addParameter(STR16("Clip Out"), nullptr, 1, 0.0, display, kClipOut);
  // Writable (no kIsReadOnly): the editor sets it to 1 to clear the clip LEDs, the processor resets it to 0.
  parameters.addParameter(STR16("Clip Clear"), nullptr, 1, 0.0, ParameterInfo::kIsHidden, kClipClear);

  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Controller::setComponentState(Steinberg::IBStream* state) {
  std::vector<uint8_t> bytes;
  if (!readStateBytes(state, bytes))
    return Steinberg::kResultFalse;
  PluginState st;
  defaultState(st);
  if (!deserializeState(bytes.data(), bytes.size(), st))
    return Steinberg::kResultFalse;
  for (int i = 0; i < static_cast<int>(kNumParams); ++i)
    setParamNormalized(static_cast<Steinberg::Vst::ParamID>(i), st.params[i]);
  bool generated = false;
  Decor copy;
  {
    std::lock_guard<std::mutex> lk(decor_mutex_);
    // No seed in the state (v1 or 0): keep the one already known (usually the processor's, sent from its setState);
    // with none, generate one and tell the processor.
    if (st.decor.seed == 0) st.decor.seed = decor_.seed;
    generated = ensureSeed(st.decor);
    decor_ = std::move(st.decor);
    copy = decor_;
  }
  ++decor_revision_;
  if (generated) sendDecor(copy);
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Controller::notify(Steinberg::Vst::IMessage* message) {
  Decor d;
  if (!decodeDecorMessage(message, d))
    return EditController::notify(message);
  {
    std::lock_guard<std::mutex> lk(decor_mutex_);
    if (d.seed == 0) d.seed = decor_.seed;
    decor_ = std::move(d);
  }
  ++decor_revision_;
  return Steinberg::kResultOk;
}

Decor Controller::decor() {
  bool generated = false;
  Decor copy;
  {
    std::lock_guard<std::mutex> lk(decor_mutex_);
    generated = ensureSeed(decor_);
    copy = decor_;
  }
  if (generated) {
    ++decor_revision_;
    sendDecor(copy);
  }
  return copy;
}

void Controller::editDecor(Decor d) {
  sanitizeDecor(d);
  {
    std::lock_guard<std::mutex> lk(decor_mutex_);
    if (d.seed == 0) d.seed = decor_.seed;
    ensureSeed(d);
    decor_ = d;
  }
  ++decor_revision_;
  sendDecor(d);
  // Not a parameter: tell the host the project changed so the new decor gets saved.
  if (Steinberg::FUnknownPtr<Steinberg::Vst::IComponentHandler2> handler2{componentHandler})
    handler2->setDirty(true);
}

void Controller::sendDecor(const Decor& d) {
  if (!getPeer())
    return;
  Steinberg::Vst::IMessage* msg = allocateMessage();
  if (!msg)
    return;
  encodeDecorMessage(msg, d);
  sendMessage(msg);
  msg->release();
}

Steinberg::tresult PLUGIN_API Controller::getParamStringByValue(Steinberg::Vst::ParamID tag,
    Steinberg::Vst::ParamValue v, Steinberg::Vst::String128 string) {
  char buf[64];
  if (!formatParam(tag, v, buf, sizeof buf))
    return EditController::getParamStringByValue(tag, v, string);
  asciiToStr128(buf, string);
  return Steinberg::kResultOk;
}

Steinberg::IPlugView* PLUGIN_API Controller::createView(Steinberg::FIDString name) {
  if (name && std::strcmp(name, Steinberg::Vst::ViewType::kEditor) == 0)
    return new Ui::Editor(this);
  return nullptr;
}

} // namespace FairyTapes::Hotline
