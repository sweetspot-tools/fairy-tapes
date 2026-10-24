// Deep Throat GUI: the plugin editor (1000 x 614 rack panel, views built in code, no .uidesc).
// Layers from the bottom (docs/DESIGN_SYSTEM.md): panel (PanelView: body, grain, boxes, printed texts) / silkscreen
// (SilkscreenView: knob scales, names, values, button labels, Comp selector values, block tags; ReadoutView: the
// read-only values and the BREATH lamp) / controls. Wear, tapes, screws, meters, scope and the back of the panel come
// with the next tasks of docs/superpowers/plans/2026-10-24-deep-throat-gui.md.
// Every one of the 25 parameters is bound to one control (ui/layout.h kControls). User gestures go to the controller as
// beginEdit / performEdit / endEdit; a double click on a knob is its default (one edit); a click on a knob's printed
// value opens a text field (Enter or leaving it confirms, Escape cancels) whose text the controller reads
// (getParamValueByString; a text it refuses is ignored). Host-side changes (automation, presets, undo) arrive as
// Parameter dependents: update() only flags the parameter, the UI timer applies it to the control, the printed values,
// the dynamic texts (ui/dynamic_text.h) and the dimming (a module's IN off dims its parts, the global Bypass all but
// itself), without sending anything back.
#pragma once

#include "params.h"
#include "ui/editor_size.h"
#include "ui/layout.h"

#include "fairy_ui/skin.h"
#include "fairy_ui/tape_font.h"

#include "public.sdk/source/vst/vstguieditor.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "vstgui/lib/controls/icontrollistener.h"
#include "vstgui/lib/cvstguitimer.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

namespace VSTGUI {
class CViewContainer;
class CTextEdit;
}

namespace FairyTapes::Ui {
class NormalizedControl;
class SilkscreenView;
}

namespace FairyTapes::DeepThroat::Ui {

class PanelView;
class ReadoutView;
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

  // IControlListener
  void valueChanged(VSTGUI::CControl* control) override;
  void controlBeginEdit(VSTGUI::CControl* control) override;
  void controlEndEdit(VSTGUI::CControl* control) override;

  // IDependent (Parameter objects): may arrive on any thread, only flags the parameter for the UI timer.
  void PLUGIN_API update(Steinberg::FUnknown* changedUnknown, Steinberg::int32 message) override;

private:
  friend struct EditorPreview;
  friend struct EditorTest;

  // Builds the layers in `parent` (the frame in open(); a plain container in the render tool and the tests) and starts
  // watching the parameters.
  void createViews(VSTGUI::CViewContainer* parent);
  void watchParameters();
  void unwatchParameters();
  // The UI timer's work: the parameters flagged by update() go to the views; also safe with no views.
  void onTimer();
  // Copies the controller's value into the control and applies the side effects.
  void syncFromController(Steinberg::Vst::ParamID id);
  // Printed value, dynamic texts and dimming for a new value of `id`.
  void applySideEffects(Steinberg::Vst::ParamID id, double norm);
  // Tags, read-outs and Comp selector values from Mode, EQ Amount, Tone and the Comp choices (ui/dynamic_text.h).
  void refreshDynamicTexts();
  // dim_ -> controls, silkscreen, read-outs, tags and panel texts.
  void refreshDimming();
  void resetViewPointers();
  // A click on a knob's printed value: a text field there with the number.
  void editValue(Steinberg::Vst::ParamID id);
  // What was typed, read by the controller, applied as one edit; ignored when the controller refuses it.
  void commitTypedValue(Steinberg::Vst::ParamID id, const std::string& typed);
  // Removes the text field; without `confirm` what was typed is dropped (Escape).
  void closeValueEdit(bool confirm);

  double scale_ = 1.0;   // sanitized content scale factor
  FairyTapes::Ui::Skin skin_;
  VSTGUI::CViewContainer* root_ = nullptr;   // the container the views were created in
  PanelView* panel_ = nullptr;               // the views are owned by the frame (or that container)
  FairyTapes::Ui::SilkscreenView* silk_ = nullptr;
  ReadoutView* readouts_ = nullptr;
  std::array<FairyTapes::Ui::NormalizedControl*, kNumParams> controls_{};
  DimState dim_{};

  // The yellow values (under the knobs, the Comp selectors, the amber read-outs) in the tape font or in plain text: the
  // one switch the VALUES FONT preference will set (Task 10); until then the preference default, the tape font.
  bool valuesHandwritten_ = true;
  FairyTapes::Ui::TapeFontId valuesFont_ = FairyTapes::Ui::TapeFontId::Zeyada;
  uint32_t valuesSeed_ = 0x5eedu;   // per-control variation of the handwriting (the instance seed comes with Task 10)

  VSTGUI::CTextEdit* valueEdit_ = nullptr;   // the typed-value field, while open
  Steinberg::Vst::ParamID valueEditId_ = kNumParams;

  std::array<Steinberg::IPtr<Steinberg::Vst::Parameter>, kNumParams> watched_{};
  std::atomic<uint32_t> pending_{0};   // one bit per ParamID changed by the host
  VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
};

} // namespace FairyTapes::DeepThroat::Ui
