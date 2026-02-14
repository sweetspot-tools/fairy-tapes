/**
 * Fairy Tapes · The Stripper — VST3 Edit Controller (parameters only, no UI).
 */

#pragma once

#include "cids.h"
#include "params.h"
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

namespace FairyTapes::TheStripper {

// IEditController already inherits from IPluginBase; avoid duplicate base.
class Controller : public Steinberg::Vst::IEditController {
public:
  Controller();
  ~Controller() { FUNKNOWN_DTOR }

  // IPluginBase
  Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
  Steinberg::tresult PLUGIN_API terminate() override;

  // IEditController
  Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) override;
  Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
  Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;
  Steinberg::int32 PLUGIN_API getParameterCount() override;
  Steinberg::tresult PLUGIN_API getParameterInfo(Steinberg::int32 index,
                                                 Steinberg::Vst::ParameterInfo& info) override;
  Steinberg::tresult PLUGIN_API getParamStringByValue(Steinberg::Vst::ParamID tag,
      Steinberg::Vst::ParamValue valueNormalized, Steinberg::Vst::String128 string) override;
  Steinberg::tresult PLUGIN_API getParamValueByString(Steinberg::Vst::ParamID tag,
      Steinberg::Vst::TChar* string, Steinberg::Vst::ParamValue& valueNormalized) override;
  Steinberg::Vst::ParamValue PLUGIN_API normalizedParamToPlain(Steinberg::Vst::ParamID tag,
      Steinberg::Vst::ParamValue valueNormalized) override;
  Steinberg::Vst::ParamValue PLUGIN_API plainParamToNormalized(Steinberg::Vst::ParamID tag,
      Steinberg::Vst::ParamValue plainValue) override;
  Steinberg::Vst::ParamValue PLUGIN_API getParamNormalized(Steinberg::Vst::ParamID tag) override;
  Steinberg::tresult PLUGIN_API setParamNormalized(Steinberg::Vst::ParamID tag,
      Steinberg::Vst::ParamValue value) override;
  Steinberg::tresult PLUGIN_API setComponentHandler(Steinberg::Vst::IComponentHandler* handler) override;
  Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;

  DECLARE_FUNKNOWN_METHODS

private:
  Steinberg::Vst::ParamValue param_value_[kNumParams];
  Steinberg::IPtr<Steinberg::Vst::IComponentHandler> component_handler_;
};

} // namespace FairyTapes::TheStripper
