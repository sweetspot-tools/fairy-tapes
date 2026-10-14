// Pillow Talk GUI: the panel layer. Colours and positions from the approved preview (docs/mockups/pillow_rack.html),
// the same material as The Stripper.
#include "ui/panel_view.h"

#include "ui/layout.h"

#include "fairy_ui/draw_util.h"
#include "fairy_ui/skin.h"

#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgradient.h"
#include "vstgui/lib/cgraphicspath.h"

namespace FairyTapes::PillowTalk::Ui {

using namespace VSTGUI;
using FairyTapes::Ui::kDimWhite;
using FairyTapes::Ui::kWhite;

namespace {

constexpr CColor kTint(0x16, 0x16, 0x17);
constexpr CColor kEdge(0x2c, 0x2c, 0x2f);
constexpr CColor kYellow(0xf2, 0xc2, 0x1b);
constexpr CCoord kPanelRadius = 14;

SharedPointer<CFontDesc> bold(CCoord size, int32_t extra = 0) {
  return makeOwned<CFontDesc>("Arial", size, kBoldFace | extra);
}

} // namespace

PanelView::PanelView(const CRect& size, const FairyTapes::Ui::Skin* skin)
: CView(size), skin_(skin) {
  titleFont_ = bold(38, kItalicFace);
  brandFont_ = bold(11);
  taglineFont_ = bold(13);
  smallFont_ = bold(11);
  syncFont_ = bold(10);
  scaleFont_ = bold(9);
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
        ctx->saveGlobalState();
        ctx->setClipRect(r);
        for (CCoord y = r.top; y < r.bottom; y += th)
          for (CCoord x = r.left; x < r.right; x += tw) tile->draw(ctx, CRect(x, y, x + tw, y + th));
        ctx->restoreGlobalState();
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

  // Boxes: white border with a gap for the title on the bottom edge.
  ctx->saveGlobalState();
  for (const BoxSpec& b : kBoxes) {
    const CRect box(o.x + b.rect.x, o.y + b.rect.y, o.x + b.rect.x + b.rect.w, o.y + b.rect.y + b.rect.h);
    FairyTapes::Ui::drawTitledBox(ctx, box, b.title, false);
  }
  ctx->restoreGlobalState();

  // Thin rules over the Sync groups.
  ctx->setLineWidth(1);
  ctx->setFrameColor(CColor(kDimWhite.red, kDimWhite.green, kDimWhite.blue, 128));
  for (const PanelRule& rule : kPanelRules)
    ctx->drawLine(o + CPoint(rule.x0, rule.y + 0.5), o + CPoint(rule.x1, rule.y + 0.5));

  // Fixed texts: header, LEVEL tags and scale, small explanations, Sync group titles.
  for (const PanelText& t : kPanelTexts) {
    switch (t.style) {
    case TextStyle::Title: ctx->setFont(titleFont_); ctx->setFontColor(kWhite); break;
    case TextStyle::Brand: ctx->setFont(brandFont_); ctx->setFontColor(kDimWhite); break;
    case TextStyle::Tagline: ctx->setFont(taglineFont_); ctx->setFontColor(kWhite); break;
    case TextStyle::Section: ctx->setFont(smallFont_); ctx->setFontColor(kYellow); break;
    case TextStyle::RowTag: ctx->setFont(smallFont_); ctx->setFontColor(kWhite); break;
    case TextStyle::Scale:
    case TextStyle::Note: ctx->setFont(scaleFont_); ctx->setFontColor(kDimWhite); break;
    case TextStyle::SyncTitle: ctx->setFont(syncFont_); ctx->setFontColor(kDimWhite); break;
    }
    const CCoord w = ctx->getStringWidth(t.text);
    ctx->drawString(t.text, o + CPoint(FairyTapes::Ui::alignedLeft(t.x, w, t.align), t.y));
  }

  ctx->restoreGlobalState();
  setDirty(false);
}

} // namespace FairyTapes::PillowTalk::Ui
