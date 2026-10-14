// The Stripper GUI: the turn of the panel.
#include "ui/flip_view.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicstransform.h"

#include <algorithm>
#include <cmath>

namespace FairyTapes::TheStripper::Ui {

using namespace VSTGUI;

FlipView::FlipView(const CRect& size, SharedPointer<CBitmap> from, SharedPointer<CBitmap> to)
: CView(size), from_(std::move(from)), to_(std::move(to)) {}

void FlipView::setProgress(double progress) {
  progress_ = (progress == progress) ? std::clamp(progress, 0.0, 1.0) : 0.0;
  invalid();
}

void FlipView::draw(CDrawContext* ctx) {
  const CRect r = getViewSize();
  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
  ctx->setFillColor(CColor(0, 0, 0, 255));
  ctx->drawRect(r, kDrawFilled);   // the table under the turning panel
  const FlipPose pose = flipPose(progress_);
  CBitmap* side = (pose.showsNewSide ? to_ : from_).get();
  if (side && pose.widthScale > 0.002) {
    const CPoint c = r.getCenter();
    CGraphicsTransform t;
    t.translate(-c.x, -c.y).scale(pose.widthScale, pose.heightScale).translate(c.x, c.y);
    CDrawContext::Transform transform(*ctx, t);
    side->draw(ctx, r);
    ctx->setFillColor(CColor(0, 0, 0, static_cast<uint8_t>(pose.shade * 255)));
    ctx->drawRect(r, kDrawFilled);
  }
  ctx->restoreGlobalState();
  setDirty(false);
}

} // namespace FairyTapes::TheStripper::Ui
