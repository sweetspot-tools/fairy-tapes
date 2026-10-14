// The Stripper GUI: the silkscreen layer. Positions and sizes from the approved preview.
#include "ui/silkscreen_view.h"

#include "ui/draw_util.h"

#include "fairy_ui/rack_geometry.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"

#include <cstddef>

namespace FairyTapes::TheStripper::Ui {

using namespace VSTGUI;
namespace Rack = FairyTapes::Ui::Rack;

static_assert(std::size(kControls) <= 48, "SilkscreenView::kMaxControls must cover kControls");

namespace {
constexpr float kDimmedAlpha = 0.35f;
constexpr double kTagOpacity = 0.10;   // very faint on purpose (spec 4)

SharedPointer<CFontDesc> bold(CCoord size) { return makeOwned<CFontDesc>("Arial", size, kBoldFace); }

void dot(CDrawContext* ctx, double x, double y, double r) {
  if (auto path = owned(ctx->createGraphicsPath())) {
    path->addEllipse(CRect(x - r, y - r, x + r, y + r));
    ctx->setFillColor(kWhite);
    ctx->drawGraphicsPath(path, CDrawContext::kPathFilled);
  }
}

void centred(CDrawContext* ctx, const char* text, double cx, double baseline) {
  ctx->drawString(text, CPoint(cx - ctx->getStringWidth(text) / 2, baseline));
}
}  // namespace

CPoint tagPosition(Module m) {
  switch (m) {
  case Module::Filters: return CPoint(196, 114);
  case Module::Eq: return CPoint(40, 302);
  case Module::Comp: return CPoint(40, 474);
  case Module::Drive: return CPoint(418, 474);
  case Module::Limiter: return CPoint(724, 474);
  case Module::Gate: return CPoint(862, 474);
  default: return CPoint(0, 0);
  }
}

SilkscreenView::SilkscreenView(const CRect& size) : CView(size) {
  endFont_ = bold(8);
  nameFont_ = bold(12);
  valueFont_ = bold(10);
  labelFont_ = bold(11);
  tagFont_ = bold(7);
  setMouseEnabled(false);
}

void SilkscreenView::setValueText(Steinberg::Vst::ParamID id, const std::string& text) {
  for (std::size_t i = 0; i < std::size(kControls) && i < kMaxControls; ++i) {
    if (kControls[i].id != id || kControls[i].sharesView) continue;
    if (values_[i] == text) return;
    values_[i] = text;
    invalid();
    return;
  }
}

void SilkscreenView::setDimmed(Steinberg::Vst::ParamID id, bool dimmed) {
  for (std::size_t i = 0; i < std::size(kControls) && i < kMaxControls; ++i) {
    if (kControls[i].id != id) continue;
    if (dimmed_[i] != dimmed) {
      dimmed_[i] = dimmed;
      invalid();
    }
    return;
  }
}

void SilkscreenView::setHidden(Steinberg::Vst::ParamID id, bool hidden) {
  for (std::size_t i = 0; i < std::size(kControls) && i < kMaxControls; ++i) {
    if (kControls[i].id != id) continue;
    if (hidden_[i] != hidden) {
      hidden_[i] = hidden;
      invalid();
    }
    return;
  }
}

void SilkscreenView::setTag(Module module, const std::string& text) {
  auto& t = tags_[static_cast<std::size_t>(module)];
  if (t == text) return;
  t = text;
  invalid();
}

void SilkscreenView::setTagDimmed(Module module, bool dimmed) {
  auto& d = tagDimmed_[static_cast<std::size_t>(module)];
  if (d == dimmed) return;
  d = dimmed;
  invalid();
}

void SilkscreenView::draw(CDrawContext* ctx) {
  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
  const CPoint o = getViewSize().getTopLeft();

  for (std::size_t i = 0; i < std::size(kControls) && i < kMaxControls; ++i) {
    const ControlSpec& c = kControls[i];
    if (c.sharesView || hidden_[i]) continue;
    ctx->saveGlobalState();
    if (dimmed_[i]) ctx->setGlobalAlpha(ctx->getGlobalAlpha() * kDimmedAlpha);
    const double cx = o.x + c.rect.x + c.rect.w / 2;
    if (c.kind == Kind::Knob) {
      const KnobLook* look = knobLookOf(c.id);
      const double R = look ? look->radius : Rack::kDefaultCapRadius;
      const double cy = o.y + c.rect.y + 30;   // the cap centre (AnalogKnob::kCenterOffsetY)
      for (int d = 0; d < Rack::kScaleDots; ++d) {
        const Rack::Pt p = Rack::scaleDot(cx, cy, R, d);
        dot(ctx, p.x, p.y, Rack::scaleDotRadius(d));
      }
      ctx->setFontColor(kWhite);
      if (look) {
        ctx->setFont(endFont_);
        const Rack::Pt lo = Rack::endLabelLow(cx, cy, R), hi = Rack::endLabelHigh(cx, cy, R);
        ctx->drawString(look->lo, CPoint(lo.x - ctx->getStringWidth(look->lo), lo.y));
        ctx->drawString(look->hi, CPoint(hi.x, hi.y));
      }
      ctx->setFont(nameFont_);
      if (c.label) centred(ctx, c.label, cx, Rack::nameBaseline(cy, R));
      if (!values_[i].empty()) {
        ctx->setFont(valueFont_);
        ctx->setFontColor(kAmber);
        centred(ctx, values_[i].c_str(), cx, Rack::valueBaseline(cy, R));
      }
    } else if (c.kind == Kind::Toggle) {
      if (c.label && !isBellButton(c.id)) {
        ctx->setFont(labelFont_);
        ctx->setFontColor(kWhite);
        centred(ctx, c.label, cx, o.y + c.rect.y + Rack::kFaceHeight / 2 + Rack::kLabelOffsetY);
      }
    } else {  // Choice: one label above each button
      int n = 0;
      while (n < 3 && c.choices[n]) ++n;
      ctx->setFont(labelFont_);
      ctx->setFontColor(kWhite);
      for (int k = 0; k < n; ++k) {
        const double sx = o.x + c.rect.x + c.rect.w * (k + 0.5) / n;
        centred(ctx, c.choices[k], sx, o.y + c.rect.y + Rack::kFaceHeight / 2 + Rack::kLabelOffsetY);
      }
    }
    ctx->restoreGlobalState();
  }

  // block tags: tiny and very faint
  ctx->setFont(tagFont_);
  for (std::size_t m = 0; m < tags_.size(); ++m) {
    if (tags_[m].empty()) continue;
    const CPoint p = tagPosition(static_cast<Module>(m));
    ctx->setFontColor(CColor(kDimWhite.red, kDimWhite.green, kDimWhite.blue,
                             static_cast<uint8_t>(255 * kTagOpacity * (tagDimmed_[m] ? kDimmedAlpha : 1.0f))));
    ctx->drawString(tags_[m].c_str(), CPoint(o.x + p.x, o.y + p.y));
  }
  ctx->restoreGlobalState();
  setDirty(false);
}

} // namespace FairyTapes::TheStripper::Ui
