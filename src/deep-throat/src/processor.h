/**
 * Fairy Tapes · Deep Throat — VST3 Audio Processor.
 * Stereo in, stereo out. For now only Input/Output gain is applied (DSP blocks come later).
 */

#pragma once

#include "cids.h"
#include "params.h"
#include "state.h"
#include "public.sdk/source/vst/vstaudioeffect.h"

namespace FairyTapes::DeepThroat {

class Processor : public Steinberg::Vst::AudioEffect {
public:
  Processor();
  ~Processor() override = default;

  Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
  Steinberg::tresult PLUGIN_API setBusArrangements(
    Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
    Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) override;
  Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
  Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
  Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;

private:
  void updateParamsFromChanges(Steinberg::Vst::IParameterChanges* changes);

  double param_value_[kNumParams];
};

} // namespace FairyTapes::DeepThroat
