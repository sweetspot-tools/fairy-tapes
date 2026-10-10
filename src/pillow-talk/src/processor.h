/**
 * Fairy Tapes · Pillow Talk — VST3 Audio Processor.
 * Buses: main stereo in, optional stereo sidechain (aux, inactive by default), stereo out.
 * The signal chain lives in Engine (engine.h); state helpers in state.h.
 */

#pragma once

#include "cids.h"
#include "engine.h"
#include "params.h"
#include "state.h"
#include "public.sdk/source/vst/vstaudioeffect.h"

namespace FairyTapes::PillowTalk {

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
  void publishDisplay(Steinberg::Vst::ProcessData& data);
  void updateParamsFromChanges(Steinberg::Vst::IParameterChanges* changes);

  double param_value_[kNumParams];
  Engine engine_;
  DisplayRateLimiter display_limiter_;
  DisplayAccumulator display_acc_;
  LevelAccumulator level_acc_;
  bool clear_clips_requested_ = false;
  bool clear_clips_ack_ = false;
};

} // namespace FairyTapes::PillowTalk
