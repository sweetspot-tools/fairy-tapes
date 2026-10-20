// The Stripper GUI: a screw. Colours and shapes from the approved preview.
#include "ui/screw_view.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cgraphicstransform.h"

namespace FairyTapes::TheStripper::Ui {

using namespace VSTGUI;

namespace {
CRect boxAround(const ScrewSpec& s) { return CRect(s.x - 13, s.y - 13, s.x + 13, s.y + 13); }

void disc(CDrawContext* ctx, double cx, double cy, double r, const CColor& fill, const CColor* edge = nullptr, double edgeWidth = 1) {
  if (auto path = owned(ctx->createGraphicsPath())) {
    path->addEllipse(CRect(cx - r, cy - r, cx + r, cy + r));
    ctx->setFillColor(fill);
    ctx->drawGraphicsPath(path, CDrawContext::kPathFilled);
    if (edge) {
      ctx->setFrameColor(*edge);
      ctx->setLineWidth(edgeWidth);
      ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
    }
  }
}
}  // namespace

ScrewView::ScrewView(const ScrewSpec& spec, double angle)
: CView(boxAround(spec)), spec_(spec), angle_(normalizeScrewAngle(angle)) {}

void ScrewView::setScrew(const ScrewSpec& spec, double angle) {
  spec_ = spec;
  angle_ = normalizeScrewAngle(angle);
  dragging_ = false;
  invalid();
}

void ScrewView::draw(CDrawContext* ctx) {
  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
  const double cx = spec_.x, cy = spec_.y;
  if (spec_.missing) {   // a bare hole where the screw was
    const CColor edge(0x2e, 0x2e, 0x31);
    disc(ctx, cx, cy, 5.2, CColor(0x06, 0x06, 0x06), &edge, 1.2);
    disc(ctx, cx, cy + 0.8, 3.6, CColor(0, 0, 0, 230));
  } else {
    const CColor edge(0x4c, 0x4c, 0x50);
    disc(ctx, cx, cy + 1.8, 7.4, CColor(0, 0, 0, 60));   // soft shadow, two layers instead of a blur
    disc(ctx, cx, cy + 1.8, 6.5, CColor(0, 0, 0, 160));
    disc(ctx, cx, cy, 6.6, CColor(0x2b, 0x2b, 0x2e), &edge, 1);
    disc(ctx, cx, cy, 4.6, CColor(0x20, 0x20, 0x22, 179));
    CGraphicsTransform turn;
    turn.rotate(angle_, CPoint(cx, cy));
    CDrawContext::Transform t(*ctx, turn);
    const CColor ink(0x05, 0x05, 0x05);
    ctx->setFillColor(ink);
    if (!spec_.cross) {
      if (auto p = owned(ctx->createRoundRectGraphicsPath(CRect(cx - 5, cy - 1.1, cx + 5, cy + 1.1), 0.6)))
        ctx->drawGraphicsPath(p, CDrawContext::kPathFilled);
      ctx->setFrameColor(CColor(0x5c, 0x5c, 0x60, 153));   // the lit lower edge of the slot
      ctx->setLineWidth(0.5);
      ctx->drawLine(CPoint(cx - 5, cy + 1.5), CPoint(cx + 5, cy + 1.5));
    } else {
      if (auto p = owned(ctx->createRoundRectGraphicsPath(CRect(cx - 4.8, cy - 1, cx + 4.8, cy + 1), 0.6)))
        ctx->drawGraphicsPath(p, CDrawContext::kPathFilled);
      if (auto p = owned(ctx->createRoundRectGraphicsPath(CRect(cx - 1, cy - 4.8, cx + 1, cy + 4.8), 0.6)))
        ctx->drawGraphicsPath(p, CDrawContext::kPathFilled);
    }
  }
  ctx->restoreGlobalState();
  setDirty(false);
}

CMouseEventResult ScrewView::onMouseDown(CPoint& where, const CButtonState& buttons) {
  if (!buttons.isLeftButton() || spec_.missing || !screwHit(spec_.x, spec_.y, where.x, where.y)) return kMouseEventNotHandled;
  dragging_ = true;
  anchor_ = where;
  startAngle_ = angle_;
  return kMouseEventHandled;
}

CMouseEventResult ScrewView::onMouseMoved(CPoint& where, const CButtonState&) {
  if (!dragging_) return kMouseEventNotHandled;
  angle_ = screwAngleAfterDrag(startAngle_, where.x - anchor_.x, where.y - anchor_.y);
  invalid();
  if (onTurned) onTurned(angle_);
  return kMouseEventHandled;
}

CMouseEventResult ScrewView::onMouseUp(CPoint&, const CButtonState&) {
  if (!dragging_) return kMouseEventNotHandled;
  dragging_ = false;
  if (onTurned) onTurned(angle_);
  return kMouseEventHandled;
}

CMouseEventResult ScrewView::onMouseCancel() {
  if (!dragging_) return kMouseEventNotHandled;
  dragging_ = false;
  return kMouseEventHandled;
}

} // namespace FairyTapes::TheStripper::Ui
