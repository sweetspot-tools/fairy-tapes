// Hotline GUI: the plugin editor (1000 x 470 rack panel, views built in code, no .uidesc), front side.
// Layers from the bottom (docs/DESIGN_SYSTEM.md): panel (PanelView) / silkscreen (SilkscreenView) / wear (WearView,
// lets clicks through) and paper tapes (TapeView) / screws (FairyTapes::Ui::ScrewView) / controls, level meters and clip
// LEDs. Every persistent parameter is bound to one control (ui/layout.h). User gestures go to the controller as
// beginEdit / performEdit / endEdit; host-side changes (automation, presets, undo) arrive as Parameter dependents and are
// applied on the UI timer, which also reads the processor's display parameters (peaks, clip flags).
// Wear: the seed comes from the instance decor (Controller::decor, saved in the state), the level from the user
// preferences (ui/prefs.h, read when the editor opens; any problem -> defaults). The panel marks and the control wear
// (caps and faces, fairy_ui/control_wear.h) are regenerated only when one of the two changes (the timer polls
// Controller::decorRevision); setWearLevel changes the level live (the SETTINGS tab of Task 8b calls it).
// Paper tapes (between the wear and the screws): placed by layoutFromSeed (ui/decor.h) for the seed and the Wear level,
// written in the tape font of the preferences; a click opens a text field (at most 14 characters, Enter or leaving the
// field confirms, Escape cancels) and the text goes to the instance decor (Controller::editDecor).
// Corner screws (over the wear and the tapes, below the controls): look from the seed (ui/screw_angles.h, the same as the
// other plug-ins'), the missing one shows only its hole; dragging turns a screw and the end of the drag stores the
// user's rotation in the instance decor (Controller::editDecor, project marked dirty once).
// The back of the panel (SETTINGS, DEVICES, ROUTING, CREDITS) is Task 8b.
#pragma once

#include "params.h"
#include "state.h"
#include "ui/editor_size.h"
#include "ui/layout.h"
#include "ui/prefs.h"

#include "fairy_ui/skin.h"
#include "fairy_ui/tape_font.h"
#include "fairy_ui/wear.h"

#include "public.sdk/source/vst/vstguieditor.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "vstgui/lib/controls/icontrollistener.h"
#include "vstgui/lib/cvstguitimer.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace VSTGUI {
class CViewContainer;
class CTextEdit;
}

namespace FairyTapes::Ui {
class NormalizedControl;
class MeterBar;
class ClipLed;
class SilkscreenView;
class WearView;
class ScrewView;
}

namespace FairyTapes::Hotline {
class Controller;
}

namespace FairyTapes::Hotline::Ui {

class TapeView;
struct EditorPreview;   // offscreen render tool (tools/render_editor.cpp)

class Editor : public Steinberg::Vst::VSTGUIEditor,
               public Steinberg::IPlugViewContentScaleSupport,
               public VSTGUI::IControlListener {
public:
  // Logical size; the window is this times the host's content scale factor.
  static constexpr int kWidth = kEditorWidth;
  static constexpr int kHeight = kEditorHeight;

  // Wear used when the controller is not Hotline's (cannot happen in the plug-in) or before open().
  static constexpr uint32_t kFallbackWearSeed = FairyTapes::Ui::kDefaultWearSeed;
  static constexpr int kNumScrewViews = 4;   // one per kScrewPos (checked against the state in editor.cpp)

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

  // Wear level (SETTINGS on the back, Task 8b): panel marks, control wear and tapes change without rebuilding the window.
  void setWearLevel(FairyTapes::Ui::WearLevel level);
  FairyTapes::Ui::WearLevel wearLevel() const { return wearLevel_; }
  uint32_t wearSeed() const { return seed_; }
  // Tape font (SETTINGS on the back): the writing on the tapes changes in place.
  void setTapeFont(FairyTapes::Ui::TapeFontId font);
  FairyTapes::Ui::TapeFontId tapeFont() const { return tapeFont_; }

private:
  friend struct EditorPreview;

  void createViews(VSTGUI::CViewContainer* parent);
  void resetViewPointers();
  void watchParameters();
  void unwatchParameters();
  void onTimer();
  void updateDisplays();
  void clearClips();
  // Copies the controller value into the control and applies the side effects (value text, dimming).
  void syncFromController(Steinberg::Vst::ParamID id);
  void applySideEffects(Steinberg::Vst::ParamID id, double norm);
  void refreshDimming();
  // Marks for a seed and level, regenerated only when one of them changes.
  const std::vector<FairyTapes::Ui::WearMark>& wearMarks(uint32_t seed, FairyTapes::Ui::WearLevel level);
  // Seed, screw rotations and tape texts from the controller's decor; regenerates the wear when the seed changed.
  void refreshDecor();
  void applyWear();
  void applyControlWear();
  // Tape views for the current seed and Wear level (removed and created again), texts from tapeTexts_.
  void rebuildTapes();
  void refreshTapeTexts();
  void editTape(TapeView* tape);
  void commitTapeText(int slot, const std::string& typed);
  // Removes the text field; `confirm` first takes what was typed (unless Escape was pressed).
  void closeTapeEdit(bool confirm);
  // Screw views for the current seed and the decor's rotations (created once, then updated in place).
  void placeScrews();
  void screwTurned(int index, double shownDeg);

  double scale_ = 1.0;  // sanitized content scale factor
  FairyTapes::Ui::Skin skin_;
  std::array<FairyTapes::Ui::NormalizedControl*, kNumParams> controls_{};  // views are owned by the frame
  FairyTapes::Ui::SilkscreenView* silk_ = nullptr;
  FairyTapes::Ui::WearView* wear_ = nullptr;
  VSTGUI::CViewContainer* root_ = nullptr;   // the container the views were created in
  std::vector<TapeView*> tapes_;
  std::array<FairyTapes::Ui::ScrewView*, kNumScrewViews> screws_{};
  VSTGUI::CTextEdit* tapeEdit_ = nullptr;
  int tapeEditSlot_ = -1;
  std::array<FairyTapes::Ui::MeterBar*, kMeterRowCount> bars_{};
  std::array<FairyTapes::Ui::ClipLed*, kClipCount> clips_{};
  ModuleState state_{};

  Prefs prefs_;   // the user preferences, as loaded at open
  FairyTapes::Ui::TapeFontId tapeFont_ = FairyTapes::Ui::TapeFontId::Zeyada;
  std::vector<FairyTapes::Ui::WearMark> marks_;
  uint32_t marksSeed_ = 0;
  FairyTapes::Ui::WearLevel marksLevel_ = FairyTapes::Ui::WearLevel::Off;
  bool marksValid_ = false;

  Controller* owner_ = nullptr;   // the same object as getController(), when it is Hotline's controller
  uint32_t seed_ = kFallbackWearSeed;
  FairyTapes::Ui::WearLevel wearLevel_ = FairyTapes::Ui::WearLevel::Light;
  uint32_t decorRevision_ = 0;
  std::vector<std::string> tapeTexts_;               // the decor's tape texts, as last seen
  std::array<float, kNumScrewViews> screwAngles_{};  // the decor's screw rotations, as last seen
  bool decorKnown_ = false;

  std::array<Steinberg::IPtr<Steinberg::Vst::Parameter>, kNumParams> watched_{};
  std::atomic<uint32_t> pending_{0};  // one bit per ParamID changed by the host
  VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
};

} // namespace FairyTapes::Hotline::Ui
