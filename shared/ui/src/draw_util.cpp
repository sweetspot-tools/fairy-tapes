// Fairy Tapes shared UI: drawing helpers for panels and popups (first written for The Stripper).
#include "fairy_ui/draw_util.h"

#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cgraphicspath.h"

namespace FairyTapes::Ui {

using namespace VSTGUI;

namespace {

constexpr CCoord kBoxRadius = 11;
constexpr CCoord kTitleGap = 13;   // blank run on each side of a box title
constexpr CCoord kBezier = 0.5523;

// Rounded box border, open at the title: starts at the right end of the gap and goes round to the left end.
SharedPointer<CGraphicsPath> boxPath(CDrawContext* ctx, const CRect& r, bool titleOnTop, CCoord gapLeft,
                                     CCoord gapRight) {
  auto path = owned(ctx->createGraphicsPath());
  if (!path) return nullptr;
  const CCoord x0 = r.left, x1 = r.right, y0 = r.top, y1 = r.bottom, c = kBoxRadius, k = c * (1 - kBezier);
  // Points of the outline walked clockwise from the top-left corner; the gap is cut out on the title edge.
  if (titleOnTop) {
    path->beginSubpath(CPoint(gapRight, y0));
    path->addLine(CPoint(x1 - c, y0));
    path->addBezierCurve(CPoint(x1 - k, y0), CPoint(x1, y0 + k), CPoint(x1, y0 + c));
    path->addLine(CPoint(x1, y1 - c));
    path->addBezierCurve(CPoint(x1, y1 - k), CPoint(x1 - k, y1), CPoint(x1 - c, y1));
    path->addLine(CPoint(x0 + c, y1));
    path->addBezierCurve(CPoint(x0 + k, y1), CPoint(x0, y1 - k), CPoint(x0, y1 - c));
    path->addLine(CPoint(x0, y0 + c));
    path->addBezierCurve(CPoint(x0, y0 + k), CPoint(x0 + k, y0), CPoint(x0 + c, y0));
    path->addLine(CPoint(gapLeft, y0));
  } else {
    path->beginSubpath(CPoint(gapLeft, y1));
    path->addLine(CPoint(x0 + c, y1));
    path->addBezierCurve(CPoint(x0 + k, y1), CPoint(x0, y1 - k), CPoint(x0, y1 - c));
    path->addLine(CPoint(x0, y0 + c));
    path->addBezierCurve(CPoint(x0, y0 + k), CPoint(x0 + k, y0), CPoint(x0 + c, y0));
    path->addLine(CPoint(x1 - c, y0));
    path->addBezierCurve(CPoint(x1 - k, y0), CPoint(x1, y0 + k), CPoint(x1, y0 + c));
    path->addLine(CPoint(x1, y1 - c));
    path->addBezierCurve(CPoint(x1, y1 - k), CPoint(x1 - k, y1), CPoint(x1 - c, y1));
    path->addLine(CPoint(gapRight, y1));
  }
  return path;
}

} // namespace

void drawTitledBox(CDrawContext* ctx, const CRect& box, const char* title, bool titleOnTop) {
  static const SharedPointer<CFontDesc> font = makeOwned<CFontDesc>("Arial", 14, kBoldFace);
  ctx->setFrameColor(kWhite);
  ctx->setLineWidth(2);
  ctx->setLineStyle(CLineStyle(CLineStyle::kLineCapRound, CLineStyle::kLineJoinRound));
  ctx->setFont(font);
  ctx->setFontColor(kWhite);
  const CCoord cx = box.getCenter().x, tw = ctx->getStringWidth(title), half = tw / 2 + kTitleGap;
  if (auto path = boxPath(ctx, box, titleOnTop, cx - half, cx + half))
    ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
  ctx->drawString(title, CPoint(cx - tw / 2, (titleOnTop ? box.top : box.bottom) + 5));
}

} // namespace FairyTapes::Ui
