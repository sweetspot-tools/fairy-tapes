// Deep Throat GUI: the plugin editor (1000 x 614 rack panel, views built in code, no .uidesc).
// Layers from the bottom (docs/DESIGN_SYSTEM.md): panel (PanelView: body, grain, boxes, printed texts) / silkscreen
// (SilkscreenView: knob scales, names, values, button labels, Comp selector values, block tags; ReadoutView: the
// read-only values) / wear (FairyTapes::Ui::WearView) / paper tapes (TapeView) / screws (FairyTapes::Ui::ScrewView) /
// SCOPE / controls / LEVEL rows and clip LEDs / BREATH lamp (an LED: a second ReadoutView, lamp only), all inside the
// front side (a container); "BACK ->" at the right of the header is on top.
// Back of the panel (shared FairyTapes::Ui::BackView, contents from ui/back_settings.h: SETTINGS, MODES, ROUTING,
// CREDITS): "BACK ->" turns the panel (FairyTapes::Ui::PanelFlip, about 0.8 s, run by the UI timer), "<- BACK" turns it
// again; a text field open on a tape or a value is confirmed first. The editor remembers the tab while it lives. The
// SETTINGS choices change the preferences: applied at once and saved in the file read at open (a failed write is
// ignored, the choice stays for the session); RE-ROLL gives the instance a new seed. BEHAVIOUR > LATENCY (MIX / LIVE,
// spec 9b) goes to the instance decor (Controller::editDecor: saved with the project, sent to the processor, latency
// restart asked of the host); with LIVE the front shows "LEVEL DETECTOR . LIVE" in amber in MODE, under the mode
// buttons, and the faint detector tag of BREATH says LEVEL in every mode.
// Wear: the seed comes from the instance decor (Controller::decor, saved with the project), the level from the user
// preferences (ui/prefs.h, read when the editor opens; a missing or bad file gives Light / Zeyada / values in the tape
// font). The marks change in place when the seed or the level changes (no rebuild); the UI timer polls
// Controller::decorRevision and applies a changed decor (state recall, undo, another editor) only then.
// Paper tapes: placed by layoutFromSeed (ui/decor.h) for the seed and the Wear level, written in the tape font of the
// preferences; a click opens a text field over the tape (Enter or leaving it confirms, Escape cancels) and the text
// (printable ASCII, at most 14 characters) goes to the instance decor (Controller::editDecor).
// Screws: the four corner screws for the seed (ui/screw_angles.h) plus the user's rotation; the end of a drag stores the
// rotation in the instance decor (one edit per gesture). reroll() gives the instance a new seed, rotations 0, tapes blank.
// The display values (meters, clips, scope, BREATH lamp) are the hidden read-only kDisp* parameters the processor writes;
// the UI timer reads them on every run. SCOPE and the GR row dim with the global Bypass only; the IN / OUT rows and the
// clip LEDs never dim. A breath (lamp, scope band) shows only with Breath IN on and no Bypass; the GR row is empty under
// Bypass. A click on a clip LED writes the kClearClips trigger (the processor clears and answers 0).
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
#include "ui/prefs.h"

#include "fairy_ui/flip_view.h"
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
#include <filesystem>
#include <string>
#include <vector>

namespace VSTGUI {
class CViewContainer;
class CTextEdit;
}

namespace FairyTapes::Ui {
class NormalizedControl;
class SilkscreenView;
class MeterBar;
class ClipLed;
class WearView;
class ScrewView;
class BackView;
class TextLink;
}

namespace FairyTapes::DeepThroat {
class Controller;
}

namespace FairyTapes::DeepThroat::Ui {

class PanelView;
class ReadoutView;
class ScopeView;
class TapeView;
struct EditorPreview;   // offscreen render tool (tools/render_editor.cpp)
struct EditorTest;      // headless editor tests (tests/test_editor.cpp)

class Editor : public Steinberg::Vst::VSTGUIEditor,
               public Steinberg::IPlugViewContentScaleSupport,
               public VSTGUI::IControlListener {
public:
  // Logical size; the window is this times the host's content scale factor.
  static constexpr int kWidth = kEditorWidth;
  static constexpr int kHeight = kEditorHeight;
  // Wear seed when the controller is not Deep Throat's (cannot happen in the plug-in).
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

  // IDependent (Parameter objects): may arrive on any thread, only flags the parameter for the UI timer.
  void PLUGIN_API update(Steinberg::FUnknown* changedUnknown, Steinberg::int32 message) override;

  // Appearance (the user preferences; SETTINGS on the back, Task 11): applied in place, nothing is rebuilt.
  // Wear level: the marks and the tapes for the level.
  void setWearLevel(WearLevel level);
  WearLevel wearLevel() const { return wearLevel_; }
  // Tape font: the writing on the tapes and the handwritten values.
  void setTapeFont(TapeFont font);
  TapeFont tapeFont() const { return tapeFont_; }
  // Values font: the yellow values in the tape font (true) or in plain text.
  void setValuesFont(bool handwritten);
  bool valuesHandwritten() const { return valuesHandwritten_; }
  // RE-ROLL: a new seed for the instance (new wear, tapes and screws), screw rotations back to 0, tapes blank.
  void reroll();
  uint32_t wearSeed() const { return seed_; }

private:
  friend struct EditorPreview;
  friend struct EditorTest;

  // Both sides of the panel in `host` (the frame in open(); a plain container in the render tool and the tests): the
  // front (a container holding the views of createViews), the hidden back and the turn between them.
  void createSides(VSTGUI::CViewContainer* host);
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
  // A click on a clip LED: writes the clear trigger (kClearClips = 1) as one edit.
  void clearClips();
  // The timer's display work: scope point, meter rows, clip LEDs, BREATH lamp from the kDisp* parameters.
  void updateDisplays();
  // A click on a knob's printed value: a text field there with the number.
  void editValue(Steinberg::Vst::ParamID id);
  // What was typed, read by the controller, applied as one edit; ignored when the controller refuses it.
  void commitTypedValue(Steinberg::Vst::ParamID id, const std::string& typed);
  // Removes the text field; without `confirm` what was typed is dropped (Escape).
  void closeValueEdit(bool confirm);

  // Reads the preferences in `file` (any problem: the defaults) and applies them (open() passes prefsPath()).
  void loadAppearance(const std::filesystem::path& file);
  // The controller's decor when its revision changed: seed (wear, tapes, screws), tape texts, screw rotations.
  void refreshDecor();
  // Marks for a seed and level, regenerated only when one of them changes.
  const std::vector<FairyTapes::Ui::WearMark>& wearMarks(uint32_t seed, WearLevel level);
  // Marks, tapes and the handwriting for the current seed and level.
  void applyWear();
  // The handwriting (values under the knobs, read-outs) for the current switch, font and seed.
  void applyHandwriting();
  // Tape views for the current seed and level (removed and created again), texts from tapeTexts_.
  void rebuildTapes();
  void refreshTapeTexts();
  // A click on a tape: a text field over it with what is written.
  void editTape(TapeView* tape);
  void commitTapeText(int slot, const std::string& typed);
  // Removes the tape's text field; without `confirm` what was typed is dropped (Escape).
  void closeTapeEdit(bool confirm);
  // The back of the panel, hidden, in `host`, on the current preferences and tab.
  void buildBack(VSTGUI::CViewContainer* host);
  // The back's wear: another pattern from the same seed, at the Wear level of the front.
  void applyBackWear();
  // Turns the panel to the back (true) or to the front; open text fields are confirmed first.
  void startFlip(bool toBack);
  // A SETTINGS choice on the back: applied to the preferences, saved (a failed write is ignored) and shown at once; a
  // BEHAVIOUR choice goes to the instance decor instead.
  void changeSetting(int key, int index);
  // The LATENCY of the instance (decor) on the back's row and in the front's texts (MODE, BREATH tag).
  void applyLatency(LatencyMode latency);
  // Screw views for the current seed and the decor's rotations (created once, then updated in place).
  void placeScrews();
  // The end of a drag on screw `index`, which now shows `shownDeg`.
  void screwTurned(int index, double shownDeg);

  double scale_ = 1.0;   // sanitized content scale factor
  FairyTapes::Ui::Skin skin_;
  VSTGUI::CViewContainer* root_ = nullptr;   // the container the views were created in
  PanelView* panel_ = nullptr;               // the views are owned by the frame (or that container)
  FairyTapes::Ui::SilkscreenView* silk_ = nullptr;
  ReadoutView* readouts_ = nullptr;   // the read-out texts (panel layer, under the wear)
  ReadoutView* lamp_ = nullptr;       // the BREATH lamp (an LED: top layer)
  FairyTapes::Ui::WearView* wear_ = nullptr;
  std::vector<TapeView*> tapes_;                                         // layoutFromSeed order
  std::array<FairyTapes::Ui::ScrewView*, kNumScrewViews> screws_{};      // kScrewPos order
  std::array<FairyTapes::Ui::NormalizedControl*, kNumParams> controls_{};
  ScopeView* scope_ = nullptr;
  std::array<FairyTapes::Ui::MeterBar*, kMeterRowCount> bars_{};   // kMeterRows order
  std::array<FairyTapes::Ui::ClipLed*, kClipCount> clips_{};       // kClips order
  DimState dim_{};

  // Appearance from the user preferences (defaults until open() reads them). prefs_ always holds what is applied;
  // prefsFile_ is where a SETTINGS choice is saved (empty: nowhere, e.g. in the tests and the render tool).
  Prefs prefs_;
  std::filesystem::path prefsFile_;
  Controller* owner_ = nullptr;   // the same object as getController(), when it is Deep Throat's controller
  WearLevel wearLevel_ = WearLevel::Light;
  TapeFont tapeFont_ = TapeFont::Zeyada;
  // The yellow values (under the knobs, the Comp selectors, the amber read-outs) in the tape font or in plain text.
  bool valuesHandwritten_ = true;
  FairyTapes::Ui::TapeFontId valuesFont_ = FairyTapes::Ui::TapeFontId::Zeyada;   // tapeFontIdOf(tapeFont_)

  // The instance decor as last seen (seed_ also varies the handwriting per control).
  uint32_t seed_ = kFallbackWearSeed;
  LatencyMode latency_ = LatencyMode::Mix;   // BEHAVIOUR > LATENCY of the instance
  uint32_t decorRevision_ = 0;
  bool decorKnown_ = false;
  std::vector<std::string> tapeTexts_;
  std::array<float, kNumScrewViews> screwAngles_{};
  std::vector<FairyTapes::Ui::WearMark> marks_;
  uint32_t marksSeed_ = 0;
  WearLevel marksLevel_ = WearLevel::Off;
  bool marksValid_ = false;

  // The two sides of the panel and the turn.
  VSTGUI::CViewContainer* front_ = nullptr;   // owned by the frame (or the host container)
  FairyTapes::Ui::BackView* back_ = nullptr;
  FairyTapes::Ui::TextLink* backLink_ = nullptr;   // "BACK ->" in the header
  FairyTapes::Ui::PanelFlip flip_;
  int backTab_ = 0;   // the back's tab, kept while the editor lives
  int modesColumn_ = 0;   // the MODES tab's yellow column: activeModeColumn(Mode), refreshed with the dynamic texts

  VSTGUI::CTextEdit* tapeEdit_ = nullptr;   // the tape's text field, while open
  int tapeEditSlot_ = -1;
  bool tapeEditReported_ = false;   // the field already sent its text (Enter or focus lost)

  VSTGUI::CTextEdit* valueEdit_ = nullptr;   // the typed-value field, while open
  Steinberg::Vst::ParamID valueEditId_ = kNumParams;

  std::array<Steinberg::IPtr<Steinberg::Vst::Parameter>, kNumParams> watched_{};
  std::atomic<uint32_t> pending_{0};   // one bit per ParamID changed by the host
  VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
};

} // namespace FairyTapes::DeepThroat::Ui
