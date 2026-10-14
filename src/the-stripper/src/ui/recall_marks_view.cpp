// The Stripper GUI: the recall marks. A blue ballpoint pen across the ring of scale dots.
#include "ui/recall_marks_view.h"

#include "ui/layout.h"
#include "ui/recall_marks.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"

namespace FairyTapes::TheStripper::Ui {

using namespace VSTGUI;

RecallMarksView::RecallMarksView(const CRect& size, std::uint32_t seed) : CView(size), seed_(seed) {
  setMouseEnabled(false);   // clicks go through
}

void RecallMarksView::setSeed(std::uint32_t seed) {
  seed_ = seed;
  invalid();
}

void RecallMarksView::setMarksExist(bool exist) {
  if (exist_ == exist) return;
  exist_ = exist;
  invalid();
}

void RecallMarksView::setValues(Steinberg::Vst::ParamID id, double current, double recalled) {
  if (id >= kNumParams) return;
  if (current_[id] == current && recalled_[id] == recalled) return;
  current_[id] = current;
  recalled_[id] = recalled;
  invalid();
}

void RecallMarksView::draw(CDrawContext* ctx) {
  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
  const CPoint o = getViewSize().getTopLeft();
  ctx->setFrameColor(CColor(0x2a, 0x4f, 0xa8, 215));   // ballpoint blue
  ctx->setLineWidth(1.4);
  ctx->setLineStyle(CLineStyle(CLineStyle::kLineCapRound, CLineStyle::kLineJoinRound));
  for (const auto& c : kControls) {
    if (c.kind != Kind::Knob || c.sharesView) continue;
    if (!recallMarkVisible(current_[c.id], recalled_[c.id], exist_)) continue;
    const KnobLook* look = knobLookOf(c.id);
    const double R = look ? look->radius : 19.0;
    const auto pts = recallMarkStroke(o.x + c.rect.x + c.rect.w / 2, o.y + c.rect.y + 30, R, recalled_[c.id], seed_ ^ static_cast<std::uint32_t>(c.id * 2654435761u));
    if (auto path = owned(ctx->createGraphicsPath())) {
      path->beginSubpath(CPoint(pts[0].x, pts[0].y));
      for (std::size_t i = 1; i < pts.size(); ++i) path->addLine(CPoint(pts[i].x, pts[i].y));
      ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
    }
  }
  ctx->restoreGlobalState();
  setDirty(false);
}

} // namespace FairyTapes::TheStripper::Ui
