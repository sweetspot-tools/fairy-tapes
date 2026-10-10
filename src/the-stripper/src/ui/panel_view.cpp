// The Stripper GUI: static panel background. Colors and positions from the approved preview
// (src/the-stripper/docs/mockups/stripper_rack.html).
#include "ui/panel_view.h"

#include "ui/layout.h"

#include "fairy_ui/skin.h"

#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgradient.h"
#include "vstgui/lib/cgraphicspath.h"

namespace FairyTapes::TheStripper::Ui {

using namespace VSTGUI;

namespace {

constexpr CColor kTint(0x16, 0x16, 0x17);
constexpr CColor kEdge(0x2c, 0x2c, 0x2f);
constexpr CColor kWhite(0xf4, 0xf4, 0xf0);
constexpr CColor kDimWhite(0xc9, 0xc9, 0xc2);
constexpr char kGrainTile[] = "panel_grain.png";   // optional 256 x 256 tile (tools/gen_panel_grain.py)

constexpr CCoord kPanelRadius = 14;
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

PanelView::PanelView(const CRect& size, const FairyTapes::Ui::Skin* skin)
: CView(size), skin_(skin) {
  titleFont_ = makeOwned<CFontDesc>("Arial", 38, kBoldFace | kItalicFace);
  boxFont_ = makeOwned<CFontDesc>("Arial", 14, kBoldFace);
  smallFont_ = makeOwned<CFontDesc>("Arial", 11, kBoldFace);
  taglineFont_ = makeOwned<CFontDesc>("Arial", 13, kBoldFace);
  bandFont_ = makeOwned<CFontDesc>("Arial", 10, kBoldFace);
  setMouseEnabled(false);
}

void PanelView::draw(CDrawContext* ctx) {
  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
  const CRect& r = getViewSize();
  const CPoint o = r.getTopLeft();

  // Tint, then the optional grain tile, then the soft light (reflection at the top, shade at the bottom).
  if (auto body = owned(ctx->createRoundRectGraphicsPath(r, kPanelRadius))) {
    ctx->setFillColor(kTint);
    ctx->drawGraphicsPath(body, CDrawContext::kPathFilled);
    if (CBitmap* tile = skin_ ? skin_->bitmap(kGrainTile) : nullptr) {
      const CCoord tw = tile->getWidth(), th = tile->getHeight();
      if (tw > 0 && th > 0) {
        ctx->setClipRect(r);
        for (CCoord y = r.top; y < r.bottom; y += th)
          for (CCoord x = r.left; x < r.right; x += tw) tile->draw(ctx, CRect(x, y, x + tw, y + th));
      }
    }
    if (auto light = owned(CGradient::create(0., 1., CColor(255, 255, 255, 16), CColor(0, 0, 0, 40))))
      ctx->fillLinearGradient(body, *light, CPoint(o.x, o.y), CPoint(o.x, r.bottom));
  }
  if (auto edge = owned(ctx->createRoundRectGraphicsPath(CRect(r).inset(1, 1), kPanelRadius - 1))) {
    ctx->setFrameColor(kEdge);
    ctx->setLineWidth(2);
    ctx->drawGraphicsPath(edge, CDrawContext::kPathStroked);
  }

  // Header
  ctx->setFont(titleFont_);
  ctx->setFontColor(kWhite);
  ctx->drawString("THE STRIPPER", o + CPoint(34, 58));
  ctx->setFont(smallFont_);
  ctx->setFontColor(kDimWhite);
  ctx->drawString("FAIRY TAPES", o + CPoint(36, 78));
  ctx->setFont(taglineFont_);
  ctx->setFontColor(kWhite);
  const char* tagline = "CHANNEL STRIP  \xC2\xB7  4-BAND EQ  \xC2\xB7  COMPRESSOR  \xC2\xB7  NAM DRIVE  \xC2\xB7  LIMITER  \xC2\xB7  GATE";
  ctx->drawString(tagline, o + CPoint(966 - ctx->getStringWidth(tagline), 46));

  // Section boxes: white border with a gap for the title.
  ctx->setFrameColor(kWhite);
  ctx->setLineWidth(2);
  ctx->setLineStyle(CLineStyle(CLineStyle::kLineCapRound, CLineStyle::kLineJoinRound));
  ctx->setFont(boxFont_);
  ctx->setFontColor(kWhite);
  for (const auto& b : kBoxes) {
    const CRect br(b.rect.x, b.rect.y, b.rect.x + b.rect.w, b.rect.y + b.rect.h);
    const CRect box(br.left + o.x, br.top + o.y, br.right + o.x, br.bottom + o.y);
    const CCoord cx = box.getCenter().x, tw = ctx->getStringWidth(b.title), half = tw / 2 + kTitleGap;
    if (auto path = boxPath(ctx, box, b.titleOnTop, cx - half, cx + half))
      ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
    const CCoord edgeY = b.titleOnTop ? box.top : box.bottom;
    ctx->drawString(b.title, CPoint(cx - tw / 2, edgeY + 5));
  }

  // EQ band headers
  ctx->setFont(bandFont_);
  ctx->setFontColor(kDimWhite);
  ctx->setFrameColor(CColor(kDimWhite.red, kDimWhite.green, kDimWhite.blue, 128));
  ctx->setLineWidth(1);
  for (const auto& band : kEqBands) {
    ctx->drawLine(o + CPoint(band.left, kEqBandRuleY), o + CPoint(band.right, kEqBandRuleY));
    ctx->drawString(band.name, o + CPoint((band.left + band.right) / 2 - ctx->getStringWidth(band.name) / 2, kEqBandRuleY - 6));
  }

  ctx->restoreGlobalState();
  setDirty(false);
}

} // namespace FairyTapes::TheStripper::Ui
