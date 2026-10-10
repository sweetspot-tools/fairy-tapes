/**
 * Fairy Tapes · Pillow Talk — VST3 Edit Controller (parameters and the VSTGUI editor).
 */

#pragma once

#include "cids.h"
#include "params.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

namespace FairyTapes::PillowTalk {

class Controller : public Steinberg::Vst::EditController {
public:
  Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
  Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) override;
  Steinberg::tresult PLUGIN_API getParamStringByValue(Steinberg::Vst::ParamID tag,
      Steinberg::Vst::ParamValue valueNormalized, Steinberg::Vst::String128 string) override;
  // Returns the editor for ViewType::kEditor (the host owns the returned reference), else nullptr.
  Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;
};

} // namespace FairyTapes::PillowTalk
