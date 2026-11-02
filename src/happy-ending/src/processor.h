/**
 * Fairy Tapes · Happy Ending — VST3 Audio Processor.
 * Buses: one main input and one main output, both stereo or both mono (no sidechain). The chain
 * lives in Engine (engine.h); state in state.h; the hidden display parameters in display.h.
 *
 * Threads and lifetime:
 *  - setupProcessing() and setActive(true) (re)prepare the engine for the current rate, block size and
 *    OVERSAMPLING; both run with processing stopped. getLatencySamples() is Engine::latencySamples().
 *  - A change of OVERSAMPLING while the plug-in runs never touches the engine on the audio thread: the
 *    value is kept, the zones are rebuilt at the next activation. The controller asks the host for that
 *    with restartComponent(kLatencyChanged) and tells this side with an "Oversampling" message. A setState
 *    with another OVERSAMPLING only stores the value (no rebuild while running): the controller's
 *    setComponentState asks for the restart, and an inactive processor picks it up at the next setActive.
 *  - process() allocates nothing: parameters, the display writes and the mono scratch buffer (sized in
 *    setupProcessing) are all preallocated.
 *  - LISTEN and SET live only in this object (atomics), never in the state; setState puts them back
 *    to off.
 *  - The instance decor (state v2: wear seed, screw angles, tape texts) is saved here, under decor_mutex_
 *    (setState / getState / notify may come from different threads, never the audio thread), and exchanged with
 *    the controller through "Decor" messages: sent after setState and on connect, adopted when the controller sends
 *    one. A fresh instance, a v1 state and a state without a seed all get a seed generated outside process().
 *  - MONO: the chain is stereo, so a mono bus is processed as a dual-mono pair (the channel plus a
 *    scratch copy); the LUFS it reads are 3.01 LU above the BS.1770 value of the mono channel and are
 *    published corrected by kDualMonoLufsOffsetDb.
 */

#pragma once

#include "cids.h"
#include "display.h"
#include "engine.h"
#include "params.h"
#include "state.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include <array>
#include <atomic>
#include <mutex>
#include <vector>

namespace FairyTapes::HappyEnding {

class Processor : public Steinberg::Vst::AudioEffect {
public:
  Processor();
  ~Processor() override = default;

  Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
  Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
  Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) override;
  Steinberg::tresult PLUGIN_API setBusArrangements(
    Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
    Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) override;
  Steinberg::uint32 PLUGIN_API getLatencySamples() override;
  Steinberg::uint32 PLUGIN_API getTailSamples() override;
  Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
  Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
  Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;
  Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;
  Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint* other) override;

private:
  void prepareEngine();
  int oversamplingFactor() const;
  void readParameterChanges(Steinberg::Vst::IParameterChanges* changes);
  void publishDisplay(Steinberg::Vst::ProcessData& data, bool dualMono);
  void sendDecor();

  std::array<std::atomic<double>, kNumParams> param_;  // normalized, ParamID order
  std::atomic<int> listen_{-1};                         // -1 off, 0..2 the bells (not stored)
  std::atomic<bool> setActive_{false};                  // SET (not stored)

  std::mutex decor_mutex_;   // setState / getState / notify (never process())
  Decor decor_;              // seed generated in the constructor or when a state has none

  Engine engine_;
  DisplayRateLimiter display_limiter_;
  MeterHold hold_;
  std::vector<float> scratch_;      // R channel of a mono bus, maxSamplesPerBlock long
  bool clear_clips_requested_ = false, clear_clips_ack_ = false;
  bool reset_integrated_requested_ = false, reset_integrated_ack_ = false;
};

} // namespace FairyTapes::HappyEnding
