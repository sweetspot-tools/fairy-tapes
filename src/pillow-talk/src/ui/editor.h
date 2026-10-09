// Pillow Talk GUI: the plugin editor (480 x 560, views built in code, no .uidesc).
// Every persistent parameter is bound to one control. User gestures go to the controller as
// beginEdit / performEdit / endEdit; host-side changes (automation, presets, undo) arrive as
// Parameter dependents and are applied on the UI timer.
#pragma once

#include "params.h"

#include "fairy_ui/skin.h"

#include "public.sdk/source/vst/vstguieditor.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "vstgui/lib/controls/icontrollistener.h"
#include "vstgui/lib/cvstguitimer.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace FairyTapes::Ui {
class NormalizedControl;
}

namespace FairyTapes::PillowTalk::Ui {

class DuckScope;

class Editor : public Steinberg::Vst::VSTGUIEditor, public VSTGUI::IControlListener {
public:
  static constexpr int kWidth = 480;
  static constexpr int kHeight = 560;

  explicit Editor(Steinberg::Vst::EditController* controller);
  ~Editor() override;

  // VSTGUIEditor
  bool PLUGIN_API open(void* parent, const VSTGUI::PlatformType& platformType) override;
  void PLUGIN_API close() override;
  // Edits are reported by the control listener below, not through the frame.
  void beginEdit(int32_t) override {}
  void endEdit(int32_t) override {}

  // IPlugView: fixed size.
  Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) override;
  Steinberg::tresult PLUGIN_API checkSizeConstraint(Steinberg::ViewRect* rect) override;

  // IControlListener
  void valueChanged(VSTGUI::CControl* control) override;
  void controlBeginEdit(VSTGUI::CControl* control) override;
  void controlEndEdit(VSTGUI::CControl* control) override;

  // IDependent (Parameter objects): may arrive on any thread, only flags the parameter.
  void PLUGIN_API update(Steinberg::FUnknown* changedUnknown, Steinberg::int32 message) override;

private:
  void createViews();
  void watchParameters();
  void unwatchParameters();
  void onTimer();
  // Copies the controller value into the control and applies the side effects (Sync, scope).
  void syncFromController(Steinberg::Vst::ParamID id);
  void applySideEffects(Steinberg::Vst::ParamID id, double norm);

  FairyTapes::Ui::Skin skin_;
  std::array<FairyTapes::Ui::NormalizedControl*, kNumParams> controls_{};  // owned by the frame
  DuckScope* scope_ = nullptr;                                              // owned by the frame
  std::array<Steinberg::IPtr<Steinberg::Vst::Parameter>, kNumParams> watched_{};
  std::atomic<uint32_t> pending_{0};  // one bit per ParamID changed by the host
  VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
};

} // namespace FairyTapes::PillowTalk::Ui
