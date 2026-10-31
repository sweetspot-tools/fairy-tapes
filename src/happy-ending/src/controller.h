/**
 * Fairy Tapes · Happy Ending — VST3 Edit Controller (parameters and the editor, ui/editor.h).
 *
 * createView gives the editor (Ui::Editor, 1280 x 890 times the host factor times the SIZE of HappyEnding.prefs) for
 * ViewType::kEditor. Built without HAPPY_ENDING_WITH_EDITOR (the controller tests, no VSTGUI) it gives none.
 *
 * Keeps a copy of the instance decor (wear seed, screw angles, tape texts) for the editor; the processor saves
 * it. The two copies meet through "Decor" messages (messages.h), never on the audio thread.
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
#include "state.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include <atomic>
#include <cstdint>
#include <mutex>

namespace FairyTapes::HappyEnding {

class Controller : public Steinberg::Vst::EditController {
public:
  Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
  Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;
  Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) override;
  Steinberg::tresult PLUGIN_API setParamNormalized(Steinberg::Vst::ParamID tag,
                                                   Steinberg::Vst::ParamValue value) override;
  Steinberg::tresult PLUGIN_API getParamStringByValue(Steinberg::Vst::ParamID tag,
      Steinberg::Vst::ParamValue valueNormalized, Steinberg::Vst::String128 string) override;
  Steinberg::tresult PLUGIN_API getParamValueByString(Steinberg::Vst::ParamID tag,
      Steinberg::Vst::TChar* string, Steinberg::Vst::ParamValue& valueNormalized) override;

  // The MATCH button of the editor (UI thread): reads the short-term LUFS the processor
  // publishes, moves LOUDNESS once to the value that centres the TARGET (matchLoudnessIndex), as one
  // host edit (beginEdit / performEdit / endEdit). No measurement yet or no change: does nothing.
  void doMatch();

  // "Decor" from the processor (after setState and on connect); any other message goes to the base class.
  Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;

  // Editor side (UI thread). decor() is a copy (seed 0 until the processor or a state has provided one);
  // decorRevision() changes whenever the decor does, so the editor can poll it.
  Decor decor() const;
  std::uint32_t decorRevision() const { return decorRevision_.load(); }
  // An editor edit (screw angle, tape text, re-roll): sanitized, kept, sent to the processor, project marked dirty.
  // Seed 0 keeps the current seed (and generates one if there is none yet).
  void editDecor(Decor d);

private:
  void oversamplingChanged(int index);
  void sendDecor(const Decor& d);

  bool loadingState_ = false;

  mutable std::mutex decorMutex_;   // notify() may arrive on another thread
  Decor decor_;                     // seed 0 until the processor or a state provides one
  std::atomic<std::uint32_t> decorRevision_{0};
};

} // namespace FairyTapes::HappyEnding
