// Fairy Tapes shared UI: the silkscreen layer. Positions from fairy_ui/silkscreen.h, colours and fonts from the approved
// preview (first written for The Stripper).
#include "fairy_ui/silkscreen_view.h"

#include "fairy_ui/draw_util.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"

namespace FairyTapes::Ui {

using namespace VSTGUI;

namespace {
SharedPointer<CFontDesc> bold(CCoord size) { return makeOwned<CFontDesc>("Arial", size, kBoldFace); }
}  // namespace

SilkscreenView::SilkscreenView(const CRect& size, std::vector<PrintedItem> items, std::vector<BlockTag> tags)
    : CView(size), model_(std::move(items), std::move(tags)) {
  endFont_ = bold(kSilkEndSize);
  nameFont_ = bold(kSilkNameSize);
  valueFont_ = bold(kSilkValueSize);
  labelFont_ = bold(kSilkLabelSize);
  tagFont_ = bold(kSilkTagSize);
  setMouseEnabled(false);
}

void SilkscreenView::setValueText(int id, const std::string& text) { changed(model_.setValueText(id, text)); }
void SilkscreenView::setDimmed(int id, bool dimmed) { changed(model_.setDimmed(id, dimmed)); }
void SilkscreenView::setHidden(int id, bool hidden) { changed(model_.setHidden(id, hidden)); }
void SilkscreenView::setTag(int key, const std::string& text) { changed(model_.setTag(key, text)); }
void SilkscreenView::setTagDimmed(int key, bool dimmed) { changed(model_.setTagDimmed(key, dimmed)); }

void SilkscreenView::draw(CDrawContext* ctx) {
  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
  const CPoint o = getViewSize().getTopLeft();

  for (std::size_t i = 0; i < model_.size(); ++i) {
    if (model_.hidden(i)) continue;
    const PrintedParts parts = model_.parts(i);
    ctx->saveGlobalState();
    if (model_.dimmed(i)) ctx->setGlobalAlpha(ctx->getGlobalAlpha() * kSilkDimmedAlpha);
    if (!parts.dots.empty()) {
      ctx->setFillColor(kWhite);
      for (const PrintDot& d : parts.dots) {
        if (auto path = owned(ctx->createGraphicsPath())) {
          path->addEllipse(CRect(o.x + d.x - d.r, o.y + d.y - d.r, o.x + d.x + d.r, o.y + d.y + d.r));
          ctx->drawGraphicsPath(path, CDrawContext::kPathFilled);
        }
      }
    }
    for (const PrintText& t : parts.texts) {
      switch (t.role) {
      case TextRole::EndValue: ctx->setFont(endFont_); break;
      case TextRole::Name: ctx->setFont(nameFont_); break;
      case TextRole::Value: ctx->setFont(valueFont_); break;
      case TextRole::Label: ctx->setFont(labelFont_); break;
      }
      ctx->setFontColor(t.role == TextRole::Value ? kAmber : kWhite);
      const CCoord w = ctx->getStringWidth(t.text.c_str());
      ctx->drawString(t.text.c_str(), CPoint(o.x + alignedLeft(t.x, w, t.align), o.y + t.y));
    }
    ctx->restoreGlobalState();
  }

  // block tags: tiny and very faint
  ctx->setFont(tagFont_);
  for (const SilkscreenModel::Tag& t : model_.tags()) {
    if (t.text.empty()) continue;
    ctx->setFontColor(CColor(kDimWhite.red, kDimWhite.green, kDimWhite.blue,
                             static_cast<uint8_t>(255 * kSilkTagOpacity * (t.dimmed ? kSilkDimmedAlpha : 1.0f))));
    ctx->drawString(t.text.c_str(), CPoint(o.x + t.slot.x, o.y + t.slot.y));
  }
  ctx->restoreGlobalState();
  setDirty(false);
}

} // namespace FairyTapes::Ui
