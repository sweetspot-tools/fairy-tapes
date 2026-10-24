// Hotline GUI: the panel layer, drawn in code (docs/DESIGN_SYSTEM.md, layers 1-2): #161617 tint, the shared grain
// tile (shared/ui/assets/panel_grain.png in the bundle's Resources; plain tint when it is missing), soft light, edge,
// the boxes with their titles, and the fixed texts of ui/layout.h. Clicks go through.
#pragma once

#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cview.h"

namespace FairyTapes::Ui {
class Skin;
}

namespace FairyTapes::Hotline::Ui {

class PanelView : public VSTGUI::CView {
public:
  PanelView(const VSTGUI::CRect& size, const FairyTapes::Ui::Skin* skin);

  void draw(VSTGUI::CDrawContext* context) override;

private:
  const FairyTapes::Ui::Skin* skin_;
  VSTGUI::SharedPointer<VSTGUI::CFontDesc> titleFont_, brandFont_, taglineFont_, smallFont_, scaleFont_;
};

} // namespace FairyTapes::Hotline::Ui
