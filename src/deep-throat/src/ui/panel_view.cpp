// Deep Throat GUI: the panel layer. Colours and sizes from the approved preview (docs/mockups/deep_throat_rack.html),
// positions from ui/layout.h; the panel material is the suite's (fairy_ui/draw_util.h, as The Stripper).
#include "ui/panel_view.h"

#include "fairy_ui/draw_util.h"
#include "fairy_ui/silkscreen.h"
#include "fairy_ui/skin.h"

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cdrawcontext.h"

#include <iterator>

namespace FairyTapes::DeepThroat::Ui {

using namespace VSTGUI;
using FairyTapes::Ui::kAmber;
using FairyTapes::Ui::kDimWhite;
using FairyTapes::Ui::kWhite;

namespace {

constexpr CColor kYellow(0xf2, 0xc2, 0x1b);   // LEVEL section tags
constexpr CColor kRed(0xe8, 0x39, 0x2b);      // SCOPE legend: breath band

struct StyleLook {
  CCoord size;
  bool italic;
  CColor colour;
};

constexpr StyleLook lookOf(TextStyle s) {
  switch (s) {
  case TextStyle::Title: return {38, true, kWhite};
  case TextStyle::Brand: return {11, false, kDimWhite};
  case TextStyle::Tagline: return {13, false, kWhite};
  case TextStyle::Section: return {11, false, kYellow};
  case TextStyle::RowTag: return {11, false, kWhite};
  case TextStyle::Scale: return {9, false, kDimWhite};
  case TextStyle::Label12: return {12, false, kWhite};
  case TextStyle::Label11: return {11, false, kWhite};
  case TextStyle::Label10: return {10, false, kWhite};
  case TextStyle::Small: return {9, false, kDimWhite};
  case TextStyle::LegendVoice: return {9, false, kWhite};
  case TextStyle::LegendRider: return {9, false, kAmber};
  case TextStyle::LegendBreath: return {9, false, kRed};
  }
  return {9, false, kWhite};
}

bool sameDim(const DimState& a, const DimState& b) {
  if (a.bypass != b.bypass) return false;
  for (int i = 0; i < 6; ++i)
    if (a.in[i] != b.in[i]) return false;
  return true;
}

} // namespace

PanelView::PanelView(const CRect& size, const FairyTapes::Ui::Skin* skin)
: CView(size), skin_(skin) {
  for (std::size_t i = 0; i < kNumStyles; ++i) {
    const StyleLook look = lookOf(static_cast<TextStyle>(i));
    fonts_[i] = makeOwned<CFontDesc>("Arial", look.size, kBoldFace | (look.italic ? kItalicFace : 0));
  }
  drawn_.reserve(std::size(kPanelTexts));
  setMouseEnabled(false);
}

void PanelView::setDimmed(bool bypass) {
  DimState s = dim_;
  s.bypass = bypass;
  setDimState(s);
}

void PanelView::setDimState(const DimState& s) {
  if (sameDim(s, dim_)) return;
  dim_ = s;
  invalid();
}

bool PanelView::textDimmed(std::size_t index) const {
  return index < std::size(kPanelTexts) && panelTextDimmed(kPanelTexts[index], dim_);
}

void PanelView::draw(CDrawContext* ctx) {
  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
  const CRect& r = getViewSize();
  const CPoint o = r.getTopLeft();

  FairyTapes::Ui::drawPanelBackground(ctx, r, skin_);   // tint, grain tile, soft light, edge (shared)

  // Boxes: white border with a gap for the title on the bottom edge.
  ctx->saveGlobalState();
  for (const BoxSpec& b : kBoxes) {
    const CRect box(o.x + b.rect.x, o.y + b.rect.y, o.x + b.rect.x + b.rect.w, o.y + b.rect.y + b.rect.h);
    FairyTapes::Ui::drawTitledBox(ctx, box, b.title, false);
  }
  ctx->restoreGlobalState();

  // Printed texts: header, SCOPE legend, LEVEL tags and scales, module labels.
  drawn_.clear();
  for (std::size_t i = 0; i < std::size(kPanelTexts); ++i) {
    const PanelText& t = kPanelTexts[i];
    const StyleLook look = lookOf(t.style);
    const float alpha = textDimmed(i) ? kDimmedAlpha : 1.f;
    ctx->saveGlobalState();
    ctx->setGlobalAlpha(alpha);
    ctx->setFont(fonts_[static_cast<std::size_t>(t.style)]);
    ctx->setFontColor(look.colour);
    const CCoord w = ctx->getStringWidth(t.text);
    const CCoord left = FairyTapes::Ui::alignedLeft(t.x, w, t.align);
    ctx->drawString(t.text, o + CPoint(left, t.y));
    ctx->restoreGlobalState();
    drawn_.push_back({t.text, left, t.y, w, alpha});
  }

  ctx->restoreGlobalState();
  setDirty(false);
}

} // namespace FairyTapes::DeepThroat::Ui
