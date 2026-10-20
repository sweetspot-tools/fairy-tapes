/**
 * Fairy Tapes · Pillow Talk — VST3 Edit Controller implementation.
 * The editor (ui/editor.h) is created on demand by createView.
 */

#include "controller.h"
#include "engine.h"
#include "messages.h"
#include "state.h"
#include "state_stream.h"
#include "ui/editor.h"
#include "pluginterfaces/base/ibstream.h"
#include "public.sdk/source/vst/vstparameters.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace FairyTapes::PillowTalk {

namespace {
using Steinberg::Vst::ParameterInfo;

void asciiToStr128(const char* src, Steinberg::Vst::String128 dst) {
  int i = 0;
  for (; i < 127 && src[i]; ++i)
    dst[i] = static_cast<Steinberg::Vst::TChar>(static_cast<unsigned char>(src[i]));
  dst[i] = 0;
}

void addNoteList(Steinberg::Vst::ParameterContainer& params, const Steinberg::Vst::TChar* title,
                 Steinberg::Vst::ParamID id, double defaultNorm) {
  auto* p = new Steinberg::Vst::StringListParameter(title, id);
  p->appendString(STR16("1/16"));
  p->appendString(STR16("1/8"));
  p->appendString(STR16("1/4"));
  p->appendString(STR16("1/2"));
  p->getInfo().defaultNormalizedValue = defaultNorm;
  p->setNormalized(defaultNorm);
  params.addParameter(p);
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
  parameters.addParameter(STR16("Bypass"), nullptr, 1, d[kBypass],
                          automate | ParameterInfo::kIsBypass, kBypass);
  parameters.addParameter(STR16("Time Sync"), nullptr, 1, d[kTimeSync], automate, kTimeSync);
  parameters.addParameter(STR16("Time"), STR16("ms"), 0, d[kTimeMs], automate, kTimeMs);
  addNoteList(parameters, STR16("Time Note"), kTimeNote, d[kTimeNote]);
  parameters.addParameter(STR16("Feedback"), STR16("%"), 0, d[kFeedback], automate, kFeedback);
  parameters.addParameter(STR16("Tone"), STR16("Hz"), 0, d[kTone], automate, kTone);
  parameters.addParameter(STR16("Saturation"), STR16("%"), 0, d[kSaturation], automate, kSaturation);
  parameters.addParameter(STR16("Wow/Flutter"), STR16("%"), 0, d[kWowFlutter], automate, kWowFlutter);
  parameters.addParameter(STR16("Mix"), STR16("%"), 0, d[kMix], automate, kMix);
  parameters.addParameter(STR16("Threshold"), STR16("dB"), 0, d[kThreshold], automate, kThreshold);
  parameters.addParameter(STR16("Ducking Depth"), STR16("dB"), 0, d[kDuckDepth], automate, kDuckDepth);
  parameters.addParameter(STR16("Hold Sync"), nullptr, 1, d[kHoldSync], automate, kHoldSync);
  parameters.addParameter(STR16("Hold"), STR16("ms"), 0, d[kHoldMs], automate, kHoldMs);
  addNoteList(parameters, STR16("Hold Note"), kHoldNote, d[kHoldNote]);
  parameters.addParameter(STR16("Release"), STR16("ms"), 0, d[kRelease], automate, kRelease);
  parameters.addParameter(STR16("Sidechain"), nullptr, 1, d[kSidechain], automate, kSidechain);

  const auto display = ParameterInfo::kIsReadOnly | ParameterInfo::kIsHidden;
  parameters.addParameter(STR16("Display Voice"), nullptr, 0, 0.0, display, kDispVoice);
  parameters.addParameter(STR16("Display Send"), nullptr, 0, 1.0, display, kDispSend);
  parameters.addParameter(STR16("Display Echo"), nullptr, 0, 0.0, display, kDispEcho);
  parameters.addParameter(STR16("Display In L"), nullptr, 0, 0.0, display, kDispInL);
  parameters.addParameter(STR16("Display In R"), nullptr, 0, 0.0, display, kDispInR);
  parameters.addParameter(STR16("Display Out L"), nullptr, 0, 0.0, display, kDispOutL);
  parameters.addParameter(STR16("Display Out R"), nullptr, 0, 0.0, display, kDispOutR);
  parameters.addParameter(STR16("Display Clip In"), nullptr, 1, 0.0, display, kDispClipIn);
  parameters.addParameter(STR16("Display Clip Out"), nullptr, 1, 0.0, display, kDispClipOut);
  // Writable (no kIsReadOnly): the editor sets it to 1 to clear the clip LEDs, the processor resets it to 0.
  parameters.addParameter(STR16("Clear Clips"), nullptr, 1, 0.0, ParameterInfo::kIsHidden, kClearClips);

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
  for (int i = 0; i < (int)kNumParams; ++i)
    setParamNormalized((Steinberg::Vst::ParamID)i, st.params[i]);
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
  switch (tag) {
  case kBypass:
  case kTimeSync:
  case kHoldSync:
  case kSidechain:
    asciiToStr128(v >= 0.5 ? "On" : "Off", string);
    return Steinberg::kResultOk;
  case kTimeMs:
    std::snprintf(buf, sizeof(buf), "%.0f ms", timeMsFromNorm(v));
    break;
  case kHoldMs:
    std::snprintf(buf, sizeof(buf), "%.0f ms", holdMsFromNorm(v));
    break;
  case kRelease:
    std::snprintf(buf, sizeof(buf), "%.0f ms", releaseMsFromNorm(v));
    break;
  case kFeedback:
    std::snprintf(buf, sizeof(buf), "%.0f %%", feedbackFromNorm(v) * 100.0);
    break;
  case kTone:
    std::snprintf(buf, sizeof(buf), "%.0f Hz", toneHzFromNorm(v));
    break;
  case kSaturation:
  case kWowFlutter:
  case kMix:
    std::snprintf(buf, sizeof(buf), "%.0f %%", v * 100.0);
    break;
  case kThreshold:
    std::snprintf(buf, sizeof(buf), "%.1f dB", thresholdDbFromNorm(v));
    break;
  case kDuckDepth:
    if (duckDepthDbFromNorm(v) >= kDuckMuteDb)
      std::snprintf(buf, sizeof(buf), "Mute");
    else
      std::snprintf(buf, sizeof(buf), "%.1f dB", duckDepthDbFromNorm(v));
    break;
  default:  // note lists: the StringListParameter formats itself
    return EditController::getParamStringByValue(tag, v, string);
  }
  asciiToStr128(buf, string);
  return Steinberg::kResultOk;
}

Steinberg::IPlugView* PLUGIN_API Controller::createView(Steinberg::FIDString name) {
  if (name && std::strcmp(name, Steinberg::Vst::ViewType::kEditor) == 0)
    return new Ui::Editor(this);
  return nullptr;
}

} // namespace FairyTapes::PillowTalk
