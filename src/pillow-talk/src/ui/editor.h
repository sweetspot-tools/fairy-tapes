// Pillow Talk GUI: the plugin editor (480 x 560, views built in code, no .uidesc).
// Every persistent parameter is bound to one control. User gestures go to the controller as
// beginEdit / performEdit / endEdit; host-side changes (automation, presets, undo) arrive as
// Parameter dependents and are applied on the UI timer.
#pragma once

#include "params.h"
#include "ui/editor_size.h"

#include "fairy_ui/skin.h"

#include "public.sdk/source/vst/vstguieditor.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
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

class Editor : public Steinberg::Vst::VSTGUIEditor,
               public Steinberg::IPlugViewContentScaleSupport,
               public VSTGUI::IControlListener {
public:
  // Logical size; the window is this times the host's content scale factor.
  static constexpr int kWidth = kEditorWidth;
  static constexpr int kHeight = kEditorHeight;

  explicit Editor(Steinberg::Vst::EditController* controller);
  ~Editor() override;

  // VSTGUIEditor
  bool PLUGIN_API open(void* parent, const VSTGUI::PlatformType& platformType) override;
  void PLUGIN_API close() override;
  // Edits are reported by the control listener below, not through the frame.
  void beginEdit(int32_t) override {}
  void endEdit(int32_t) override {}

  // IPlugView: fixed logical size, scaled by the content scale factor.
  Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) override;
  Steinberg::tresult PLUGIN_API checkSizeConstraint(Steinberg::ViewRect* rect) override;

  // IPlugViewContentScaleSupport (Windows/Linux hosts; macOS scales natively, factor stays 1).
  Steinberg::tresult PLUGIN_API setContentScaleFactor(ScaleFactor factor) override;

  OBJ_METHODS(Editor, VSTGUIEditor)
  DEFINE_INTERFACES
    DEF_INTERFACE(Steinberg::IPlugViewContentScaleSupport)
  END_DEFINE_INTERFACES(VSTGUIEditor)
  REFCOUNT_METHODS(VSTGUIEditor)

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

  double scale_ = 1.0;  // sanitized content scale factor
  FairyTapes::Ui::Skin skin_;
  std::array<FairyTapes::Ui::NormalizedControl*, kNumParams> controls_{};  // owned by the frame
  DuckScope* scope_ = nullptr;                                              // owned by the frame
  std::array<Steinberg::IPtr<Steinberg::Vst::Parameter>, kNumParams> watched_{};
  std::atomic<uint32_t> pending_{0};  // one bit per ParamID changed by the host
  VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
};

} // namespace FairyTapes::PillowTalk::Ui
