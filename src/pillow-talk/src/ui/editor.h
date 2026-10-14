// Pillow Talk GUI: the plugin editor (1000 x 628 rack panel, views built in code, no .uidesc).
// Layers from the bottom (docs/DESIGN_SYSTEM.md): panel (PanelView) / silkscreen (SilkscreenView) / wear (WearView) /
// controls, scope, meters and clip LEDs. Every persistent parameter is bound to one control (ui/layout.h). User gestures
// go to the controller as beginEdit / performEdit / endEdit; host-side changes (automation, presets, undo) arrive as
// Parameter dependents and are applied on the UI timer, which also reads the processor's display parameters.
#pragma once

#include "params.h"
#include "ui/editor_size.h"
#include "ui/layout.h"

#include "fairy_ui/skin.h"
#include "fairy_ui/wear.h"

#include "public.sdk/source/vst/vstguieditor.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "vstgui/lib/controls/icontrollistener.h"
#include "vstgui/lib/cvstguitimer.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace VSTGUI {
class CViewContainer;
}

namespace FairyTapes::Ui {
class NormalizedControl;
class MeterBar;
class ClipLed;
class SilkscreenView;
class WearView;
}

namespace FairyTapes::PillowTalk::Ui {

class DuckScope;
struct EditorPreview;   // offscreen render tool (tools/render_editor.cpp)

class Editor : public Steinberg::Vst::VSTGUIEditor,
               public Steinberg::IPlugViewContentScaleSupport,
               public VSTGUI::IControlListener {
public:
  // Logical size; the window is this times the host's content scale factor.
  static constexpr int kWidth = kEditorWidth;
  static constexpr int kHeight = kEditorHeight;

  // Wear until the seed and the preference exist (Task 5: state v2 and user preferences).
  static constexpr uint32_t kWearSeed = FairyTapes::Ui::kDefaultWearSeed;
  static constexpr FairyTapes::Ui::WearLevel kWearLevel = FairyTapes::Ui::WearLevel::Light;

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
  friend struct EditorPreview;

  void createViews(VSTGUI::CViewContainer* parent);
  void watchParameters();
  void unwatchParameters();
  void onTimer();
  void updateDisplays();
  void clearClips();
  // Copies the controller value into the control and applies the side effects (value text, dimming, scope).
  void syncFromController(Steinberg::Vst::ParamID id);
  void applySideEffects(Steinberg::Vst::ParamID id, double norm);
  void refreshDimming();
  // Marks for a seed and level, regenerated only when one of them changes.
  const std::vector<FairyTapes::Ui::WearMark>& wearMarks(uint32_t seed, FairyTapes::Ui::WearLevel level);
  void resetViewPointers();

  double scale_ = 1.0;  // sanitized content scale factor
  FairyTapes::Ui::Skin skin_;
  std::array<FairyTapes::Ui::NormalizedControl*, kNumParams> controls_{};  // views are owned by the frame
  DuckScope* scope_ = nullptr;
  FairyTapes::Ui::SilkscreenView* silk_ = nullptr;
  FairyTapes::Ui::WearView* wear_ = nullptr;
  std::array<FairyTapes::Ui::MeterBar*, kMeterRowCount> bars_{};
  std::array<FairyTapes::Ui::ClipLed*, kClipCount> clips_{};
  DimState dim_{};

  std::vector<FairyTapes::Ui::WearMark> marks_;
  uint32_t marksSeed_ = 0;
  FairyTapes::Ui::WearLevel marksLevel_ = FairyTapes::Ui::WearLevel::Off;
  bool marksValid_ = false;

  std::array<Steinberg::IPtr<Steinberg::Vst::Parameter>, kNumParams> watched_{};
  std::atomic<uint32_t> pending_{0};  // one bit per ParamID changed by the host
  VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
};

} // namespace FairyTapes::PillowTalk::Ui
