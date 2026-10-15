/**
 * Fairy Tapes · Deep Throat — VST3 Edit Controller implementation.
 */

#include "controller.h"
#include "state.h"
#include "pluginterfaces/base/ibstream.h"
#include "public.sdk/source/vst/vstparameters.h"
#include <cstdint>
#include <cstdio>

namespace FairyTapes::DeepThroat {

namespace {
using Steinberg::Vst::ParameterInfo;

void asciiToStr128(const char* src, Steinberg::Vst::String128 dst) {
  int i = 0;
  for (; i < 127 && src[i]; ++i)
    dst[i] = static_cast<Steinberg::Vst::TChar>(static_cast<unsigned char>(src[i]));
  dst[i] = 0;
}

// Stepped choice parameter: entries are given in order, norm = index / (count - 1).
void addChoice(Steinberg::Vst::ParameterContainer& params, const Steinberg::Vst::TChar* title,
               Steinberg::Vst::ParamID id, std::initializer_list<const Steinberg::Vst::TChar*> items,
               double defaultNorm) {
  auto* p = new Steinberg::Vst::StringListParameter(title, id);
  for (const auto* s : items)
    p->appendString(s);
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
  addChoice(parameters, STR16("Mode"), kMode, {STR16("Speak"), STR16("Sing"), STR16("Puke")}, d[kMode]);
  parameters.addParameter(STR16("Input"), STR16("dB"), 0, d[kInput], automate, kInput);
  parameters.addParameter(STR16("Rider Target"), STR16("dBFS"), 0, d[kRiderTarget], automate, kRiderTarget);
  parameters.addParameter(STR16("Rider Range"), STR16("%"), 0, d[kRiderRange], automate, kRiderRange);
  parameters.addParameter(STR16("Breath"), STR16("%"), 0, d[kBreathAmount], automate, kBreathAmount);
  parameters.addParameter(STR16("De-ess"), STR16("%"), 0, d[kDeEssAmount], automate, kDeEssAmount);
  parameters.addParameter(STR16("De-ess Listen"), nullptr, 1, d[kDeEssListen], automate, kDeEssListen);
  parameters.addParameter(STR16("Compressor"), STR16("%"), 0, d[kCompAmount], automate, kCompAmount);
  parameters.addParameter(STR16("Tone"), STR16("%"), 0, d[kTone], automate, kTone);
  parameters.addParameter(STR16("Output"), STR16("dB"), 0, d[kOutput], automate, kOutput);
  parameters.addParameter(STR16("Rider In"), nullptr, 1, d[kRiderIn], automate, kRiderIn);
  parameters.addParameter(STR16("Breath In"), nullptr, 1, d[kBreathIn], automate, kBreathIn);
  parameters.addParameter(STR16("EQ In"), nullptr, 1, d[kEqIn], automate, kEqIn);
  parameters.addParameter(STR16("De-ess In"), nullptr, 1, d[kDeEssIn], automate, kDeEssIn);
  parameters.addParameter(STR16("Comp In"), nullptr, 1, d[kCompIn], automate, kCompIn);
  parameters.addParameter(STR16("Tone In"), nullptr, 1, d[kToneIn], automate, kToneIn);
  parameters.addParameter(STR16("Breath Sensitivity"), STR16("%"), 0, d[kBreathSens], automate, kBreathSens);
  parameters.addParameter(STR16("Breath Listen"), nullptr, 1, d[kBreathListen], automate, kBreathListen);
  parameters.addParameter(STR16("De-ess Freq"), STR16("Hz"), 0, d[kDeEssFreq], automate, kDeEssFreq);
  parameters.addParameter(STR16("Comp Mix"), STR16("%"), 0, d[kCompMix], automate, kCompMix);
  addChoice(parameters, STR16("Comp Ratio"), kCompRatio,
            {STR16("AUTO"), STR16("2:1"), STR16("3:1"), STR16("4:1"), STR16("6:1"), STR16("8:1"),
             STR16("10:1"), STR16("20:1")},
            d[kCompRatio]);
  addChoice(parameters, STR16("Comp Dist"), kCompDist,
            {STR16("AUTO"), STR16("OFF"), STR16("Dist 2"), STR16("Dist 3")}, d[kCompDist]);
  addChoice(parameters, STR16("Comp SC HPF"), kCompHpf, {STR16("OFF"), STR16("80 Hz"), STR16("150 Hz")},
            d[kCompHpf]);
  parameters.addParameter(STR16("EQ Amount"), STR16("%"), 0, d[kEqAmount], automate, kEqAmount);

  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Controller::setComponentState(Steinberg::IBStream* state) {
  if (!state)
    return Steinberg::kResultFalse;
  uint8_t buf[kStateSize];
  Steinberg::int32 read = 0;
  if (state->read(buf, (Steinberg::int32)kStateSize, &read) != Steinberg::kResultOk)
    return Steinberg::kResultFalse;
  double values[kNumParams];
  if (!deserializeState(buf, (size_t)read, values))
    return Steinberg::kResultFalse;
  for (int i = 0; i < (int)kNumParams; ++i)
    setParamNormalized((Steinberg::Vst::ParamID)i, values[i]);
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Controller::getParamStringByValue(Steinberg::Vst::ParamID tag,
    Steinberg::Vst::ParamValue v, Steinberg::Vst::String128 string) {
  char buf[64];
  switch (tag) {
  case kBypass:
  case kDeEssListen:
  case kBreathListen:
  case kRiderIn:
  case kBreathIn:
  case kEqIn:
  case kDeEssIn:
  case kCompIn:
  case kToneIn:
    asciiToStr128(v >= 0.5 ? "On" : "Off", string);
    return Steinberg::kResultOk;
  case kInput:
    std::snprintf(buf, sizeof(buf), "%+.1f dB", inputDbFromNorm(v));
    break;
  case kOutput:
    std::snprintf(buf, sizeof(buf), "%+.1f dB", outputDbFromNorm(v));
    break;
  case kRiderTarget:
    std::snprintf(buf, sizeof(buf), "%.1f dBFS", riderTargetDbFromNorm(v));
    break;
  case kDeEssFreq:
    std::snprintf(buf, sizeof(buf), "%.0f Hz", deEssFreqHzFromNorm(v));
    break;
  case kRiderRange:
  case kBreathAmount:
  case kDeEssAmount:
  case kCompAmount:
  case kTone:
  case kBreathSens:
  case kCompMix:
  case kEqAmount:
    std::snprintf(buf, sizeof(buf), "%.0f %%", v * 100.0);
    break;
  default:  // choice lists: the StringListParameter formats itself
    return EditController::getParamStringByValue(tag, v, string);
  }
  asciiToStr128(buf, string);
  return Steinberg::kResultOk;
}

} // namespace FairyTapes::DeepThroat
