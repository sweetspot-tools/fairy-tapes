/**
 * Fairy Tapes · Hotline — VST3 Edit Controller (parameters; the editor is ui/editor.h).
 */

#pragma once

#include "cids.h"
#include "params.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

namespace FairyTapes::Hotline {

class Controller : public Steinberg::Vst::EditController {
public:
  Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
  Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) override;
  Steinberg::tresult PLUGIN_API getParamStringByValue(Steinberg::Vst::ParamID tag,
      Steinberg::Vst::ParamValue valueNormalized, Steinberg::Vst::String128 string) override;
  // The editor (ui/editor.h) for ViewType::kEditor, nullptr for any other name.
  Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;
};

} // namespace FairyTapes::Hotline
