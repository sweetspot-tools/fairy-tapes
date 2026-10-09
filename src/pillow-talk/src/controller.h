/**
 * Fairy Tapes · Pillow Talk — VST3 Edit Controller (parameters only, no custom UI).
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
};

} // namespace FairyTapes::PillowTalk
