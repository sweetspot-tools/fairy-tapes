// The Stripper GUI: the plugin editor (1000 x 628, views built in code, no .uidesc).
// Every control is bound to one parameter through the layout table (ui/layout.h). User gestures reach the
// controller as beginEdit / performEdit / endEdit; host-side changes (automation, presets, undo) are picked up
// by polling the controller on the UI timer, so no parameter listener can outlive the editor.
#pragma once

#include "params.h"
#include "preferences.h"
#include "state.h"
#include "ui/layout.h"
#include "ui/tape_kit.h"

#include "fairy_ui/flip_view.h"

#include "fairy_ui/skin.h"

#include "public.sdk/source/vst/vstguieditor.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "vstgui/lib/controls/icontrollistener.h"
#include "vstgui/lib/cvstguitimer.h"

#include <array>
#include <functional>
#include <memory>
#include <chrono>
#include <string>

namespace VSTGUI {
class CTextEdit;
}

namespace FairyTapes::Ui {
class WearView;
class NormalizedControl;
class SilkscreenView;
class MeterBar;
class ClipLed;
class ScrewView;
class BackView;
}

namespace FairyTapes::TheStripper {
class Controller;
}

namespace FairyTapes::TheStripper::Ui {

class ModelBrowser;
class ModelStrip;
class RecallMarksView;
class TapePieceView;

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
  void showValue(const ControlSpec& spec, double paramNorm);   // yellow value under a knob (silkscreen)
  void updateMeters();
  void updateModel();      // model strip and Drive dimming from the controller's model status
  void openBrowser();
  void applyAppearance(const Preferences& prefs);   // wear, fonts, meter settings: live, from the back panel
  void updateRecallMarks();                       // current and recalled values of the knobs, for the pen marks
  void placeScrews(bool useSavedAngles);          // the four screws of the current seed, at their saved angles or the initial ones
  void screwsTurned();                            // a screw was turned: save the four angles
  void rebuildPieces();                           // the pieces of tape of the current seed, in their place in the layers
  void startFlip(bool toBack);
  void changePreference(const std::function<void(Preferences&)>& mutate);
  void editValue(const ControlSpec& spec);   // opens the text field over the printed value of a knob
  void closeValueEdit();
  void commitValue(const ControlSpec& spec, double paramNorm);   // a value typed in (same path as turning the knob)
  void editTape(TapePieceView* piece);   // opens the text field over a piece of tape
  void closeTapeEdit();
  void refreshModules();
  const ControlSpec* specOf(VSTGUI::CControl* control) const;

  double scale_ = 1.0;   // sanitized content scale factor
  Controller* ctl_ = nullptr;   // referenced (addRef) for the lifetime of the editor
  FairyTapes::Ui::Skin skin_;
  std::array<FairyTapes::Ui::NormalizedControl*, kNumParams> controls_{};   // owned by the frame
  std::array<const ControlSpec*, kNumParams> specs_{};
  std::array<double, kNumParams> seen_{};   // controller value last copied into the control
  std::array<FairyTapes::Ui::MeterBar*, kMeterRowCount> bars_{};   // owned by the frame, in kMeterRows order
  std::array<FairyTapes::Ui::ClipLed*, kClipCount> clips_{};
  std::chrono::steady_clock::time_point lastTick_{};
  VSTGUI::CViewContainer* front_ = nullptr;          // the front of the panel (all views of createViews), owned by the frame
  FairyTapes::Ui::BackView* backView_ = nullptr;     // the back, owned by the frame (hidden until the panel is turned)
  FairyTapes::Ui::PanelFlip flip_;                   // the turn of the panel (snapshots, FlipView, timing), shared/ui
  Preferences prefs_;
  FairyTapes::Ui::WearView* wearView_ = nullptr;     // owned by front_
  FairyTapes::Ui::SilkscreenView* silk_ = nullptr;   // owned by the frame
  bool tapeValues_ = false;                // the values under the knobs are handwritten (preference)
  TapeFontId valuesFont_ = TapeFontId::Zeyada;
  std::uint32_t valuesSeed_ = 0;
  RecallMarksView* marks_ = nullptr;                      // owned by front_
  std::array<FairyTapes::Ui::ScrewView*, 4> screws_{};                    // owned by front_
  std::array<TapePieceView*, kMaxTapePieces> pieces_{};   // owned by the frame
  VSTGUI::CTextEdit* valueEdit_ = nullptr;               // the open value field, owned by the frame
  const ControlSpec* valueEditSpec_ = nullptr;
  VSTGUI::CTextEdit* tapeEdit_ = nullptr;                // the open text field, owned by the frame
  int tapeEditIndex_ = -1;
  bool modelGpu_ = false;
  std::string stripTip_;                   // the hint on the model strip (setTooltipText keeps the pointer)
  ModelStrip* strip_ = nullptr;            // owned by the frame
  ModelBrowser* browser_ = nullptr;        // owned by the frame while the popup is open
  bool browserClosing_ = false;            // set by the popup, handled on the UI timer
  int modelState_ = -1;                    // last controller status shown (0 idle, 1 loading, 2 loaded, 3 failed)
  std::string modelName_, modelError_;
  bool globalBypass_ = false;
  bool namOn_ = true;    // the Drive's NAM engine switch
  bool tapeOn_ = true;   // the Drive's TAPE engine switch
  bool channelMode_ = true;
  VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
};

} // namespace FairyTapes::TheStripper::Ui
