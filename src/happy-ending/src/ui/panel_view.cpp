// Happy Ending GUI: the panel layer. Colours and sizes from the preview (docs/mockups/happy_ending_rack.html), positions
// from ui/layout.h; the panel material is the suite's (fairy_ui/draw_util.h, as The Stripper).
#include "ui/panel_view.h"

#include "fairy_ui/draw_util.h"
#include "fairy_ui/silkscreen.h"
#include "fairy_ui/skin.h"

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"

#include <iterator>

namespace FairyTapes::HappyEnding::Ui {

using namespace VSTGUI;
using FairyTapes::Ui::kDimWhite;
using FairyTapes::Ui::kWhite;

namespace {

constexpr CColor kYellow(0xf2, 0xc2, 0x1b);   // LEVEL section tags, the LUFS title
constexpr CCoord kBoxRadius = 11;             // the boxes' corners (as fairy_ui drawTitledBox)
constexpr float kHeaderLineOpacity = 0.5f;

struct StyleLook {
  CCoord size;
  bool italic;
  CColor colour;
  float opacity;
};

constexpr StyleLook lookOf(TextStyle s) {
  switch (s) {
  case TextStyle::Title: return {38, true, kWhite, 1.f};
  case TextStyle::Brand: return {11, false, kDimWhite, 1.f};
  case TextStyle::Tagline: return {13, false, kWhite, 1.f};
  case TextStyle::Section: return {11, false, kYellow, 1.f};
  case TextStyle::RowTag: return {11, false, kWhite, 1.f};
  case TextStyle::Scale: return {9, false, kDimWhite, 1.f};
  case TextStyle::LufsTitle: return {10, false, kYellow, 1.f};
  case TextStyle::LufsLabel: return {8, false, kDimWhite, 1.f};
  case TextStyle::LufsHint: return {7, false, kDimWhite, 1.f};
  case TextStyle::Header: return {10, false, kDimWhite, 1.f};
  case TextStyle::Small: return {9, false, kDimWhite, 1.f};
  case TextStyle::Tag: return {kTagSize, false, kDimWhite, kTagOpacity};
  }
  return {9, false, kWhite, 1.f};
}

bool sameState(const UiState& a, const UiState& b) {
  if (a.bypass != b.bypass || a.listen != b.listen || a.set != b.set || a.finalMode != b.finalMode) return false;
  for (int i = 0; i < kNumStages; ++i)
    if (a.stageOn[i] != b.stageOn[i]) return false;
  for (int i = 0; i < kNumBands; ++i)
    if (a.bandOn[i] != b.bandOn[i]) return false;
  return true;
}

// The screen's box: the same white rounded border as the titled boxes, closed (no title, no gap).
void drawPlainBox(CDrawContext* ctx, const CRect& box) {
  if (auto path = owned(ctx->createRoundRectGraphicsPath(box, kBoxRadius))) {
    ctx->setFrameColor(kWhite);
    ctx->setLineWidth(2);
    ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
  }
}

} // namespace

PanelView::PanelView(const CRect& size, const FairyTapes::Ui::Skin* skin)
: CView(size), skin_(skin) {
  for (std::size_t i = 0; i < kNumStyles; ++i) {
    const StyleLook look = lookOf(static_cast<TextStyle>(i));
    fonts_[i] = makeOwned<CFontDesc>("Arial", look.size, kBoldFace | (look.italic ? kItalicFace : 0));
  }
  texts_.reserve(std::size(kPanelTexts));
  lines_.reserve(std::size(kHeaderLines));
  tags_.reserve(std::size(kBoxes));
  setMouseEnabled(false);
}

void PanelView::setUiState(const UiState& s) {
  if (sameState(s, state_)) return;
  state_ = s;
  invalid();
}

void PanelView::draw(CDrawContext* ctx) {
  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
  const CRect& r = getViewSize();
  const CPoint o = r.getTopLeft();

  FairyTapes::Ui::drawPanelBackground(ctx, r, skin_);   // tint, grain tile, soft light, edge (shared)

  // Boxes: white border with a gap for the title on the bottom edge; the screen's box is closed.
  ctx->saveGlobalState();
  for (const BoxSpec& b : kBoxes) {
    const CRect box(o.x + b.rect.x, o.y + b.rect.y, o.x + b.rect.x + b.rect.w, o.y + b.rect.y + b.rect.h);
    if (b.title)
      FairyTapes::Ui::drawTitledBox(ctx, box, b.title, false);
    else
      drawPlainBox(ctx, box);
  }
  ctx->restoreGlobalState();

  // Header lines under the FINE EQ band names and the VIBE EQ group names.
  lines_.clear();
  for (const HeaderLine& l : kHeaderLines) {
    const float alpha = headerLineDimmed(l, state_) ? kDimmedAlpha : 1.f;
    ctx->saveGlobalState();
    ctx->setGlobalAlpha(kHeaderLineOpacity * alpha);
    ctx->setFrameColor(kDimWhite);
    ctx->setLineWidth(1);
    ctx->setLineStyle(kLineSolid);
    ctx->drawLine(o + CPoint(l.x0, l.y), o + CPoint(l.x1, l.y));
    ctx->restoreGlobalState();
    lines_.push_back({l.x0, l.x1, l.y, alpha});
  }

  // Printed texts: header, LEVEL tags and scale, LUFS labels, band and group names, CLIP and METALHEAD notes.
  texts_.clear();
  for (const PanelText& t : kPanelTexts) {
    const StyleLook look = lookOf(t.style);
    const float alpha = look.opacity * (panelTextDimmed(t, state_) ? kDimmedAlpha : 1.f);
    ctx->saveGlobalState();
    ctx->setGlobalAlpha(alpha);
    ctx->setFont(fonts_[static_cast<std::size_t>(t.style)]);
    ctx->setFontColor(look.colour);
    const CCoord w = ctx->getStringWidth(t.text);
    const CCoord left = FairyTapes::Ui::alignedLeft(t.x, w, t.align);
    ctx->drawString(t.text, o + CPoint(left, t.y));
    ctx->restoreGlobalState();
    texts_.push_back({t.text, left, t.y, w, alpha});
  }

  // Engine tags: the external engine in the box, faint, at its top left (FINAL follows its mode).
  tags_.clear();
  const StyleLook tagLook = lookOf(TextStyle::Tag);
  for (const BoxSpec& b : kBoxes) {
    const char* tag = engineTag(b.box, state_.finalMode);
    if (!tag) continue;
    const Point p = tagPosition(b.box);
    const float alpha = kTagOpacity * (tagDimmed(b.box, state_) ? kDimmedAlpha : 1.f);
    ctx->saveGlobalState();
    ctx->setGlobalAlpha(alpha);
    ctx->setFont(fonts_[static_cast<std::size_t>(TextStyle::Tag)]);
    ctx->setFontColor(tagLook.colour);
    ctx->drawString(tag, o + CPoint(p.x, p.y));
    ctx->restoreGlobalState();
    tags_.push_back({b.box, tag, p.x, p.y, alpha});
  }

  ctx->restoreGlobalState();
  setDirty(false);
}

} // namespace FairyTapes::HappyEnding::Ui
