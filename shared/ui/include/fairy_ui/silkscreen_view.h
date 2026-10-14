// Fairy Tapes shared UI: the silkscreen layer (docs/DESIGN_SYSTEM.md, layer 3). Everything printed on the panel next to
// the controls: scale dots and end values, names, current values (yellow), button labels, block tags. It lies below
// the wear (scratches, stains and tape cover it) and above the panel; the controls stand over both. Clicks go through.
// What is printed where comes from the plug-in's list of items (fairy_ui/silkscreen.h).
#pragma once

#include "fairy_ui/silkscreen.h"

#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cview.h"

#include <string>
#include <vector>

namespace FairyTapes::Ui {

class SilkscreenView : public VSTGUI::CView {
public:
  SilkscreenView(const VSTGUI::CRect& size, std::vector<PrintedItem> items, std::vector<BlockTag> tags = {});

  // Current value text printed under a knob (yellow); every item with this id.
  void setValueText(int id, const std::string& text);
  // Attenuates the printed parts of a control (its module is out or the global bypass is on).
  void setDimmed(int id, bool dimmed);
  // Hides the printed parts of a control (a control that is not shown).
  void setHidden(int id, bool hidden);
  // The small, very faint tag at the top left of a block (what is mounted on it now).
  void setTag(int key, const std::string& text);
  void setTagDimmed(int key, bool dimmed);

  const SilkscreenModel& model() const { return model_; }

  void draw(VSTGUI::CDrawContext* context) override;
  CLASS_METHODS_NOCOPY(SilkscreenView, VSTGUI::CView)

private:
  void changed(bool c) {
    if (c) invalid();
  }

  SilkscreenModel model_;
  VSTGUI::SharedPointer<VSTGUI::CFontDesc> endFont_, nameFont_, valueFont_, labelFont_, tagFont_;
};

} // namespace FairyTapes::Ui
