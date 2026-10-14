// The Stripper GUI: the wear layer (scratches, stains, residue, specks) over the panel and its silkscreen, below the
// controls. It lets clicks through. The marks come from the seeded generator (ui/wear.h).
#pragma once

#include "ui/wear.h"

#include "vstgui/lib/cview.h"

#include <vector>

namespace FairyTapes::TheStripper::Ui {

class WearView : public VSTGUI::CView {
public:
  WearView(const VSTGUI::CRect& size, std::vector<WearMark> marks);

  void draw(VSTGUI::CDrawContext* context) override;
  CLASS_METHODS_NOCOPY(WearView, VSTGUI::CView)

private:
  std::vector<WearMark> marks_;
};

} // namespace FairyTapes::TheStripper::Ui
