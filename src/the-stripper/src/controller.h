/**
 * Fairy Tapes · The Stripper — VST3 Edit Controller (parameters only, no UI).
 */

#pragma once

#include "cids.h"
#include <string>
#include "params.h"
#include "pluginterfaces/base/funknown.h"
#include "messages.h"
#include "state.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include <mutex>
#include <vector>

namespace FairyTapes::TheStripper {

namespace Ui {
struct EditorPreview;
}

// IEditController already inherits from IPluginBase; avoid duplicate base.
class Controller : public Steinberg::Vst::IEditController, public Steinberg::Vst::IConnectionPoint {
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

  /** Latest value (0..1) of a meter parameter (kMeterInL ... kClearClips). Read by the editor from its timer. */
  Steinberg::Vst::ParamValue meterValue(Steinberg::Vst::ParamID id) const {
    return (id >= kMeterBase && id < kNumMeterIDs) ? meter_value_[id - kMeterBase] : 0.0;
  }
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

  // IConnectionPoint: model loading messages (see messages.h)
  Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint* other) override;
  Steinberg::tresult PLUGIN_API disconnect(Steinberg::Vst::IConnectionPoint* other) override;
  Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;

  /** User gestures from the editor: the value is stored and reported to the host (setParamNormalized is host -> controller only). */
  void beginEdit(Steinberg::Vst::ParamID tag);
  void performEdit(Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value);
  void endEdit(Steinberg::Vst::ParamID tag);

  /** Editor-only data kept with the plug-in state (controller side): the seed of the wear and the writing on the tape. */
  std::uint32_t wearSeed() const;
  TapeText tapeText(int index) const;                       // `set` false when never written (or out of range)
  void setTapeText(int index, const std::string& text);     // UTF-8, cut to kMaxTapeTextBytes
  void rerollWear();                                        // RE-ROLL: a new seed (new wear, dirt, tape) and the writing erased

  /** Ask the processor to load a .nam model (UTF-8 path); empty = no model. The editor shows the outcome via the getters below. */
  void requestModelLoad(const std::string& path);
  std::string modelPath() const;
  std::string modelName() const;    // file name of the loaded model, empty when none
  std::string modelError() const;   // why the last load failed, empty otherwise
  int modelState() const;           // 0 idle, 1 loading, 2 loaded, 3 failed

  DECLARE_FUNKNOWN_METHODS

private:
  Steinberg::tresult applyState(Steinberg::IBStream* state, bool withEditorData);
  friend struct Ui::EditorPreview;   // tools/render_editor.cpp sets the model status without a processor
  Steinberg::Vst::ParamValue param_value_[kNumParams];
  Steinberg::Vst::ParamValue meter_value_[kNumMeters] = {};   // written by the processor, read by the editor
  std::uint32_t wearSeed_ = 0;                 // editor data (v10 state); 0 until initialize() or setState() gives one
  std::vector<TapeText> tapeTexts_;
  mutable std::mutex uiMutex_;
  std::string modelPath_;
  mutable std::mutex modelMutex_;   // notify() may arrive on a worker thread
  ModelStatus modelStatus_;
  Steinberg::IPtr<Steinberg::Vst::IConnectionPoint> peer_;
  Steinberg::IPtr<Steinberg::Vst::IHostApplication> host_;
  Steinberg::IPtr<Steinberg::Vst::IComponentHandler> component_handler_;
};

} // namespace FairyTapes::TheStripper
