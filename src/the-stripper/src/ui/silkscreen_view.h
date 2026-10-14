// The Stripper GUI: the silkscreen layer. Everything printed on the panel next to the controls: scale dots and end
// values, names, current values (yellow), button labels, block tags. It lies below the wear (scratches, stains and
// tape cover it) and above the panel; the controls stand over both.
#pragma once

#include "params.h"
#include "ui/layout.h"

#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cview.h"

#include <array>
#include <string>

namespace FairyTapes::TheStripper::Ui {

class SilkscreenView : public VSTGUI::CView {
public:
  explicit SilkscreenView(const VSTGUI::CRect& size);

  // Current value text printed under a knob (yellow).
  void setValueText(Steinberg::Vst::ParamID id, const std::string& text);
  // Attenuates the printed parts of one control (its module is out or the global bypass is on).
  void setDimmed(Steinberg::Vst::ParamID id, bool dimmed);
  // Hides the printed parts of one control (a control that is not shown, e.g. the compressor modes in Mix bus).
  void setHidden(Steinberg::Vst::ParamID id, bool hidden);
  // The small tag at the top left of a block (what is mounted on it now).
  void setTag(Module module, const std::string& text);
  void setTagDimmed(Module module, bool dimmed);

  void draw(VSTGUI::CDrawContext* context) override;
  CLASS_METHODS_NOCOPY(SilkscreenView, VSTGUI::CView)

private:
  static constexpr int kMaxControls = 48;   // one slot per entry of kControls
  std::array<std::string, kMaxControls> values_;
  std::array<bool, kMaxControls> dimmed_{};
  std::array<bool, kMaxControls> hidden_{};
  std::array<std::string, 12> tags_;
  std::array<bool, 12> tagDimmed_{};
  VSTGUI::SharedPointer<VSTGUI::CFontDesc> endFont_, nameFont_, valueFont_, labelFont_, tagFont_;
};

// Where the tag of a block is printed (top left, inside the box), in editor coordinates (baseline).
VSTGUI::CPoint tagPosition(Module module);

} // namespace FairyTapes::TheStripper::Ui
