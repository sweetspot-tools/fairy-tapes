/**
 * Fairy Tapes · Happy Ending — VST3 Edit Controller (parameters; no custom editor yet, the host shows
 * its generic UI).
 *
 * Registers the stored table (params.h, ParamID order, every control stepped), the two hidden session
 * parameters (LISTEN, SET: never stored, reset by setComponentState) and the hidden display
 * parameters. Every parameter is automatable except OVERSAMPLING, hidden session and display ones.
 * A change of OVERSAMPLING (a user edit, or a state load that differs: once, after the load) is sent to the
 * processor as an "Oversampling" message and asks the host to re-read the latency
 * (restartComponent(kLatencyChanged)).
 */

#pragma once

#include "cids.h"
#include "params.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

namespace FairyTapes::HappyEnding {

class Controller : public Steinberg::Vst::EditController {
public:
  Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
  Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) override;
  Steinberg::tresult PLUGIN_API setParamNormalized(Steinberg::Vst::ParamID tag,
                                                   Steinberg::Vst::ParamValue value) override;
  Steinberg::tresult PLUGIN_API getParamStringByValue(Steinberg::Vst::ParamID tag,
      Steinberg::Vst::ParamValue valueNormalized, Steinberg::Vst::String128 string) override;
  Steinberg::tresult PLUGIN_API getParamValueByString(Steinberg::Vst::ParamID tag,
      Steinberg::Vst::TChar* string, Steinberg::Vst::ParamValue& valueNormalized) override;

  // The MATCH button of the future editor (UI thread): reads the short-term LUFS the processor
  // publishes, moves LOUDNESS once to the value that centres the TARGET (matchLoudnessIndex), as one
  // host edit (beginEdit / performEdit / endEdit). No measurement yet or no change: does nothing.
  void doMatch();

private:
  void oversamplingChanged(int index);

  bool loadingState_ = false;
};

} // namespace FairyTapes::HappyEnding
