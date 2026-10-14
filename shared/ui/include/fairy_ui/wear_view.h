// Fairy Tapes shared UI: the wear layer (scratches, stains, residue, specks) over the panel and its silkscreen, below
// the controls (docs/DESIGN_SYSTEM.md, layer 4). It lets clicks through. The marks come from the seeded generator
// (fairy_ui/wear.h), in the view's own coordinates.
#pragma once

#include "fairy_ui/wear.h"

#include "vstgui/lib/cview.h"

#include <vector>

namespace FairyTapes::Ui {

class WearView : public VSTGUI::CView {
public:
  WearView(const VSTGUI::CRect& size, std::vector<WearMark> marks);

  // New marks (e.g. the Wear preference changed); redraws.
  void setMarks(std::vector<WearMark> marks);
  const std::vector<WearMark>& marks() const { return marks_; }

  void draw(VSTGUI::CDrawContext* context) override;
  CLASS_METHODS_NOCOPY(WearView, VSTGUI::CView)

private:
  std::vector<WearMark> marks_;
};

} // namespace FairyTapes::Ui
