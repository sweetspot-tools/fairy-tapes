// Happy Ending GUI: the plugin editor (1280 x 890 rack panel, views built in code, no .uidesc).
// Layers from the bottom (docs/DESIGN_SYSTEM.md): panel (PanelView: body, grain, boxes with their titles on the bottom
// border, the screen's untitled box, the LUFS screen's box, header lines, printed texts, engine tags), the silkscreen, the
// controls, the LEVEL views and the shared screen; wear, paper tapes and screws (below) lie between the silkscreen and the
// screen. The panel has two sides in the frame (Task 13): the front (a container with all of the above and the "BACK ->"
// link) and the back (FairyTapes::Ui::BackView on Happy Ending's own layout, ui/back_layout.h and ui/back_settings.h:
// SETTINGS, BUTTONS, ROUTING, CREDITS; the last three are filled by the editor plan's Task 14), with the turn between them
// (FairyTapes::Ui::PanelFlip, about 0.8 s, run by the UI timer; a text field open on the front is confirmed first).
// Window: 1280 x 890 logical px times the host's content scale factor (IPlugViewContentScaleSupport) times the SIZE of
// this plug-in's preferences (ui/prefs.h, HappyEnding.prefs, key size=75|90|100|125), read when the view is made and
// again when it opens; the frame is zoomed by the same factor, so the views keep their logical coordinates.
// checkSizeConstraint accepts only that size (no free resizing). A new host factor, or a new SIZE chosen on the back, while
// the views are shown asks the host for the new window (IPlugFrame::resizeView); if the host refuses, the window keeps its
// size and zoom (SIZE: the choice is still saved and applies the next time the editor opens).
// SETTINGS (Task 13): WEAR, TAPE FONT, VALUES FONT and SIZE are the user's preferences, applied at once and saved to
// HappyEnding.prefs (a failed write is ignored: the choice still applies to this editor); RE-ROLL is the instance decor;
// OVERSAMPLING is the stored parameter kOversampling, written as one host edit (the controller does the rest).
// Controls (Task 9): one view per ControlSpec of kControls (RackKnob; RackToggle for IN, BYPASS, WARM, SET and the three
// LISTEN; RackLitChoice for LIMIT STYLE and FINAL MODE; RackCycle for M/S, RATIO, SC HPF, CEIL, TARGET; DYN a small
// header button with its name on a face that lights amber), MATCH (momentary, Controller::doMatch) and the five screen
// view buttons (one face-lit choice; the view is kept by the controller while the instance lives, never in the state).
// Above the panel the silkscreen prints their names, scales and values (formatValue; LIMIT RELEASE in real ms,
// releaseText; HI PASS FREQ with the scale of its M/S). Parameter dependents: update() only raises a flag, the UI timer
// then reads the controller and applies the values, the printed texts and the UiState (ui/layout.h): dimming and locking
// (controlDimmed / controlLocked), FINAL's controls (controlVisible), the LISTEN LEDs of the bell really heard
// (listenedBand) and the panel layer. User gestures go to the host as beginEdit / performEdit / endEdit (one edit, on the
// grid of the parameter's steps); a double click on a knob is its default; a click on the value under a knob opens a text
// field (Enter or leaving it confirms through parseValue, Escape cancels). The three LISTEN buttons write kListenBand (a
// click on the lit one switches it off). close() switches LISTEN and SET off (one edit each) when they are on.
// LEVEL (Task 10, ui/level_views.h): the four peak rows, the five GR rows with their tags, the nine clip LEDs, the LUFS
// screen (short-term, integrated, TO TARGET with its green LED) and the two ACT LEDs of the DYN buttons show the hidden
// display parameters, read on every UI timer run (they are not watched); the peak rows span -60..+6 dB over 15 LEDs (the
// scale the processor publishes and the one printed under them). The GR rows, the stage clip LEDs and the ACT LEDs dim with BYPASS and their stage's IN (a dimmed
// GR row shows no reduction); the peak rows, the IN and OUT clip LEDs and the LUFS screen are always live. A click on a clip
// LED writes the kClearClips trigger, one on the integrated loudness kResetIntegrated (one host edit each).
// SCREEN (Task 11, ui/screen_view.h): one view on kScreenRect for the five views EQ, GLUE, CLIP, LIMIT and FINAL (the view
// buttons pick it; the controller keeps it). refreshFromController() hands it the stored table and the UiState, the timer
// run the display parameters (both histories grow on every run); it dims with BYPASS only.
// WEAR, TAPES, SCREWS (Task 12; layers between the silkscreen and the screen / controls): the wear marks (FairyTapes::Ui::
// WearView, lets clicks through), the paper tapes (TapeView, writable) and the four corner screws (FairyTapes::Ui::ScrewView,
// turnable). The seed, the tape texts and the screw rotations are the instance decor (Controller::decor, saved with the
// project); the Wear level, the tape font and the values font are the user's preferences (ui/prefs.h, read when the
// editor is made and when it opens; a missing or bad file gives Light / Zeyada / handwritten values). Controller::decor() has
// seed 0 until the processor, a state or an edit gives one: seed 0 means "decor not ready" and nothing seeded is drawn (no
// marks, no tape, no screw; the values still use the tape font, with a fixed variation seed); the timer builds them when
// Controller::decorRevision changes to a decor with a seed, and applies a changed decor (state recall, undo, another
// editor) only then. A click on a tape opens a text field over it (Enter or leaving it confirms, Escape cancels; printable
// ASCII, at most 14 characters, through one Controller::editDecor); the end of a screw drag stores its rotation (one
// editDecor per gesture). reroll() gives the instance a new seed, screw rotations 0 and blank tapes.
// Values font: the yellow values under the knobs and the cycling selectors are drawn in the tape font with the
// normalized digit height (fairy_ui/tape_font.h) while "handwritten values" is on (the default); the LUFS screen is an
// instrument readout, not a printed value, and stays in the standard font (as the preview: it only writes the knob and
// selector values by hand).
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
#include <filesystem>
#include <cstdint>
#include <string>
#include <vector>

namespace VSTGUI {
class CTextEdit;
class CViewContainer;
}

namespace FairyTapes::Ui {
class BackView;
class ClipLed;
class NormalizedControl;
class ScrewView;
class SilkscreenView;
class TextLink;
class WearView;
}

namespace FairyTapes::HappyEnding {
class Controller;
}

namespace FairyTapes::HappyEnding::Ui {

class ActLed;
class GrTags;
class LevelBar;
class LufsView;
class PanelView;
class ScreenDisplayView;
class TapeView;
struct EditorPreview;   // offscreen render tool (tools/render_editor.cpp)
struct EditorTest;      // headless editor tests (tests/test_editor.cpp)

class Editor : public Steinberg::Vst::VSTGUIEditor,
               public Steinberg::IPlugViewContentScaleSupport,
               public VSTGUI::IControlListener {
public:
  // Logical size; the window is this times the host's content scale factor times SIZE.
  static constexpr int kWidth = kEditorWidth;
  static constexpr int kHeight = kEditorHeight;
  static constexpr int kNumScrewViews = 4;   // one per kScrewPos (checked against the state in editor.cpp)

  // Reads SIZE from the user's preferences (a missing or bad file: 100 %), so the host's first getSize is right.
  explicit Editor(Steinberg::Vst::EditController* controller);
  ~Editor() override;

  // VSTGUIEditor
  bool PLUGIN_API open(void* parent, const VSTGUI::PlatformType& platformType) override;
  void PLUGIN_API close() override;
  // Edits are reported by the control listener below, not through the frame.
  void beginEdit(int32_t) override {}
  void endEdit(int32_t) override {}

  // Appearance (the user's preferences; SETTINGS on the back): applied in place, nothing is rebuilt. They update the
  // preferences in memory and the back's rows, but do not save the file: the back's choices do (changeSetting).
  // Wear level: the marks and the tapes for the level (Off: no mark and no tape; the screws stay).
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
  // The seed the decor is drawn with; 0 while the decor is not ready.
  std::uint32_t wearSeed() const { return seed_; }

  // IPlugView: fixed size (logical x host factor x SIZE).
  Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) override;
  Steinberg::tresult PLUGIN_API checkSizeConstraint(Steinberg::ViewRect* rect) override;

  // IPlugViewContentScaleSupport (Windows/Linux hosts; macOS scales natively, factor stays 1).
  Steinberg::tresult PLUGIN_API setContentScaleFactor(ScaleFactor factor) override;

  OBJ_METHODS(Editor, VSTGUIEditor)
  DEFINE_INTERFACES
    DEF_INTERFACE(Steinberg::IPlugViewContentScaleSupport)
  END_DEFINE_INTERFACES(VSTGUIEditor)
  REFCOUNT_METHODS(VSTGUIEditor)

  // IControlListener: the controls' gestures to the host (controlBeginEdit / valueChanged / controlEndEdit).
  void valueChanged(VSTGUI::CControl* control) override;
  void controlBeginEdit(VSTGUI::CControl* control) override;
  void controlEndEdit(VSTGUI::CControl* control) override;

  // IDependent (the controller's Parameter objects): may arrive on any thread, only flags a change for the UI timer.
  void PLUGIN_API update(Steinberg::FUnknown* changedUnknown, Steinberg::int32 message) override;

private:
  friend struct EditorPreview;
  friend struct EditorTest;

  // Both sides of the panel in `host` (the frame in open(); a plain container in the render tool and the tests): the front (a
  // container holding the views of createViews), the hidden back and the turn between them.
  void createSides(VSTGUI::CViewContainer* host);
  // Builds the layers in `parent` (the front container; a plain container in the tests that need no back).
  void createViews(VSTGUI::CViewContainer* parent);
  void resetViewPointers();
  // The UI timer (30 ms) running onTimer; open() starts it, close() stops it.
  void startTimer();
  // The UI timer's work: a flagged change of the controller's parameters to the views; the display parameters to the
  // LEVEL views; the typed value field that lost the focus is done. Also safe with no views.
  void onTimer();
  // The display parameters (hidden, written by the processor) to the peak rows, GR rows, clip LEDs, LUFS screen and ACT
  // LEDs. Read on every timer run: they are not watched.
  void updateDisplays();
  // Every view from the controller's parameters: the controls' values (stepped values on the step the DSP uses), the
  // printed values, and the UiState (dimming, locking, FINAL's controls, the LISTEN LEDs, the HI PASS scale, the panel).
  void refreshFromController();
  // The stored parameters, LISTEN and SET as dependents of this editor (update() raises pending_).
  void watchParameters();
  void unwatchParameters();

  // One user edit of a parameter outside a control gesture (begin, set, perform, end).
  void editParameter(Steinberg::Vst::ParamID id, double norm);
  // The hidden triggers of the processor (the editor writes 1, the processor clears and answers 0): one click on a clip
  // LED clears every clip LED, one on the integrated loudness restarts it.
  void clearClips() { editParameter(kClearClips, 1.0); }
  void resetIntegrated() { editParameter(kResetIntegrated, 1.0); }
  // close(): LISTEN and SET back to off, one edit each, only when they are on.
  void switchSessionParametersOff();

  // Typed values: a text field over the value of the knob of `id`; closing it (confirm: what was typed is read).
  void editValue(Steinberg::Vst::ParamID id);
  void closeValueEdit(bool confirm);
  void commitTypedValue(Steinberg::Vst::ParamID id, const std::string& typed);

  // The controller's decor when its revision changed: seed (wear, tapes, screws), tape texts, screw rotations.
  void refreshDecor();
  const std::vector<FairyTapes::Ui::WearMark>& wearMarks(std::uint32_t seed, WearLevel level);
  // Marks, tapes and the handwriting for the current seed and level (nothing seeded while the seed is 0).
  void applyWear();
  // The silkscreen again after a change of the values font or of the seed (the handwriting varies with it).
  void applyHandwriting();
  // Tape views for the current seed and level (removed and created again), texts from tapeTexts_.
  void rebuildTapes();
  void refreshTapeTexts();
  // A click on a tape: a text field over it with what is written.
  void editTape(TapeView* tape);
  void commitTapeText(int slot, const std::string& typed);
  // Removes the tape's text field; without `confirm` what was typed is dropped (Escape).
  void closeTapeEdit(bool confirm);
  // Screw views for the current seed and the decor's rotations (created once, then updated in place).
  void placeScrews();
  // The end of a drag on screw `index`, which now shows `shownDeg`.
  void screwTurned(int index, double shownDeg);

  // The back of the panel, hidden, in `host`, on the current preferences, OVERSAMPLING and tab.
  void buildBack(VSTGUI::CViewContainer* host);
  // The back's wear: another pattern from the same seed, at the Wear level of the front (none while the decor is not ready).
  void applyBackWear();
  // Turns the panel to the back (true) or to the front; open text fields are confirmed first.
  void startFlip(bool toBack);
  // A SETTINGS choice on the back: a preference is applied, saved (a failed write is ignored) and shown at once; OVERSAMPLING
  // is written to the stored parameter as one host edit.
  void changeSetting(int key, int index);
  // The step of OVERSAMPLING as the controller has it.
  int oversamplingStep() const;

  // Reads the preferences in `file` (any problem: the defaults) and applies them: SIZE (the window), Wear, tape font, values
  // font. `file` is also where the back's choices are saved (empty: nowhere).
  // withSize false leaves the window and the SIZE choice (prefs_.size) as they are.
  void loadAppearance(const std::filesystem::path& file, bool withSize = true);
  // What open() does with the preferences: Wear and the fonts again, never SIZE.
  void reloadAppearanceOnOpen();
  // SIZE in use: the preference when the window follows it. Asks for the new window while the views are shown.
  void setSize(EditorSize size);
  // The window for a host factor and a SIZE: the reported size (setRect), the frame's zoom and, while the views are shown,
  // a request to the host (IPlugFrame::resizeView). The host may refuse: then rect and zoom stay as they were and false is
  // returned (scale_ and size_ are only changed when it returns true).
  bool applyWindowSize(double scale, EditorSize size);
  PixelSize windowSize() const { return scaledEditorSize(scale_, size_); }

  Controller* owner_ = nullptr;   // the plug-in's controller (MATCH, the screen view); nullptr for any other
  double scale_ = 1.0;   // sanitized content scale factor of the host
  Prefs prefs_;          // the user preferences as last read or chosen (SIZE in prefs_.size)
  EditorSize size_ = EditorSize::S100;   // the SIZE the window has (prefs_.size, unless the host refused the change)
  std::filesystem::path prefsFile_;      // where a SETTINGS choice is saved (empty: nowhere)
  FairyTapes::Ui::Skin skin_;
  VSTGUI::CViewContainer* root_ = nullptr;   // the container the views were created in (shown while not null)
  PanelView* panel_ = nullptr;               // the views are owned by the frame (or that container)
  FairyTapes::Ui::SilkscreenView* silk_ = nullptr;
  std::array<FairyTapes::Ui::NormalizedControl*, kNumControls> controls_{};   // kControls order
  FairyTapes::Ui::NormalizedControl* match_ = nullptr;   // MATCH (momentary)
  FairyTapes::Ui::NormalizedControl* views_ = nullptr;   // the five screen view buttons, one exclusive choice
  // LEVEL (kMeterRows, kGrRows, kClipLeds order), the LUFS screen and the ACT LEDs (LOW, AIR).
  std::array<LevelBar*, 4> meters_{};
  std::array<LevelBar*, 5> grBars_{};
  std::array<FairyTapes::Ui::ClipLed*, 9> clips_{};
  GrTags* grTags_ = nullptr;
  LufsView* lufs_ = nullptr;
  FairyTapes::Ui::WearView* wear_ = nullptr;
  std::vector<TapeView*> tapes_;                                         // layoutFromSeed order
  std::array<FairyTapes::Ui::ScrewView*, kNumScrewViews> screws_{};      // kScrewPos order
  ScreenDisplayView* screen_ = nullptr;   // the shared screen (Task 11)
  std::array<ActLed*, 2> actLeds_{};
  UiState state_{};   // as last read from the controller (dims the GR rows, the clip LEDs and the ACT LEDs)
  VSTGUI::CTextEdit* valueEdit_ = nullptr;               // the open typed value field, if any
  Steinberg::Vst::ParamID valueEditId_ = kNumParams;
  bool valueEditReported_ = false;                       // the field already reported what was typed
  VSTGUI::CTextEdit* tapeEdit_ = nullptr;                // the tape's text field, while open
  int tapeEditSlot_ = -1;
  bool tapeEditReported_ = false;                        // the field already sent its text (Enter or focus lost)

  // Appearance: the user's preferences as applied, and the instance decor as last seen.
  WearLevel wearLevel_ = WearLevel::Light;
  TapeFont tapeFont_ = TapeFont::Zeyada;
  bool valuesHandwritten_ = true;                       // the yellow values in the tape font
  FairyTapes::Ui::TapeFontId valuesFont_ = FairyTapes::Ui::TapeFontId::Zeyada;   // tapeFontIdOf(tapeFont_)
  std::uint32_t seed_ = 0;                              // the decor's seed; 0 = not ready, nothing seeded is drawn
  std::uint32_t decorRevision_ = 0;
  bool decorKnown_ = false;
  std::vector<std::string> tapeTexts_;
  std::array<float, kNumScrewViews> screwAngles_{};
  std::vector<FairyTapes::Ui::WearMark> marks_;         // cache of the last generated marks
  std::uint32_t marksSeed_ = 0;
  WearLevel marksLevel_ = WearLevel::Off;
  bool marksValid_ = false;

  // The two sides of the panel and the turn.
  VSTGUI::CViewContainer* front_ = nullptr;   // owned by the frame (or the host container); root_ is this one in the frame
  FairyTapes::Ui::BackView* back_ = nullptr;
  FairyTapes::Ui::TextLink* backLink_ = nullptr;   // "BACK ->" in the header of the front
  VSTGUI::CView* oversamplingNote_ = nullptr;      // the line under OVERSAMPLING, shown on the SETTINGS tab only
  FairyTapes::Ui::PanelFlip flip_;
  int backTab_ = 0;   // the back's tab, kept while the editor lives

  // The watched parameters: the stored table, then LISTEN and SET.
  static constexpr int kNumWatched = static_cast<int>(kNumParams) + 2;
  std::array<Steinberg::IPtr<Steinberg::Vst::Parameter>, kNumWatched> watched_{};
  std::atomic<bool> pending_{false};   // a watched parameter changed since the last timer run
  VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
};

} // namespace FairyTapes::HappyEnding::Ui
