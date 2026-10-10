// The Stripper GUI: static panel background (tint, grain, light gradient, section boxes, header).
#pragma once

#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cview.h"

namespace FairyTapes::Ui {
class Skin;
}

namespace FairyTapes::TheStripper::Ui {

class PanelView : public VSTGUI::CView {
public:
  PanelView(const VSTGUI::CRect& size, const FairyTapes::Ui::Skin* skin);
  void draw(VSTGUI::CDrawContext* context) override;

private:
  const FairyTapes::Ui::Skin* skin_;
  VSTGUI::SharedPointer<VSTGUI::CFontDesc> titleFont_, boxFont_, smallFont_, taglineFont_, bandFont_, scaleFont_;
};

} // namespace FairyTapes::TheStripper::Ui
