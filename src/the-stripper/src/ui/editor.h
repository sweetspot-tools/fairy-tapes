// The Stripper GUI: the plugin editor (1000 x 628, views built in code, no .uidesc).
// Every control is bound to one parameter through the layout table (ui/layout.h). User gestures reach the
// controller as beginEdit / performEdit / endEdit; host-side changes (automation, presets, undo) are picked up
// by polling the controller on the UI timer, so no parameter listener can outlive the editor.
#pragma once

#include "params.h"
#include "ui/layout.h"

#include "fairy_ui/skin.h"

#include "public.sdk/source/vst/vstguieditor.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "vstgui/lib/controls/icontrollistener.h"
#include "vstgui/lib/cvstguitimer.h"

#include <array>

namespace FairyTapes::Ui {
class NormalizedControl;
}

namespace FairyTapes::TheStripper {
class Controller;
}

namespace FairyTapes::TheStripper::Ui {

class Editor : public Steinberg::Vst::VSTGUIEditor,
               public Steinberg::IPlugViewContentScaleSupport,
               public VSTGUI::IControlListener {
public:
  static constexpr int kWidth = kEditorWidth;
  static constexpr int kHeight = kEditorHeight;

  explicit Editor(Controller* controller);
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

private:
  friend struct EditorPreview;   // tools/render_editor.cpp draws the views offscreen
  void createViews(VSTGUI::CViewContainer* parent);
  void onTimer();
  // Copies controller values into the controls (all of them when `force`), then refreshes dimming and mode.
  void syncFromController(bool force);
  void refreshModules();
  const ControlSpec* specOf(VSTGUI::CControl* control) const;

  double scale_ = 1.0;   // sanitized content scale factor
  Controller* ctl_ = nullptr;   // referenced (addRef) for the lifetime of the editor
  FairyTapes::Ui::Skin skin_;
  std::array<FairyTapes::Ui::NormalizedControl*, kNumParams> controls_{};   // owned by the frame
  std::array<const ControlSpec*, kNumParams> specs_{};
  std::array<double, kNumParams> seen_{};   // controller value last copied into the control
  bool channelMode_ = true;
  VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
};

} // namespace FairyTapes::TheStripper::Ui
