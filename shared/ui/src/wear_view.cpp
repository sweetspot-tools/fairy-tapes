// Fairy Tapes shared UI: the wear layer. Colours and strokes as in the approved preview (first written for The Stripper).
#include "fairy_ui/wear_view.h"

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"

#include <algorithm>
#include <cmath>

namespace FairyTapes::Ui {

using namespace VSTGUI;

namespace {

CColor colour(std::uint32_t rgb, double alpha) {
  return CColor(static_cast<uint8_t>((rgb >> 16) & 0xFF), static_cast<uint8_t>((rgb >> 8) & 0xFF),
                static_cast<uint8_t>(rgb & 0xFF), static_cast<uint8_t>(std::clamp(alpha, 0.0, 1.0) * 255.0 + 0.5));
}

// Quadratic curve from p0 through control pc to p2, as the cubic VSTGUI draws.
void quad(CGraphicsPath* path, const WearPt& p0, const WearPt& pc, const WearPt& p2) {
  path->beginSubpath(CPoint(p0.x, p0.y));
  path->addBezierCurve(CPoint(p0.x + 2.0 / 3.0 * (pc.x - p0.x), p0.y + 2.0 / 3.0 * (pc.y - p0.y)),
                       CPoint(p2.x + 2.0 / 3.0 * (pc.x - p2.x), p2.y + 2.0 / 3.0 * (pc.y - p2.y)), CPoint(p2.x, p2.y));
}

}  // namespace

WearView::WearView(const CRect& size, std::vector<WearMark> marks) : CView(size), marks_(std::move(marks)) {
  setMouseEnabled(false);   // clicks go through to what lies below
}

void WearView::setMarks(std::vector<WearMark> marks) {
  marks_ = std::move(marks);
  invalid();
}

void WearView::draw(CDrawContext* ctx) {
  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
  CRect clip;
  ctx->getClipRect(clip);
  const CPoint o = getViewSize().getTopLeft();

  for (const WearMark& m : marks_) {
    if (m.pts.empty()) continue;
    const WearBox wb = wearMarkBounds(m);
    CRect b(wb.left, wb.top, wb.right, wb.bottom);
    b.offset(o.x, o.y);
    if (!b.rectOverlap(clip)) continue;
    CGraphicsTransform shift;
    shift.translate(o.x, o.y);
    auto path = owned(ctx->createGraphicsPath());
    if (!path) continue;
    switch (m.kind) {
    case WearMark::Kind::Stain:
      path->beginSubpath(CPoint(m.pts[0].x, m.pts[0].y));
      for (std::size_t i = 1; i < m.pts.size(); ++i) path->addLine(CPoint(m.pts[i].x, m.pts[i].y));
      path->closeSubpath();
      ctx->setFillColor(colour(m.rgb, m.alpha));
      ctx->drawGraphicsPath(path, CDrawContext::kPathFilled, &shift);
      break;
    case WearMark::Kind::Speck:
      path->addEllipse(CRect(m.pts[0].x - m.width, m.pts[0].y - m.width, m.pts[0].x + m.width, m.pts[0].y + m.width));
      ctx->setFillColor(colour(m.rgb, m.alpha));
      ctx->drawGraphicsPath(path, CDrawContext::kPathFilled, &shift);
      break;
    case WearMark::Kind::Scratch: {
      if (m.pts.size() < 3) break;
      quad(path, m.pts[0], m.pts[1], m.pts[2]);
      ctx->setLineStyle(CLineStyle(CLineStyle::kLineCapButt));
      ctx->setLineWidth(m.width);
      CGraphicsTransform under = shift;
      under.translate(0.5, 0.6);
      ctx->setFrameColor(colour(0x000000, m.shadowAlpha));
      ctx->drawGraphicsPath(path, CDrawContext::kPathStroked, &under);
      ctx->setFrameColor(colour(m.rgb, m.alpha));
      ctx->drawGraphicsPath(path, CDrawContext::kPathStroked, &shift);
      break;
    }
    case WearMark::Kind::Bristle:
      if (m.pts.size() < 3) break;
      quad(path, m.pts[0], m.pts[1], m.pts[2]);
      ctx->setLineStyle(m.dashes.empty()
                            ? CLineStyle(m.roundCap ? CLineStyle::kLineCapRound : CLineStyle::kLineCapButt)
                            : CLineStyle(m.roundCap ? CLineStyle::kLineCapRound : CLineStyle::kLineCapButt,
                                         CLineStyle::kLineJoinMiter, 0.0, CLineStyle::CoordVector(m.dashes.begin(), m.dashes.end())));
      ctx->setLineWidth(m.width);
      ctx->setFrameColor(colour(m.rgb, 1.0));
      ctx->drawGraphicsPath(path, CDrawContext::kPathStroked, &shift);
      break;
    }
  }
  ctx->restoreGlobalState();
  setDirty(false);
}

} // namespace FairyTapes::Ui
