/**
 * Fairy Tapes · Deep Throat — VST3 Audio Processor.
 * Supported layouts: stereo in -> stereo out and mono in -> mono out (both tested; mixed layouts
 * are rejected). The whole chain runs in Engine (engine.h); the reported latency is the engine's
 * look-ahead delay (constant for a given sample rate and LATENCY setting, the same for mono and stereo).
 * LATENCY (spec 9b: MIX / LIVE) is part of the instance decor (state v3, "Decor" messages). setupProcessing() and
 * setActive(true) prepare the engine for it, with processing stopped; a change that arrives while the plug-in runs
 * (setState, the controller's decor message) is only kept: the controller asks the host for
 * restartComponent(kLatencyChanged), and the next activation applies it. process() never touches it.
 */

#pragma once

#include "cids.h"
#include "engine.h"
#include "params.h"
#include "state.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include <atomic>
#include <mutex>

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
  Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint* other) override;
  Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;

private:
  void sendDecor();
  void updateParamsFromChanges(Steinberg::Vst::IParameterChanges* changes);
  void publishDisplay(Steinberg::Vst::ProcessData& data);

  double param_value_[kNumParams];
  Engine engine_;
  DisplayRateLimiter display_limiter_;
  DisplayAccumulator display_acc_;
  LevelAccumulator level_acc_;
  bool clear_clips_requested_ = false;
  bool clear_clips_ack_ = false;

  // The instance decor (state v2) is saved here and exchanged with the controller through "Decor" messages
  // (messages.h); process() never touches it.
  std::mutex decor_mutex_;   // setState/getState/notify may come from different non-audio threads
  Decor decor_;              // seed generated in the constructor or when a state has none
  // decor_.behaviour.latency, readable without the lock (setupProcessing / setActive).
  std::atomic<LatencyMode> latency_{LatencyMode::Mix};
};

} // namespace FairyTapes::DeepThroat
