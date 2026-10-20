// Pillow Talk GUI: the panel layer. Colours and positions from the approved preview (docs/mockups/pillow_rack.html),
// the same material as The Stripper.
#include "ui/panel_view.h"

#include "ui/layout.h"

#include "fairy_ui/draw_util.h"
#include "fairy_ui/skin.h"

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"

namespace FairyTapes::PillowTalk::Ui {

using namespace VSTGUI;
using FairyTapes::Ui::kDimWhite;
using FairyTapes::Ui::kWhite;

namespace {

constexpr CColor kYellow(0xf2, 0xc2, 0x1b);

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

  FairyTapes::Ui::drawPanelBase(ctx, r, skin_);   // tint, grain tile, soft light, edge (shared, as The Stripper)

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
