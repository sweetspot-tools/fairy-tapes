// Fairy Tapes shared UI: the turn of the panel.
#include "fairy_ui/flip_view.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicstransform.h"
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/cviewcontainer.h"

#include <algorithm>
#include <cmath>

namespace FairyTapes::Ui {

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

// ---- PanelFlip -----------------------------------------------------------------------------------------------------
PanelFlip::PanelFlip(CViewContainer* host, CView* front, CView* back, double snapshotScale)
: host_(host), front_(front), back_(back), scale_(snapshotScale > 0.0 && std::isfinite(snapshotScale) ? snapshotScale : 1.0) {}

SharedPointer<CBitmap> PanelFlip::snapshot(CView* side) const {
  const CRect r = side->getViewSize();
  auto off = COffscreenContext::create(CPoint(r.getWidth(), r.getHeight()), scale_);
  if (!off) return nullptr;
  off->beginDraw();
  off->setClipRect(CRect(0, 0, r.getWidth(), r.getHeight()));
  side->draw(off);
  off->endDraw();
  return SharedPointer<CBitmap>(off->getBitmap());
}

bool PanelFlip::start(bool toBack) {
  if (!host_ || !front_ || !back_ || flip_ || toBack == showingBack_) return false;
  CView* from = toBack ? front_ : back_;
  CView* to = toBack ? back_ : front_;
  showingBack_ = toBack;
  SharedPointer<CBitmap> a = snapshot(from), b = snapshot(to);
  from->setVisible(false);
  if (!a || !b) {   // no offscreen drawing: swap the sides without the turn
    to->setVisible(true);
    host_->invalid();
    return true;
  }
  to->setVisible(false);
  flip_ = new FlipView(from->getViewSize(), a, b);
  start_ = std::chrono::steady_clock::now();
  host_->addView(flip_);
  return true;
}

bool PanelFlip::tick() {
  if (!flip_) return false;
  const double p = flipProgress(std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count());
  if (flipDone(p)) {
    finish();
    return false;
  }
  flip_->setProgress(p);
  return true;
}

void PanelFlip::finish() {
  if (!flip_) return;
  if (host_) host_->removeView(flip_, true);
  flip_ = nullptr;
  (showingBack_ ? back_ : front_)->setVisible(true);
  (showingBack_ ? front_ : back_)->setVisible(false);
  if (host_) host_->invalid();
}

void PanelFlip::reset() {
  host_ = nullptr;
  front_ = back_ = nullptr;
  flip_ = nullptr;
  showingBack_ = false;
}

} // namespace FairyTapes::Ui
