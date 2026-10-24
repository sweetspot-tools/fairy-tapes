// Deep Throat GUI: the plugin editor (1000 x 614 rack panel, views built in code, no .uidesc).
// Layers from the bottom (docs/DESIGN_SYSTEM.md): panel (PanelView: body, grain, boxes, printed texts) / silkscreen /
// wear / tapes / screws / controls. This first version builds the panel layer only; the controls, meters, scope,
// decor and the back of the panel come with the next tasks of docs/superpowers/plans/2026-10-24-deep-throat-gui.md.
// The UI timer (onTimer) reads Bypass and the six IN parameters from the controller and dims the panel texts.
#pragma once

#include "params.h"
#include "ui/editor_size.h"
#include "ui/layout.h"

#include "fairy_ui/skin.h"

#include "public.sdk/source/vst/vstguieditor.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "vstgui/lib/controls/icontrollistener.h"
#include "vstgui/lib/cvstguitimer.h"

#include <cstdint>

namespace VSTGUI {
class CViewContainer;
}

namespace FairyTapes::DeepThroat::Ui {

class PanelView;
struct EditorPreview;   // offscreen render tool (tools/render_editor.cpp)
struct EditorTest;      // headless editor tests (tests/test_editor.cpp)

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

  // IControlListener (no controls yet: they arrive with the controls layer).
  void valueChanged(VSTGUI::CControl*) override {}

private:
  friend struct EditorPreview;
  friend struct EditorTest;

  // Builds the layers in `parent` (the frame in open(); a plain container in the render tool and the tests).
  void createViews(VSTGUI::CViewContainer* parent);
  // The UI timer's work; also safe with no views.
  void onTimer();
  // Bypass and the IN parameters from the controller -> dimming of the panel texts.
  void refreshDimming();
  void resetViewPointers();

  double scale_ = 1.0;   // sanitized content scale factor
  FairyTapes::Ui::Skin skin_;
  PanelView* panel_ = nullptr;   // owned by the frame (or the container the views were built in)
  VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
};

} // namespace FairyTapes::DeepThroat::Ui
