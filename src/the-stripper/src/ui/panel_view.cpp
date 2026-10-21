// The Stripper GUI: static panel background. Colors and positions from the approved preview
// (src/the-stripper/docs/mockups/stripper_rack.html).
#include "ui/panel_view.h"

#include "fairy_ui/draw_util.h"
#include "ui/layout.h"

#include "fairy_ui/skin.h"

#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgradient.h"
#include "vstgui/lib/cgraphicspath.h"

#include <cmath>
#include <string>

namespace FairyTapes::TheStripper::Ui {

using FairyTapes::Ui::drawTitledBox;
using FairyTapes::Ui::kAmber;
using FairyTapes::Ui::kDimWhite;
using FairyTapes::Ui::kWhite;

using namespace VSTGUI;

namespace {



} // namespace

PanelView::PanelView(const CRect& size, const FairyTapes::Ui::Skin* skin)
: CView(size), skin_(skin) {
  titleFont_ = makeOwned<CFontDesc>("Arial", 38, kBoldFace | kItalicFace);
  smallFont_ = makeOwned<CFontDesc>("Arial", 11, kBoldFace);
  taglineFont_ = makeOwned<CFontDesc>("Arial", 13, kBoldFace);
  bandFont_ = makeOwned<CFontDesc>("Arial", 10, kBoldFace);
  scaleFont_ = makeOwned<CFontDesc>("Arial", 9, kBoldFace);
  setMouseEnabled(false);
}

void PanelView::draw(CDrawContext* ctx) {
  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
  const CRect& r = getViewSize();
  const CPoint o = r.getTopLeft();
  FairyTapes::Ui::drawPanelBackground(ctx, r, skin_);

  // Header
  ctx->setFont(titleFont_);
  ctx->setFontColor(kWhite);
  ctx->drawString("THE STRIPPER", o + CPoint(34, 58));
  ctx->setFont(smallFont_);
  ctx->setFontColor(kDimWhite);
  ctx->drawString("FAIRY TAPES", o + CPoint(36, 78));
  ctx->setFont(taglineFont_);
  ctx->setFontColor(kWhite);
  const char* tagline = "CHANNEL STRIP  \xC2\xB7  4-BAND EQ  \xC2\xB7  COMPRESSOR  \xC2\xB7  DRIVE  \xC2\xB7  LIMITER  \xC2\xB7  GATE";
  ctx->drawString(tagline, o + CPoint(966 - ctx->getStringWidth(tagline), 46));

  // Section boxes: white border with a gap for the title.
  for (const auto& b : kBoxes) {
    const CRect box(o.x + b.rect.x, o.y + b.rect.y, o.x + b.rect.x + b.rect.w, o.y + b.rect.y + b.rect.h);
    drawTitledBox(ctx, box, b.title, b.titleOnTop);
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

  // Level box silkscreen: row tags, section tags, dB scale. (The LEDs themselves are views.)
  ctx->setFont(smallFont_);
  ctx->setFontColor(kWhite);
  for (const auto& row : kMeterRows) {
    const double w = ctx->getStringWidth(row.tag);
    ctx->drawString(row.tag, o + CPoint(kMeterLabelX - w, row.cy + 4));
  }
  ctx->setFontColor(kAmber);
  struct Tag {
    const char* text;
    double y;
  };
  for (const Tag& t : {Tag{"IN", 124}, Tag{"OUT", 166}, Tag{"GR", 209}, Tag{"CLIP", 244}}) {
    const double w = ctx->getStringWidth(t.text);
    ctx->drawString(t.text, o + CPoint(464 - w / 2, t.y));
  }
  ctx->setFont(scaleFont_);
  ctx->setFontColor(kDimWhite);
  const double perLed = 66.0 / (kMeterLeds - 1);
  for (const int db : kMeterScaleDb) {
    const int index = static_cast<int>(std::lround((db + 60) / perLed));
    const std::string text = (db > 0 ? "+" : "") + std::to_string(db);
    const double w = ctx->getStringWidth(text.c_str());
    ctx->drawString(text.c_str(), o + CPoint(kMeterLeft + kMeterPitch * (index + 0.5) - w / 2, kMeterScaleY));
  }

  ctx->restoreGlobalState();
  setDirty(false);
}

} // namespace FairyTapes::TheStripper::Ui
