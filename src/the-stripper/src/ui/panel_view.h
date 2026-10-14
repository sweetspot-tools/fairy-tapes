// The Stripper GUI: static panel background (tint, grain, light gradient, section boxes, header).
#pragma once

#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cview.h"

namespace FairyTapes::Ui {
class Skin;
}

namespace FairyTapes::TheStripper::Ui {

// The panel itself without any text: tint, grain tile (when the skin has it), soft light, edge. Shared by the front and the back.
void drawPanelBase(VSTGUI::CDrawContext* context, const VSTGUI::CRect& rect, const FairyTapes::Ui::Skin* skin);

class PanelView : public VSTGUI::CView {
public:
  PanelView(const VSTGUI::CRect& size, const FairyTapes::Ui::Skin* skin);
  void draw(VSTGUI::CDrawContext* context) override;

private:
  const FairyTapes::Ui::Skin* skin_;
  VSTGUI::SharedPointer<VSTGUI::CFontDesc> titleFont_, smallFont_, taglineFont_, bandFont_, scaleFont_;
};

} // namespace FairyTapes::TheStripper::Ui
