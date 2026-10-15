/**
 * Fairy Tapes · Deep Throat — VST3 Audio Processor.
 * Supported layouts: stereo in -> stereo out and mono in -> mono out (both tested; mixed layouts
 * are rejected). The whole chain runs in Engine (engine.h); the reported latency is the engine's
 * look-ahead delay (constant for a given sample rate, the same for mono and stereo).
 */

#pragma once

#include "cids.h"
#include "engine.h"
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
  Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& newSetup) override;
  Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
  Steinberg::uint32 PLUGIN_API getLatencySamples() override;
  Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
  Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
  Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;

private:
  void updateParamsFromChanges(Steinberg::Vst::IParameterChanges* changes);

  double param_value_[kNumParams];
  Engine engine_;
};

} // namespace FairyTapes::DeepThroat
