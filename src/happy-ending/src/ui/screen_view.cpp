// Happy Ending GUI: the shared screen (ui/screen_view.h). Positions, colours and widths from the preview
// (docs/mockups/happy_ending_rack.html: redraw, vibeRedraw, viewTick), the shapes from the pure model (ui/screen_model.h).
#include "ui/screen_view.h"

#include "dsp/matched_biquad.h"
#include "engine.h"
#include "ui/value_text.h"

#include "fairy_ui/draw_util.h"

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/clinestyle.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace FairyTapes::HappyEnding::Ui {

using namespace VSTGUI;

namespace {

constexpr CColor kScreenFill(0x07, 0x07, 0x08), kScreenBorder(0x3a, 0x3a, 0x3d);
constexpr CColor kGridLine(0x23, 0x23, 0x26), kZeroLine(0x3a, 0x3a, 0x3e), kFrameLine(0x2a, 0x2a, 0x2d);
constexpr CColor kLevelGrey(0x8e, 0x8e, 0x93);
constexpr CColor kYellow(0xf2, 0xc2, 0x1b), kSideBlue(0x7f, 0xc0, 0xff);   // the preview's YEL and SIDEC
constexpr float kDimmedAlpha = 0.35f;
constexpr CCoord kScreenRadius = 4, kBorderWidth = 1.2;
constexpr double kDotRadius = 3.6;
constexpr double kBandPassFloorDb = 30.0;   // the LISTEN zone: 0 dB at the top of the screen, -30 dB at the bottom

static_assert(kHsGain + 1 == kVibeOn && kDynAir + 1 == kGlueOn, "FINE EQ then VIBE EQ are contiguous in the table");
static_assert(kFinalDrive == kFinalOn + 4, "FINAL's parameters are contiguous in the table");

CColor withOpacity(CColor c, double opacity) {
  c.alpha = static_cast<uint8_t>(std::lround(255.0 * std::clamp(opacity, 0.0, 1.0)));
  return c;
}

bool feedsFine(ParamID id) { return id >= kFineOn && id <= kHsGain; }
bool feedsVibe(ParamID id) { return id >= kVibeOn && id <= kDynAir; }
bool feedsClip(ParamID id) { return id == kClipOn || id == kClipDrive || id == kLoudness; }
bool feedsFinal(ParamID id) { return (id >= kFinalOn && id <= kFinalDrive) || id == kLoudness || id == kCeiling; }

double sane(double v, double lo, double hi, double fallback) {
  return std::isnan(v) ? fallback : std::clamp(v, lo, hi);
}

// The reduction after the MIX knob: the share `mix` of the compressed signal on the dry one.
double netReductionDb(double grDb, double mix) {
  const double wet = std::pow(10.0, -std::max(grDb, 0.0) / 20.0);
  return std::max(0.0, -20.0 * std::log10((1.0 - mix) + mix * wet));
}

double tableValue(ParamID id, int index) {
  const ValueTable t = valueTable(id);
  return t.data[std::clamp(index, 0, static_cast<int>(t.size) - 1)];
}

// ---- Drawing helpers ------------------------------------------------------------------------------------------------
// The drawing context with the view's offset: the model's points are in the panel's layout coordinates.
struct Canvas {
  CDrawContext* ctx;
  double ox, oy;

  CPoint at(double x, double y) const { return CPoint(x + ox, y + oy); }
  CPoint at(const Point& p) const { return at(p.x, p.y); }

  // Dash lengths are in units of the line width.
  void style(const CColor& color, double width, double dashOn = 0.0, double dashOff = 0.0) const {
    ctx->setFrameColor(color);
    ctx->setLineWidth(width);
    if (dashOn > 0.0)
      ctx->setLineStyle(CLineStyle(CLineStyle::kLineCapButt, CLineStyle::kLineJoinMiter, 0, {dashOn / width, dashOff / width}));
    else
      ctx->setLineStyle(kLineSolid);
  }
  void stroke(const std::vector<Point>& pts, const CColor& color, double width, double dashOn = 0.0, double dashOff = 0.0) const {
    if (pts.size() < 2) return;
    auto path = owned(ctx->createGraphicsPath());
    if (!path) return;
    path->beginSubpath(at(pts[0]));
    for (std::size_t i = 1; i < pts.size(); ++i) path->addLine(at(pts[i]));
    style(color, width, dashOn, dashOff);
    ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
    ctx->setLineStyle(kLineSolid);
  }
  void fill(const std::vector<Point>& pts, const CColor& color) const {
    if (pts.size() < 3) return;
    auto path = owned(ctx->createGraphicsPath());
    if (!path) return;
    path->beginSubpath(at(pts[0]));
    for (std::size_t i = 1; i < pts.size(); ++i) path->addLine(at(pts[i]));
    path->closeSubpath();
    ctx->setFillColor(color);
    ctx->drawGraphicsPath(path, CDrawContext::kPathFilled);
  }
  void hline(double y, double x0, double x1, const CColor& color, double width, double dashOn = 0.0, double dashOff = 0.0) const {
    style(color, width, dashOn, dashOff);
    ctx->drawLine(at(x0, y), at(x1, y));
    ctx->setLineStyle(kLineSolid);
  }
  void vline(double x, double y0, double y1, const CColor& color, double width) const {
    style(color, width);
    ctx->drawLine(at(x, y0), at(x, y1));
  }
  // A ring with a dark inside (a path, not drawEllipse: the Cairo backend of drawEllipse scales the unit circle wrongly).
  void ring(double cx, double cy, double radius, const CColor& inside, const CColor& rim, double width) const {
    auto path = owned(ctx->createGraphicsPath());
    if (!path) return;
    const CPoint c = at(cx, cy);
    path->addEllipse(CRect(c.x - radius, c.y - radius, c.x + radius, c.y + radius));
    ctx->setFillColor(inside);
    ctx->drawGraphicsPath(path, CDrawContext::kPathFilled);
    style(rim, width);
    ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
  }
};

// The curve and the area down (or up) to the line y = `base`: the points plus the two corners on the line.
std::vector<Point> closedTo(const std::vector<Point>& curve, float base) {
  std::vector<Point> out(curve);
  if (!curve.empty()) {
    out.push_back({curve.back().x, base});
    out.push_back({curve.front().x, base});
  }
  return out;
}

struct LegendItem {
  const char* text;
  CColor color;
  double dashOn = 0.0, dashOff = 0.0;
};

// The legend of the preview: a 14 px line sample and the name, 9 px bold, from the left of the screen at its top.
void drawLegend(const Canvas& c, std::initializer_list<LegendItem> items, double y) {
  static const SharedPointer<CFontDesc> font = makeOwned<CFontDesc>("Arial", 9, kBoldFace);
  double lx = kScreenRect.x + 10;
  c.ctx->setFont(font);
  for (const LegendItem& it : items) {
    c.hline(y, lx, lx + 14, it.color, 2, it.dashOn, it.dashOff);
    c.ctx->setFontColor(it.color);
    c.ctx->drawString(it.text, c.at(lx + 19, y + 3.5));
    lx += 24 + c.ctx->getStringWidth(it.text) + 12;
  }
}

constexpr double kLegendY = 12.0;   // from the top of the screen (the preview's SY + 12)

// ---- The LISTEN zone ------------------------------------------------------------------------------------------------
// The response of the band-pass that solos a bell, drawn the way the preview does: 0 dB at the top of the screen,
// kBandPassFloorDb below it at the bottom.
std::vector<Point> listenCurve(const double* norm, int bell, std::string& text) {
  struct BellIds {
    ParamID freq, gain, q;
    const char* name;
  };
  static constexpr BellIds kBells[3] = {{kLmFreq, kLmGain, kLmQ, "LO MID"}, {kMidFreq, kMidGain, kMidQ, "MID"}, {kHmFreq, kHmGain, kHmQ, "HI MID"}};
  std::vector<Point> out;
  text.clear();
  if (bell < 0 || bell > 2) return out;
  const EngineSettings s = mapParams(norm, -1, false);
  const DSP::BandSettings& band = s.bands[static_cast<std::size_t>(2 + bell)];
  const BellIds& ids = kBells[bell];
  const double f0 = tableValue(ids.freq, band.freqIndex);
  const double q = DSP::effectiveQ(tableValue(ids.q, band.qIndex), tableValue(ids.gain, band.gainIndex));
  DSP::BiquadCoeffs c;
  DSP::matched::computeBandPass(c, f0, EqCurves::kSampleRate, q);
  out.reserve(Scale::kEqPoints);
  for (int i = 0; i < Scale::kEqPoints; ++i) {
    const double hz = Scale::kEqMinHz * std::pow(Scale::kEqMaxHz / Scale::kEqMinHz, static_cast<double>(i) / (Scale::kEqPoints - 1));
    const double db = c.magnitudeDb(hz, EqCurves::kSampleRate);
    const double y = eqY(-Scale::kEqRangeDb + 2.0 * Scale::kEqRangeDb * std::max(0.0, 1.0 + db / kBandPassFloorDb));
    out.push_back({eqX(hz), static_cast<float>(std::clamp(y, static_cast<double>(kScreenRect.y), static_cast<double>(kScreenRect.y + kScreenRect.h)))});
  }
  text = std::string("LISTEN  \xC2\xB7  ") + ids.name + "  " + detail::freqText(f0);
  if (band.ms != 0) text += std::string("  \xC2\xB7  ") + detail::msSigla(band.ms);
  return out;
}

} // namespace

// ---- The view -------------------------------------------------------------------------------------------------------
ScreenDisplayView::ScreenDisplayView()
: CView(CRect(kScreenRect.x, kScreenRect.y, kScreenRect.x + kScreenRect.w, kScreenRect.y + kScreenRect.h)) {
  setMouseEnabled(false);   // clicks go through
  loadDefaults(norm_.data());
  const EngineSettings s = mapParams(norm_.data(), -1, false);
  glueThreshDb_ = s.glue.threshDb;
  glueMix_ = s.glue.mix;
  ceilingDb_ = std::min<double>(s.ceilingDb, 0.0);
  ceilingLin_ = ceilingLinear(norm_.data());
}

void ScreenDisplayView::setView(ScreenView view) {
  if (view == view_) return;
  view_ = view;
  dotStale_ = true;
  invalid();
}

ScrollPoint ScreenDisplayView::newest(ScreenView v) const {
  const History& h = history(v);
  return h.size() ? h.at(h.size() - 1) : ScrollPoint{};
}

void ScreenDisplayView::setParams(const double* norm, const UiState& state) {
  bool changed = false;
  for (int i = 0; i < static_cast<int>(kNumParams); ++i) {
    const auto id = static_cast<ParamID>(i);
    const double v = sane(norm[id], 0.0, 1.0, norm_[id]);
    if (v == norm_[id]) continue;
    norm_[id] = v;
    changed = true;
    if (feedsFine(id) || feedsVibe(id)) curvesStale_ = true;
    if (feedsFine(id)) fineStale_ = true;
    if (feedsVibe(id)) vibeStaticStale_ = vibeLiveStale_ = true;
    if (feedsClip(id)) clipStale_ = dotStale_ = true;
    if (feedsFinal(id)) finalStale_ = dotStale_ = true;
  }
  if (changed) {
    const EngineSettings s = mapParams(norm_.data(), -1, false);
    glueThreshDb_ = s.glue.threshDb;
    glueMix_ = s.glue.mix;
    ceilingDb_ = std::min<double>(s.ceilingDb, 0.0);
    ceilingLin_ = ceilingLinear(norm_.data());
  }
  const int heard = listenedBand(state);
  const int bell = heard >= 2 ? heard - 2 : -1;
  if (bell != listenBell_) {
    listenBell_ = bell;
    fineStale_ = true;
    changed = true;
  }
  if (state.bypass != dimmed_ || state.stageOn[static_cast<int>(Stage::Limit)] != state_.stageOn[static_cast<int>(Stage::Limit)] ||
      state.stageOn[static_cast<int>(Stage::Vibe)] != state_.stageOn[static_cast<int>(Stage::Vibe)])
    changed = true;
  state_ = state;
  dimmed_ = state.bypass;
  if (changed) invalid();
}

void ScreenDisplayView::tick(const ScreenFrame& in) {
  ScreenFrame f;
  f.glueInDb = sane(in.glueInDb, -120.0, 24.0, -120.0);
  f.glueGrDb = sane(in.glueGrDb, 0.0, 60.0, 0.0);
  f.limitInDb = sane(in.limitInDb, -120.0, 24.0, -120.0);
  f.limitGrDb = sane(in.limitGrDb, 0.0, 60.0, 0.0);
  f.limitThreshDb = sane(in.limitThreshDb, -60.0, 24.0, 0.0);
  f.clipInDb = sane(in.clipInDb, -120.0, 24.0, -120.0);
  f.finalInDb = sane(in.finalInDb, -120.0, 24.0, -120.0);
  f.dynLow = sane(in.dynLow, 0.0, 1.0, 0.0);
  f.dynAir = sane(in.dynAir, 0.0, 1.0, 0.0);

  const bool pullChanged = f.dynLow != frame_.dynLow || f.dynAir != frame_.dynAir;
  if (pullChanged) vibeLiveStale_ = true;
  const bool dotMoved = (view_ == ScreenView::Clip && f.clipInDb != frame_.clipInDb) || (view_ == ScreenView::Final && f.finalInDb != frame_.finalInDb);
  frame_ = f;

  // Both histories grow whatever view is shown. A stage that is out keeps no reduction (what the engine publishes then is
  // not a live value: GLUE and LIMIT follow their own state), and LIMIT's line is the ceiling.
  const bool glueOn = state_.stageOn[static_cast<int>(Stage::Glue)], limitOn = state_.stageOn[static_cast<int>(Stage::Limit)];
  ScrollPoint g;
  g.levelDb = static_cast<float>(f.glueInDb);
  g.grDb = static_cast<float>(glueOn ? f.glueGrDb : 0.0);
  g.netGrDb = static_cast<float>(netReductionDb(g.grDb, glueMix_));
  g.threshDb = static_cast<float>(glueThreshDb_);
  glueHistory_.push(g);
  ScrollPoint l;
  l.levelDb = static_cast<float>(f.limitInDb);
  l.grDb = static_cast<float>(limitOn ? f.limitGrDb : 0.0);
  l.threshDb = static_cast<float>(ceilingDb_);
  limitHistory_.push(l);

  if (view_ == ScreenView::Glue || view_ == ScreenView::Limit || dotMoved || (view_ == ScreenView::Eq && pullChanged)) invalid();
}

// ---- Cached shapes --------------------------------------------------------------------------------------------------
void ScreenDisplayView::refreshEq() {
  if (curvesStale_) {
    curves_.update(norm_.data());
    curvesStale_ = false;
  }
  msActive_ = curves_.msActive();
  if (fineStale_) {
    fineSt_.clear();
    fineMid_.clear();
    fineSide_.clear();
    if (msActive_) {
      fineMid_ = curvePath(curves_, EqTrace::FineMid);
      fineSide_ = curvePath(curves_, EqTrace::FineSide);
    } else {
      fineSt_ = curvePath(curves_, EqTrace::FineSt);
    }
    listenZone_ = listenCurve(norm_.data(), listenBell_, listenText_);
    fineStale_ = false;
    ++recomputes_;
  }
  if (state_.stageOn[static_cast<int>(Stage::Vibe)]) {
    if (vibeStaticStale_) {
      vibeStatic_ = curvePath(curves_, EqTrace::VibeStatic);
      vibeStaticStale_ = false;
      ++recomputes_;
    }
    if (vibeLiveStale_) {
      vibeLive_ = curvePath(curves_, EqTrace::Vibe, static_cast<float>(frame_.dynLow), static_cast<float>(frame_.dynAir));
      vibeLiveStale_ = false;
      ++recomputes_;
    }
  }
}

void ScreenDisplayView::refreshDot() {
  const double inDb = view_ == ScreenView::Clip ? frame_.clipInDb : frame_.finalInDb;
  if (!dotStale_ && inDb == dotInDb_ && dotView_ == view_) return;
  const double lin = std::pow(10.0, inDb / 20.0);
  if (view_ == ScreenView::Clip)
    dot_ = {clipX(lin), clipY(std::fabs(clipTransfer(lin, norm_.data())))};
  else
    dot_ = {finalX(lin), finalY(std::fabs(finalTransfer(lin, norm_.data(), ceilingLin_)))};
  dotInDb_ = inDb;
  dotView_ = view_;
  dotStale_ = false;
}

void ScreenDisplayView::refreshShapes() {
  switch (view_) {
    case ScreenView::Eq: refreshEq(); break;
    case ScreenView::Clip:
      if (clipStale_) {
        clipCurve_ = curvePath(ScreenView::Clip, norm_.data(), ceilingLin_);
        clipStale_ = false;
        ++recomputes_;
      }
      refreshDot();
      break;
    case ScreenView::Final:
      if (finalStale_) {
        finalCurve_ = curvePath(ScreenView::Final, norm_.data(), ceilingLin_);
        finalStale_ = false;
        ++recomputes_;
      }
      refreshDot();
      break;
    case ScreenView::Glue:
    case ScreenView::Limit: break;
  }
}

// ---- Drawing --------------------------------------------------------------------------------------------------------
void ScreenDisplayView::draw(CDrawContext* ctx) {
  listenZoneDrawn_ = limitThreshDrawn_ = vibeDashedDrawn_ = false;
  refreshShapes();

  const CRect& r = getViewSize();
  const Canvas c{ctx, r.left - kScreenRect.x, r.top - kScreenRect.y};
  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
  CRect box(r);
  box.inset(kBorderWidth / 2, kBorderWidth / 2);
  if (auto path = owned(ctx->createRoundRectGraphicsPath(box, kScreenRadius))) {
    ctx->setFillColor(kScreenFill);
    ctx->drawGraphicsPath(path, CDrawContext::kPathFilled);
  }

  ctx->saveGlobalState();   // the content: clipped to the screen, dimmed with BYPASS
  ctx->setClipRect(box);
  if (dimmed_) ctx->setGlobalAlpha(kDimmedAlpha);
  switch (view_) {
    case ScreenView::Eq: drawEq(ctx); break;
    case ScreenView::Glue:
    case ScreenView::Limit: drawHistory(ctx, view_); break;
    case ScreenView::Clip:
    case ScreenView::Final: drawTransfer(ctx, view_); break;
  }
  ctx->restoreGlobalState();

  if (auto path = owned(ctx->createRoundRectGraphicsPath(box, kScreenRadius))) {   // the border over the content's edge
    c.style(kScreenBorder, kBorderWidth);
    ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
  }
  ctx->restoreGlobalState();
  setDirty(false);
}

void ScreenDisplayView::drawEq(CDrawContext* ctx) {
  ++drawCount_[static_cast<std::size_t>(ScreenView::Eq)];
  static const SharedPointer<CFontDesc> axisFont = makeOwned<CFontDesc>("Arial", 8, kBoldFace);
  static const SharedPointer<CFontDesc> listenFont = makeOwned<CFontDesc>("Arial", 10, kBoldFace);
  const CRect& r = getViewSize();
  const Canvas c{ctx, r.left - kScreenRect.x, r.top - kScreenRect.y};
  const double x0 = kScreenRect.x, x1 = kScreenRect.x + kScreenRect.w, y0 = kScreenRect.y, y1 = kScreenRect.y + kScreenRect.h;

  // grid: the frequencies, then the dB lines (the 0 dB line a little brighter), the labels at 55 %
  struct Tick {
    double hz;
    const char* label;
  };
  static constexpr Tick kFreqs[] = {{30, "30"},   {50, "50"},   {100, "100"},  {200, "200"}, {500, "500"},
                                    {1000, "1k"}, {2000, "2k"}, {5000, "5k"}, {10000, "10k"}};
  ctx->setFont(axisFont);
  ctx->setFontColor(withOpacity(FairyTapes::Ui::kDimWhite, 0.55));
  for (const Tick& t : kFreqs) {
    const double x = eqX(t.hz);
    c.vline(x, y0, y1, kGridLine, 1);
    ctx->drawString(t.label, c.at(x + 3, y1 - 5));
  }
  static constexpr struct {
    int db;
    const char* label;
  } kLevels[] = {{-6, "-6"}, {-3, "-3"}, {0, nullptr}, {3, "+3"}, {6, "+6"}};
  for (const auto& d : kLevels) {
    const double y = eqY(d.db);
    c.hline(y, x0, x1, d.db == 0 ? kZeroLine : kGridLine, 1);
    if (!d.label) continue;
    ctx->drawString(d.label, c.at(x1 - 6 - ctx->getStringWidth(d.label), y - 3));
  }

  // LISTEN: the zone the bell lets through, behind the curves (which fade while it is on)
  const bool listening = listenBell_ >= 0 && !listenZone_.empty();
  if (listening) {
    c.fill(closedTo(listenZone_, static_cast<float>(y1)), withOpacity(FairyTapes::Ui::kAmber, 0.18));
    c.stroke(listenZone_, withOpacity(FairyTapes::Ui::kAmber, 0.8), 1.4);
    ctx->setFont(listenFont);
    ctx->setFontColor(FairyTapes::Ui::kAmber);
    ctx->drawString(listenText_.c_str(), c.at(x1 - 10 - ctx->getStringWidth(listenText_.c_str()), y0 + 16));
    listenZoneDrawn_ = true;
  }

  // FINE EQ: one curve on the stereo signal, or MID and SIDE once a band works in M/S
  const float zero = eqY(0.0);
  struct Trace {
    const std::vector<Point>* curve;
    CColor color;
  };
  std::vector<Trace> traces;
  if (msActive_)
    traces = {{&fineMid_, kYellow}, {&fineSide_, kSideBlue}};
  else
    traces = {{&fineSt_, FairyTapes::Ui::kWhite}};
  for (const Trace& t : traces) {
    c.fill(closedTo(*t.curve, zero), withOpacity(t.color, listening ? 0.03 : 0.08));
    c.stroke(*t.curve, withOpacity(t.color, listening ? 0.35 : 1.0), 1.8);
  }

  // VIBE EQ: amber, the static setting dashed under it while the DYN pulls a boost back
  const bool vibeOn = state_.stageOn[static_cast<int>(Stage::Vibe)];
  const bool pulling = frame_.dynLow > 0.01 || frame_.dynAir > 0.01;
  if (vibeOn) {
    if (pulling) {
      c.stroke(vibeStatic_, withOpacity(FairyTapes::Ui::kAmber, 0.45), 1, 3, 3);
      vibeDashedDrawn_ = true;
    }
    c.stroke(vibeLive_, withOpacity(FairyTapes::Ui::kAmber, 0.9), 1.6);
  }

  // legends: the FINE EQ traces on the first line, VIBE on the second
  if (msActive_)
    drawLegend(c, {{"MID", kYellow}, {"SIDE", kSideBlue}}, y0 + kLegendY);
  else
    drawLegend(c, {{"STEREO", FairyTapes::Ui::kWhite}}, y0 + kLegendY);
  if (vibeOn) {
    c.hline(y0 + 26, x0 + 10, x0 + 24, FairyTapes::Ui::kAmber, 2);
    static const SharedPointer<CFontDesc> font = makeOwned<CFontDesc>("Arial", 9, kBoldFace);
    ctx->setFont(font);
    ctx->setFontColor(FairyTapes::Ui::kAmber);
    ctx->drawString("VIBE", c.at(x0 + 29, y0 + 29.5));
  }
}

void ScreenDisplayView::drawHistory(CDrawContext* ctx, ScreenView v) {
  ++drawCount_[static_cast<std::size_t>(v)];
  const CRect& r = getViewSize();
  const Canvas c{ctx, r.left - kScreenRect.x, r.top - kScreenRect.y};
  const double x0 = kScreenRect.x, x1 = kScreenRect.x + kScreenRect.w, y0 = kScreenRect.y;
  const bool glue = v == ScreenView::Glue;
  const History& h = history(v);

  for (int k = 1; k < 4; ++k) c.hline(y0 + k * kScreenRect.h / 4, x0, x1, kGridLine, 1);
  if (h.size() > 1) {
    c.fill(curvePath(v, HistoryTrace::LevelFill, h), withOpacity(kLevelGrey, 0.45));
    c.stroke(curvePath(v, HistoryTrace::Gr, h), FairyTapes::Ui::kAmber, 1.6);
    if (glue) c.stroke(curvePath(v, HistoryTrace::NetGr, h), withOpacity(FairyTapes::Ui::kAmber, 0.8), 1.2, 3, 3);
  }
  const CColor line = withOpacity(FairyTapes::Ui::kWhite, 0.6);
  if (glue) {
    c.hline(levelY(v, glueThreshDb_), x0, x1, line, 1, 4, 4);
    drawLegend(c, {{"LEVEL", kLevelGrey}, {"GR", FairyTapes::Ui::kAmber}, {"NET (MIX)", FairyTapes::Ui::kAmber, 3, 3}, {"THRESHOLD", FairyTapes::Ui::kWhite, 4, 4}},
               y0 + kLegendY);
  } else {
    c.hline(levelY(v, ceilingDb_), x0, x1, line, 1, 4, 4);   // the CEILING
    limitThreshDrawn_ = state_.stageOn[static_cast<int>(Stage::Limit)];
    if (limitThreshDrawn_) c.hline(levelY(v, frame_.limitThreshDb), x0, x1, withOpacity(FairyTapes::Ui::kWhite, 0.4), 1, 1, 3);
    drawLegend(c, {{"LEVEL", kLevelGrey}, {"GR", FairyTapes::Ui::kAmber}, {"CEILING", FairyTapes::Ui::kWhite, 4, 4}, {"THRESHOLD", FairyTapes::Ui::kWhite, 1, 3}},
               y0 + kLegendY);
  }
}

void ScreenDisplayView::drawTransfer(CDrawContext* ctx, ScreenView v) {
  ++drawCount_[static_cast<std::size_t>(v)];
  const CRect& r = getViewSize();
  const Canvas c{ctx, r.left - kScreenRect.x, r.top - kScreenRect.y};
  const bool clip = v == ScreenView::Clip;
  const double max = clip ? Scale::kClipMax : Scale::kFinalMax;
  auto X = [clip](double in) { return clip ? clipX(in) : finalX(in); };
  auto Y = [clip](double out) { return clip ? clipY(out) : finalY(out); };

  // the plot: its frame, and the line of no change
  const float left = X(0.0), right = X(max), top = Y(max), bottom = Y(0.0);
  c.stroke({{left, top}, {right, top}, {right, bottom}, {left, bottom}, {left, top}}, kFrameLine, 1);
  c.stroke({{left, bottom}, {right, top}}, withOpacity(FairyTapes::Ui::kWhite, 0.35), 1, 3, 3);
  if (!clip) c.hline(finalCeilingY(ceilingLin_), left, right, withOpacity(FairyTapes::Ui::kWhite, 0.6), 1, 4, 4);
  c.stroke(clip ? clipCurve_ : finalCurve_, FairyTapes::Ui::kAmber, 1.8);
  c.ring(dot_.x, dot_.y, kDotRadius, kScreenFill, FairyTapes::Ui::kWhite, 1.5);

  const double y = kScreenRect.y + kLegendY;
  if (clip)
    drawLegend(c, {{"TRANSFER", FairyTapes::Ui::kAmber}, {"NO CLIP", FairyTapes::Ui::kWhite, 3, 3}}, y);
  else
    drawLegend(c, {{"TRANSFER", FairyTapes::Ui::kAmber}, {"NO CHANGE", FairyTapes::Ui::kWhite, 3, 3}, {"CEILING", FairyTapes::Ui::kWhite, 4, 4}}, y);
}

} // namespace FairyTapes::HappyEnding::Ui
