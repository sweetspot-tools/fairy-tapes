// The Stripper GUI: the GPU chip. Same drawing as the preview (28 x 28 units, pins on four sides, a die in the middle).
#include "ui/gpu_chip_view.h"

#include "vstgui/lib/cdrawcontext.h"

#include <algorithm>

namespace FairyTapes::TheStripper::Ui {

using namespace VSTGUI;

GpuChipView::GpuChipView(const CRect& size) : CView(size) {
  setMouseEnabled(false);   // clicks go through to the button
}

void GpuChipView::setDimmed(bool dimmed) {
  if (dimmed_ == dimmed) return;
  dimmed_ = dimmed;
  invalid();
}

void GpuChipView::draw(CDrawContext* ctx) {
  const CRect r = getViewSize();
  const CCoord k = r.getWidth() / 28.0;
  const CCoord ox = r.left, oy = r.top;
  const CColor ink(0x68, 0x68, 0x6c, dimmed_ ? 0x80 : 0xff);
  ctx->setDrawMode(kAntiAliasing);
  ctx->setFillColor(ink);
  auto rect = [&](double x, double y, double w, double h) {
    ctx->drawRect(CRect(ox + x * k, oy + y * k, ox + (x + w) * k, oy + (y + h) * k), kDrawFilled);
  };
  for (int i = 0; i < 3; ++i) {   // pins
    rect(9 + i * 4, 5, 1.4, 2.6);
    rect(9 + i * 4, 20.4, 1.4, 2.6);
    rect(5, 10 + i * 3.6, 2.6, 1.4);
    rect(20.4, 10 + i * 3.6, 2.6, 1.4);
  }
  ctx->setFrameColor(ink);
  ctx->setLineWidth(std::max(1.0, 1.3 * k));
  ctx->drawRect(CRect(ox + 7.6 * k, oy + 7.8 * k, ox + 20.4 * k, oy + 20.2 * k), kDrawStroked);   // package
  rect(11.5, 11.8, 5, 4.5);                                                                        // die
  setDirty(false);
}

} // namespace FairyTapes::TheStripper::Ui
