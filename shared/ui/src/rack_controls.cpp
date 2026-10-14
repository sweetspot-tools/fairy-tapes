// Fairy Tapes shared UI: the rack look of the controls. Colours and geometry from the approved preview.
#include "fairy_ui/rack_controls.h"

#include "fairy_ui/knob_math.h"
#include "fairy_ui/sanitize.h"

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgradient.h"
#include "vstgui/lib/cgraphicspath.h"

#include <algorithm>
#include <cmath>

namespace FairyTapes::Ui {

using namespace VSTGUI;

namespace {

constexpr CColor kCapBase(0x23, 0x24, 0x27);
constexpr CColor kCapHighlight(0x3c, 0x3d, 0x41);
constexpr CColor kCapSkirt(0x0e, 0x0e, 0x0f);
constexpr CColor kChamfer(0x8f, 0x91, 0x96);
constexpr CColor kInlay(0xd4, 0xd1, 0xc5);
constexpr CColor kFace(0x9a, 0x9c, 0xa1);
constexpr CColor kFaceEdge(0xb8, 0xb8, 0xbb);
constexpr CColor kBellOn(0xf0, 0xb4, 0x29);
constexpr CColor kBellOff(0x8e, 0x8e, 0x91);
constexpr CColor kBellInk(0x1a, 0x1a, 0x1c);
constexpr CColor kBellInkOn(0x2a, 0x1d, 0x05);
constexpr float kDimmedAlpha = 0.4f;
constexpr double kTexture = 0.25;   // every texture layer of the cap is 25 % over the base colour (spec 4)

CRect circleRect(double cx, double cy, double r) { return CRect(cx - r, cy - r, cx + r, cy + r); }

// A path, not drawEllipse: the Cairo backend of drawEllipse scales the unit circle wrongly.
void fillCircle(CDrawContext* ctx, double cx, double cy, double r, const CColor& color) {
  if (auto path = owned(ctx->createGraphicsPath())) {
    path->addEllipse(circleRect(cx, cy, r));
    ctx->setFillColor(color);
    ctx->drawGraphicsPath(path, CDrawContext::kPathFilled);
  }
}

void strokeCircle(CDrawContext* ctx, double cx, double cy, double r, double width, const CColor& color) {
  if (auto path = owned(ctx->createGraphicsPath())) {
    path->addEllipse(circleRect(cx, cy, r));
    ctx->setFrameColor(color);
    ctx->setLineWidth(width);
    ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
  }
}

CColor withAlpha(const CColor& c, double a) {
  CColor out(c);
  out.alpha = static_cast<uint8_t>(std::clamp(a, 0.0, 1.0) * 255.0 + 0.5);
  return out;
}

void begin(CDrawContext* ctx, bool dimmed) {
  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
  if (dimmed) ctx->setGlobalAlpha(ctx->getGlobalAlpha() * kDimmedAlpha);
}

CColor ledOn(RackLed led) { return led == RackLed::Yellow ? CColor(0xf2, 0xc2, 0x1b) : CColor(0xe8, 0x39, 0x2b); }
CColor ledOff(RackLed led) { return led == RackLed::Yellow ? CColor(0x3a, 0x33, 0x14) : CColor(0x3a, 0x10, 0x10); }

// One button: shadow, grey face (sinks while lit), edge lights, LED under it.
void drawFace(CDrawContext* ctx, const CRect& face, bool on, RackLed led, bool withLed) {
  const double sink = on ? Rack::kPressDepth : 0.0;
  // soft shadow under a raised part; shorter and tighter when pressed
  CRect shadow(face);
  shadow.offset(0, on ? 1.5 : 4.0);
  if (auto path = owned(ctx->createRoundRectGraphicsPath(shadow, 2))) {
    ctx->setFillColor(CColor(0, 0, 0, on ? 140 : 215));
    ctx->drawGraphicsPath(path, CDrawContext::kPathFilled);
  }
  CRect f(face);
  f.offset(0, sink);
  if (auto path = owned(ctx->createRoundRectGraphicsPath(f, 2))) {
    ctx->setFillColor(kFace);
    ctx->drawGraphicsPath(path, CDrawContext::kPathFilled);
    ctx->setFrameColor(kFaceEdge);
    ctx->setLineWidth(1);
    ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
  }
  // worn edges: a light line top left, a dark one bottom right
  CRect hi(f);
  hi.inset(0.8, 0.8);
  hi.offset(-0.3, -0.4);
  if (auto path = owned(ctx->createRoundRectGraphicsPath(hi, 1.8))) {
    ctx->setFrameColor(CColor(255, 255, 255, 72));
    ctx->setLineWidth(1);
    ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
  }
  CRect lo(f);
  lo.inset(0.5, 0.5);
  lo.offset(0.7, 0.9);
  if (auto path = owned(ctx->createRoundRectGraphicsPath(lo, 1.8))) {
    ctx->setFrameColor(CColor(0, 0, 0, 102));
    ctx->setLineWidth(1.2);
    ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
  }
  if (withLed) {
    const CPoint c = f.getCenter();
    const double ly = face.getCenter().y + Rack::kLedOffsetY;
    fillCircle(ctx, c.x, ly, Rack::kLedRadius, on ? ledOn(led) : ledOff(led));
    strokeCircle(ctx, c.x, ly, Rack::kLedRadius, 0.6, CColor(0, 0, 0));
  }
}

// Face at the top of `r`, centred horizontally, standard size.
CRect faceIn(const CRect& r) {
  const CCoord x = r.left + (r.getWidth() - Rack::kFaceWidth) / 2;
  return CRect(x, r.top, x + Rack::kFaceWidth, r.top + Rack::kFaceHeight);
}

void drawBellIcon(CDrawContext* ctx, const CPoint& c, bool on) {
  if (auto path = owned(ctx->createGraphicsPath())) {
    // a shelf that rises into a bump and falls back: the bell curve of a bell filter
    path->beginSubpath(CPoint(c.x - 9, c.y + 4));
    path->addLine(CPoint(c.x - 5, c.y + 4));
    path->addBezierCurve(CPoint(c.x - 2, c.y + 4), CPoint(c.x - 3, c.y - 5), CPoint(c.x, c.y - 5));
    path->addBezierCurve(CPoint(c.x + 3, c.y - 5), CPoint(c.x + 2, c.y + 4), CPoint(c.x + 5, c.y + 4));
    path->addLine(CPoint(c.x + 9, c.y + 4));
    ctx->setFrameColor(on ? kBellInkOn : kBellInk);
    ctx->setLineWidth(1.8);
    ctx->setLineStyle(CLineStyle(CLineStyle::kLineCapRound, CLineStyle::kLineJoinRound));
    ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
  }
}


// Draws the marks of one control at `origin`, at 25 % opacity (the material shows through). `underPass` selects the marks
// below the cap/face (dirt around it) or the ones over it. Pointer marks are the knob's business and are skipped here.
void drawWearMarks(CDrawContext* ctx, const std::vector<ControlWearMark>& marks, const CPoint& origin, bool underPass) {
  for (const auto& m : marks) {
    if (m.underCap != underPass || m.kind == ControlWearMark::Kind::Pointer) continue;
    auto path = owned(ctx->createGraphicsPath());
    if (!path) continue;
    const CColor colour(static_cast<uint8_t>((m.rgb >> 16) & 0xFF), static_cast<uint8_t>((m.rgb >> 8) & 0xFF),
                        static_cast<uint8_t>(m.rgb & 0xFF),
                        static_cast<uint8_t>(std::clamp(m.alpha * kTexture, 0.0, 1.0) * 255.0 + 0.5));
    switch (m.kind) {
    case ControlWearMark::Kind::Blob:
    case ControlWearMark::Kind::Chip:
      path->beginSubpath(CPoint(origin.x + m.pts[0].first, origin.y + m.pts[0].second));
      for (std::size_t i = 1; i < m.pts.size(); ++i) path->addLine(CPoint(origin.x + m.pts[i].first, origin.y + m.pts[i].second));
      path->closeSubpath();
      ctx->setFillColor(colour);
      ctx->drawGraphicsPath(path, CDrawContext::kPathFilled);
      break;
    case ControlWearMark::Kind::Speck:
      path->addEllipse(circleRect(origin.x + m.pts[0].first, origin.y + m.pts[0].second, m.size));
      ctx->setFillColor(colour);
      ctx->drawGraphicsPath(path, CDrawContext::kPathFilled);
      break;
    case ControlWearMark::Kind::Stroke: {
      const double x0 = origin.x + m.pts[0].first, y0 = origin.y + m.pts[0].second;
      const double xc = origin.x + m.pts[1].first, yc = origin.y + m.pts[1].second;
      const double x2 = origin.x + m.pts[2].first, y2 = origin.y + m.pts[2].second;
      path->beginSubpath(CPoint(x0, y0));
      path->addBezierCurve(CPoint(x0 + 2.0 / 3.0 * (xc - x0), y0 + 2.0 / 3.0 * (yc - y0)),
                           CPoint(x2 + 2.0 / 3.0 * (xc - x2), y2 + 2.0 / 3.0 * (yc - y2)), CPoint(x2, y2));
      ctx->setFrameColor(colour);
      ctx->setLineWidth(m.size);
      ctx->setLineStyle(CLineStyle(CLineStyle::kLineCapButt));
      ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
      break;
    }
    case ControlWearMark::Kind::Pointer: break;
    }
  }
}

} // namespace

// ---------------------------------------------------------------------------
// RackKnob

RackKnob::RackKnob(const CRect& size, IControlListener* listener, int32_t tag, double capRadius, const Skin* skin)
    : AnalogKnob(size, listener, tag, std::string(), skin), capRadius_(capRadius > 4 ? capRadius : Rack::kDefaultCapRadius) {}

void RackKnob::setWear(std::uint32_t seed, double amount) {
  wear_ = knobWear(seed, amount, capRadius_);
  invalid();
}

void RackKnob::draw(CDrawContext* ctx) {
  begin(ctx, dimmed_);
  const double v = sanitizeNorm(getValue(), getDefaultValue());
  const CRect& r = getViewSize();
  const double cx = r.left + r.getWidth() / 2, cy = r.top + AnalogKnob::kCenterOffsetY, R = capRadius_;
  const double Zf = std::max(0.35, R / 56.0);   // the preview scales its fine marks with the cap

  drawWearMarks(ctx, wear_, CPoint(cx, cy), true);   // dirt around the base, below the cap
  // contact shadow: three soft layers instead of a blur
  fillCircle(ctx, cx + R * 0.05, cy + R * 0.16, R * 1.12, CColor(0, 0, 0, 46));
  fillCircle(ctx, cx + R * 0.05, cy + R * 0.16, R * 1.06, CColor(0, 0, 0, 64));
  fillCircle(ctx, cx + R * 0.05, cy + R * 0.16, R * 1.02, CColor(0, 0, 0, 100));
  // skirt and cap
  fillCircle(ctx, cx, cy, R * 1.1, kCapSkirt);
  strokeCircle(ctx, cx - 0.6, cy - 0.7, R * 1.1, 1.1, withAlpha(kCapHighlight, 0.55));
  fillCircle(ctx, cx, cy, R, kCapBase);

  // turning marks (very fine concentric lines) and the chamfer, at 25 %
  const int rings = static_cast<int>(std::lround(36 * std::max(0.5, R / 56.0)));
  for (int q = 1; q <= rings; ++q)
    strokeCircle(ctx, cx, cy, R * q / rings * 0.97, std::max(0.35, 0.5 * Zf),
                 q % 2 ? withAlpha(CColor(255, 255, 255), 0.06 * 1.5) : withAlpha(CColor(0, 0, 0), 0.12 * 1.5));
  strokeCircle(ctx, cx - 0.5, cy - 0.6, R * 0.93, 2.2 * Zf, withAlpha(kChamfer, 0.45 * kTexture * 2));

  // light from the top left: a radial sheen over the cap
  GradientColorStopMap stops;
  stops.emplace(0.0, CColor(255, 255, 255, static_cast<uint8_t>(0.13 * 0.7 * 255)));
  stops.emplace(0.45, CColor(255, 255, 255, static_cast<uint8_t>(0.02 * 0.7 * 255)));
  stops.emplace(1.0, CColor(0, 0, 0, static_cast<uint8_t>(0.38 * 0.7 * 255)));
  if (auto sheen = owned(CGradient::create(stops))) {
    if (auto path = owned(ctx->createGraphicsPath())) {
      path->addEllipse(circleRect(cx, cy, R));
      const double rad = R * 2 * 0.85;
      ctx->fillRadialGradient(path, *sheen, CPoint(cx - R + 2 * R * 0.33, cy - R + 2 * R * 0.28), rad,
                              CPoint(0, 0));
    }
  }
  strokeCircle(ctx, cx - 0.5, cy - 0.6, R - 0.5, 0.8 * Zf, CColor(255, 255, 255, 20));
  strokeCircle(ctx, cx + 0.5, cy + 0.7, R - 0.3, 1.0 * Zf, CColor(0, 0, 0, 153));

  drawWearMarks(ctx, wear_, CPoint(cx, cy), false);   // worn edge, dents, residue and specks on the cap

  // pointer: a groove with a cream inlay, rotated with the value
  const double rad = Rack::pointerDegrees(v) * Rack::kPi / 180.0;
  const double dx = std::cos(rad), dy = -std::sin(rad);
  ctx->setLineStyle(CLineStyle(CLineStyle::kLineCapRound));
  ctx->setLineWidth(4.6 * Zf);
  ctx->setFrameColor(CColor(0, 0, 0, 71));
  ctx->drawLine(CPoint(cx + dx * R * 0.3, cy + dy * R * 0.3), CPoint(cx + dx * R * 0.86, cy + dy * R * 0.86));
  ctx->setLineStyle(CLineStyle(CLineStyle::kLineCapButt));
  ctx->setLineWidth(3.2 * Zf);
  ctx->setFrameColor(kInlay);
  ctx->drawLine(CPoint(cx + dx * R * 0.3, cy + dy * R * 0.3), CPoint(cx + dx * R * 0.86, cy + dy * R * 0.86));
  // the inlay worn through in places (the cap showing through the cream) and nicked
  ctx->setLineStyle(CLineStyle(CLineStyle::kLineCapButt));
  for (const auto& m : wear_) {
    if (m.kind != ControlWearMark::Kind::Pointer) continue;
    const double t0 = 0.3 + 0.56 * m.size, t1 = 0.3 + 0.56 * (m.size + m.length);
    ctx->setFrameColor(CColor(static_cast<uint8_t>((m.rgb >> 16) & 0xFF), static_cast<uint8_t>((m.rgb >> 8) & 0xFF),
                              static_cast<uint8_t>(m.rgb & 0xFF), static_cast<uint8_t>(m.alpha * 255.0 + 0.5)));
    ctx->drawLine(CPoint(cx + dx * R * t0, cy + dy * R * t0), CPoint(cx + dx * R * t1, cy + dy * R * t1));
  }

  ctx->restoreGlobalState();
  setDirty(false);
}

// ---------------------------------------------------------------------------
// RackToggle

RackToggle::RackToggle(const CRect& size, IControlListener* listener, int32_t tag, RackLed led, bool bellIcon,
                       const Skin* skin)
    : ToggleButton(size, listener, tag, std::string(), skin), led_(led), bell_(bellIcon) {}

void RackToggle::setWear(std::uint32_t seed, double amount) {
  wear_ = bell_ ? std::vector<ControlWearMark>() : buttonWear(seed, amount, Rack::kFaceWidth, Rack::kFaceHeight);
  invalid();
}

void RackToggle::draw(CDrawContext* ctx) {
  begin(ctx, dimmed_);
  const bool on = getValue() >= 0.5f;
  const CRect& r = getViewSize();
  if (bell_) {
    // amber face when on, no LED; the icon follows the face
    const double sink = on ? Rack::kPressDepth : 0.0;
    CRect shadow(r);
    shadow.offset(0, on ? 1.5 : 4.0);
    if (auto path = owned(ctx->createRoundRectGraphicsPath(shadow, 2))) {
      ctx->setFillColor(CColor(0, 0, 0, on ? 140 : 215));
      ctx->drawGraphicsPath(path, CDrawContext::kPathFilled);
    }
    CRect f(r);
    f.offset(0, sink);
    if (auto path = owned(ctx->createRoundRectGraphicsPath(f, 2))) {
      ctx->setFillColor(on ? kBellOn : kBellOff);
      ctx->drawGraphicsPath(path, CDrawContext::kPathFilled);
      ctx->setFrameColor(kFaceEdge);
      ctx->setLineWidth(1);
      ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
    }
    drawBellIcon(ctx, f.getCenter(), on);
  } else {
    const CRect face = faceIn(r);
    drawWearMarks(ctx, wear_, face.getTopLeft(), true);                        // dirt in the gap, below the face
    drawFace(ctx, face, on, led_, true);
    drawWearMarks(ctx, wear_, CPoint(face.left, face.top + (on ? Rack::kPressDepth : 0.0)), false);   // follows the sunk face
  }
  ctx->restoreGlobalState();
  setDirty(false);
}

// ---------------------------------------------------------------------------
// RackChoice

RackChoice::RackChoice(const CRect& size, IControlListener* listener, int32_t tag, int count, RackLed led,
                       const Skin* skin)
    : ChoiceButtons(size, listener, tag, std::vector<std::string>(static_cast<size_t>(std::max(count, 1))), skin),
      led_(led), count_(std::max(count, 1)) {}

CRect RackChoice::slot(int index) const {
  const CRect& r = getViewSize();
  const CCoord w = r.getWidth() / count_;
  return CRect(r.left + index * w, r.top, r.left + (index + 1) * w, r.bottom);
}

void RackChoice::setWear(std::uint32_t seed, double amount) {
  wear_.clear();
  for (int i = 0; i < count_; ++i)
    wear_.push_back(buttonWear(seed + static_cast<std::uint32_t>(i) * 7919u, amount, Rack::kFaceWidth, Rack::kFaceHeight));
  invalid();
}

void RackChoice::draw(CDrawContext* ctx) {
  begin(ctx, dimmed_);
  const int selected = selectedIndex();
  for (int i = 0; i < count_; ++i) {
    const CRect face = faceIn(slot(i));
    const bool on = i == selected;
    const bool hasWear = static_cast<size_t>(i) < wear_.size();
    if (hasWear) drawWearMarks(ctx, wear_[static_cast<size_t>(i)], face.getTopLeft(), true);
    drawFace(ctx, face, on, led_, true);
    if (hasWear) drawWearMarks(ctx, wear_[static_cast<size_t>(i)], CPoint(face.left, face.top + (on ? Rack::kPressDepth : 0.0)), false);
  }
  ctx->restoreGlobalState();
  setDirty(false);
}

CMouseEventResult RackChoice::onMouseDown(CPoint& where, const CButtonState& buttons) {
  if (!buttons.isLeftButton()) return kMouseEventNotHandled;
  for (int i = 0; i < count_; ++i) {
    if (slot(i).pointInside(where)) {
      if (i != selectedIndex()) editTo(static_cast<float>(normFromChoiceIndex(i, count_)));
      break;
    }
  }
  return kMouseDownEventHandledButDontNeedMovedOrUpEvents;
}

} // namespace FairyTapes::Ui
