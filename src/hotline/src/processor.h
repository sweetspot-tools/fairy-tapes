/**
 * Fairy Tapes · Hotline — VST3 Audio Processor.
 * Buses: main stereo in, main stereo out. The signal chain lives in Engine (engine.h), the state layout in state.h.
 * Zero latency; the tail is the Room tail. Meters and clip flags go to the editor through hidden output parameters.
 */

#pragma once

#include "cids.h"
#include "display.h"
#include "engine.h"
#include "params.h"
#include "public.sdk/source/vst/vstaudioeffect.h"

namespace FairyTapes::Hotline {

class Processor : public Steinberg::Vst::AudioEffect {
public:
  Processor();
  ~Processor() override = default;

  Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
  Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
  Steinberg::tresult PLUGIN_API setBusArrangements(
    Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
    Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) override;
  Steinberg::uint32 PLUGIN_API getTailSamples() override;
  Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
  Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
  Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;

private:
  void updateParamsFromChanges(Steinberg::Vst::IParameterChanges* changes);
  void publishDisplay(Steinberg::Vst::ProcessData& data);

  double param_value_[kNumParams];
  Engine engine_;
  DisplayRateLimiter display_limiter_;
  DisplayAccumulator display_acc_;
  bool prepared_ = false;
  bool clear_clips_requested_ = false;
  bool clear_clips_ack_ = false;
};

} // namespace FairyTapes::Hotline
