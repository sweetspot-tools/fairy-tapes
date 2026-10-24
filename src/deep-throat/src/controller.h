/**
 * Fairy Tapes · Deep Throat — VST3 Edit Controller (parameters; the editor (ui/editor.h) is created by createView).
 * Keeps a copy of the instance decor (wear seed, screw angles, tape texts) for the editor; the processor saves it.
 * The two copies meet through "Decor" messages (messages.h), never on the audio thread.
 */

#pragma once

#include "cids.h"
#include "params.h"
#include "state.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include <atomic>
#include <mutex>

namespace FairyTapes::DeepThroat {

class Controller : public Steinberg::Vst::EditController {
public:
  Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
  Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) override;
  Steinberg::tresult PLUGIN_API getParamStringByValue(Steinberg::Vst::ParamID tag,
      Steinberg::Vst::ParamValue valueNormalized, Steinberg::Vst::String128 string) override;
  // Parses typed values ("+6 dB", "6500 Hz", "50 %", "On", choice names); units are optional.
  Steinberg::tresult PLUGIN_API getParamValueByString(Steinberg::Vst::ParamID tag,
      Steinberg::Vst::TChar* string, Steinberg::Vst::ParamValue& valueNormalized) override;
  // "Decor" from the processor (after setState and on connect).
  Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;
  // The editor for ViewType::kEditor (owned by the caller), nullptr for any other view.
  Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;

  // Editor side (UI thread). decor() first makes sure a seed exists (generated and sent to the processor if the
  // controller still has none); decorRevision() changes whenever the decor does, so the editor can poll it.
  Decor decor();
  uint32_t decorRevision() const { return decor_revision_.load(); }
  // An editor edit (screw angle, tape text, re-roll): sanitized, kept, sent to the processor, project marked dirty.
  // Seed 0 keeps the current seed.
  void editDecor(Decor d);

private:
  void sendDecor(const Decor& d);

  mutable std::mutex decor_mutex_;   // notify() may arrive on another thread
  Decor decor_;                      // seed 0 until the processor or a state provides one
  std::atomic<uint32_t> decor_revision_{0};
};

} // namespace FairyTapes::DeepThroat
