// Happy Ending GUI: the plugin editor (1280 x 890 rack panel, views built in code, no .uidesc).
// Layers from the bottom (docs/DESIGN_SYSTEM.md): panel (PanelView: body, grain, boxes with their titles on the bottom
// border, the screen's untitled box, header lines, printed texts, engine tags); the silkscreen, wear, paper tapes, screws,
// screen, meters and controls come with the later tasks of the editor plan (docs/superpowers/plans/
// 2026-10-31-happy-ending-editor.md, Tasks 9-14).
// Window: 1280 x 890 logical px times the host's content scale factor (IPlugViewContentScaleSupport) times the SIZE of
// this plug-in's preferences (ui/prefs.h, HappyEnding.prefs, key size=75|90|100|125), read when the view is made and
// again when it opens; the frame is zoomed by the same factor, so the views keep their logical coordinates.
// checkSizeConstraint accepts only that size (no free resizing). A new host factor while the views are shown asks the
// host for the new window (IPlugFrame::resizeView).
// The UI timer reads the controller's parameters (BYPASS, the stage and band INs, LISTEN, SET, FINAL mode) and gives the
// panel its UiState (ui/layout.h): the dimming of the printed parts and the FINAL tag.
// Controller::decor() has seed 0 until the processor, a state or an edit gives one: the seeded parts (wear, tapes,
// screws, Task 12) wait for a decor revision with a seed; the panel layer does not depend on it.
#pragma once

#include "params.h"
#include "ui/editor_size.h"
#include "ui/layout.h"
#include "ui/prefs.h"

#include "fairy_ui/skin.h"

#include "public.sdk/source/vst/vstguieditor.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "vstgui/lib/controls/icontrollistener.h"
#include "vstgui/lib/cvstguitimer.h"

#include <array>
#include <filesystem>

namespace VSTGUI {
class CViewContainer;
}

namespace FairyTapes::Ui {
class NormalizedControl;
class SilkscreenView;
}

namespace FairyTapes::HappyEnding::Ui {

class PanelView;
struct EditorPreview;   // offscreen render tool (tools/render_editor.cpp)
struct EditorTest;      // headless editor tests (tests/test_editor.cpp)

class Editor : public Steinberg::Vst::VSTGUIEditor,
               public Steinberg::IPlugViewContentScaleSupport,
               public VSTGUI::IControlListener {
public:
  // Logical size; the window is this times the host's content scale factor times SIZE.
  static constexpr int kWidth = kEditorWidth;
  static constexpr int kHeight = kEditorHeight;

  // Reads SIZE from the user's preferences (a missing or bad file: 100 %), so the host's first getSize is right.
  explicit Editor(Steinberg::Vst::EditController* controller);
  ~Editor() override;

  // VSTGUIEditor
  bool PLUGIN_API open(void* parent, const VSTGUI::PlatformType& platformType) override;
  void PLUGIN_API close() override;
  // Edits are reported by the control listener below, not through the frame.
  void beginEdit(int32_t) override {}
  void endEdit(int32_t) override {}

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

  // IControlListener (the controls come with Task 9).
  void valueChanged(VSTGUI::CControl* control) override;

private:
  friend struct EditorPreview;
  friend struct EditorTest;

  // Builds the layers in `parent` (the frame in open(); a plain container in the render tool and the tests).
  void createViews(VSTGUI::CViewContainer* parent);
  void resetViewPointers();
  // The UI timer's work: the controller's state to the views; also safe with no views.
  void onTimer();
  // The UiState of the controller's parameters (ui/layout.h uiStateFrom) to the panel.
  void refreshUiState();

  // Reads the preferences in `file` (any problem: the defaults) and applies them: for now SIZE (the window).
  void loadAppearance(const std::filesystem::path& file);
  // The window for the host factor and SIZE: the reported size (setRect), the frame's zoom and, while the views are
  // shown, a request to the host (IPlugFrame::resizeView).
  void applyWindowSize();
  PixelSize windowSize() const { return scaledEditorSize(scale_, prefs_.size); }

  double scale_ = 1.0;   // sanitized content scale factor of the host
  Prefs prefs_;          // the user preferences as last read (SIZE in prefs_.size)
  FairyTapes::Ui::Skin skin_;
  VSTGUI::CViewContainer* root_ = nullptr;   // the container the views were created in (shown while not null)
  PanelView* panel_ = nullptr;               // the views are owned by the frame (or that container)
  FairyTapes::Ui::SilkscreenView* silk_ = nullptr;                          // Task 9
  std::array<FairyTapes::Ui::NormalizedControl*, kNumControls> controls_{};   // kControls order; Task 9
  VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
};

} // namespace FairyTapes::HappyEnding::Ui
