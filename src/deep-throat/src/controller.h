/**
 * Fairy Tapes · Deep Throat — VST3 Edit Controller (parameters; no editor view yet).
 */

#pragma once

#include "cids.h"
#include "params.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

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
};

} // namespace FairyTapes::DeepThroat
