// Pillow Talk GUI: static editor background (panel, title, section rules, row labels).
#pragma once

#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cview.h"

namespace FairyTapes::Ui {
class Skin;
}

namespace FairyTapes::PillowTalk::Ui {

class PanelView : public VSTGUI::CView {
public:
  // The skin's panel bitmap, when present, replaces the code-drawn panel body and border.
  PanelView(const VSTGUI::CRect& size, const FairyTapes::Ui::Skin* skin);

  void draw(VSTGUI::CDrawContext* context) override;

private:
  const FairyTapes::Ui::Skin* skin_;
  VSTGUI::SharedPointer<VSTGUI::CFontDesc> titleFont_;
};

} // namespace FairyTapes::PillowTalk::Ui
