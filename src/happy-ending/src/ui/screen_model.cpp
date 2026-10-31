#include "ui/screen_model.h"

#include "dsp/clip_only2.h"
#include "engine.h"
#include "inflator.h"
#include "loudness_macro.h"
#include "params.h"

#include <algorithm>
#include <cmath>

namespace FairyTapes::HappyEnding::Ui {

namespace {

// v limited to [lo, hi]; NaN becomes `fallback`.
double clampOr(double v, double lo, double hi, double fallback) {
  if (std::isnan(v)) return fallback;
  return std::clamp(v, lo, hi);
}

// A point forced inside the screen (the axis functions already limit their value: this is the last net).
Point inScreen(double x, double y) {
  const double x0 = kScreenRect.x, x1 = kScreenRect.x + kScreenRect.w;
  const double y0 = kScreenRect.y, y1 = kScreenRect.y + kScreenRect.h;
  return {static_cast<float>(clampOr(x, x0, x1, x0)), static_cast<float>(clampOr(y, y0, y1, y1))};
}

constexpr double kMargin = 10.0;  // the transfer views keep their axes this far from the edge

// ClipSoftly's shaper limit (clip_softly.h uses this literal).
constexpr double kHalfPi = 1.57079633;

// FinalStage's CLIP branch: ClipOnly2 settles to its static value on a constant input within a few samples
// (the contraction factor of its overshoot recursion is about 0.19).
double clipOnly2Static(double u) {
  DSP::ClipOnly2 c;
  double out = 0.0;
  for (int i = 0; i < 48; ++i) out = c.process(0, u);
  return out;
}

} // namespace

// ---- EQ curves ------------------------------------------------------------------------------------------------------

EqCurves::EqCurves() {
  fine_.prepare(kSampleRate, 512);
  vibe_.prepare(kSampleRate, 512);
  double norm[kNumParams];
  loadDefaults(norm);
  update(norm);
}

void EqCurves::update(const double* norm) {
  const EngineSettings s = mapParams(norm, -1, false);  // LISTEN is for the ear: never in the curves
  fineOn_ = s.fineOn;
  fine_.setStageOn(s.fineOn);
  bool ms = false;
  for (int b = 0; b < DSP::FineEq::kNumBands; ++b) {
    const DSP::BandSettings& band = s.bands[static_cast<std::size_t>(b)];
    fine_.setBand(b, band);
    if (band.on && band.ms != 0) ms = true;
  }
  fine_.setListen(-1);
  msActive_ = fineOn_ && ms;
  vibe_.set(s.vibe);
}

double EqCurves::fineDb(int target, double hz) const {
  if (!fineOn_) return 0.0;
  return fine_.magnitudeDb(target, hz);
}

double EqCurves::vibeDb(double hz, float pullLow, float pullAir) const {
  return vibe_.magnitudeDb(hz, pullLow, pullAir);
}

double EqCurves::vibeStaticDb(double hz) const {
  return vibe_.magnitudeDb(hz, true);
}

// ---- Transfer curves ------------------------------------------------------------------------------------------------

double clipTransfer(double x, const double* norm) {
  if (std::isnan(x)) return 0.0;
  const EngineSettings s = mapParams(norm, -1, false);
  if (!s.clipOn) return x;
  const LoudPush push = loudPush(s.loudness);
  const double driveDb = std::clamp(static_cast<double>(s.clipDriveDb + push.clipDb), 0.0, 12.0);
  const double g = std::pow(10.0, driveDb / 20.0);
  return std::sin(std::clamp(x * g, -kHalfPi, kHalfPi)) / g;
}

double ceilingLinear(const double* norm) {
  return std::pow(10.0, std::min(mapParams(norm, -1, false).ceilingDb, 0.0f) / 20.0);
}

double finalTransfer(double x, const double* norm, double ceilingLin) {
  if (std::isnan(x)) return 0.0;
  const EngineSettings s = mapParams(norm, -1, false);
  if (!s.finalOn) return x;
  const double c = (std::isfinite(ceilingLin) && ceilingLin > 0.0) ? ceilingLin : 1.0;
  const LoudPush push = loudPush(s.loudness);

  double y;
  if (s.finalMode == DSP::FinalStage::kClip) {
    const float driveDb = std::clamp(std::min(2.0f, s.finalDriveDb + finalDrivePush(push)), 0.0f, 12.0f);
    const double gain = std::pow(10.0, static_cast<double>(driveDb) / 20.0);
    y = clipOnly2Static(x * gain * DSP::ClipOnly2::kKnee / c) * c / DSP::ClipOnly2::kKnee;
  } else {
    const float effect = std::clamp(std::min(1.0f, s.finalEffect + finalEffectPush(push)), 0.0f, 1.0f);
    const float curve = std::clamp(s.finalCurve, 0.0f, 1.0f);
    inflator::Inflator<float> inflate;  // the block's own settings: quartic + quadratic 50 / 50, no compensation
    inflate.setShapes(curve, curve, 0.5f, false);
    inflate.setWet(effect);
    const float u = static_cast<float>(x / c);
    y = static_cast<double>(inflate(u)) * c;
  }
  return std::clamp(y, -c, c);
}

// ---- Axis scales ----------------------------------------------------------------------------------------------------

float eqX(double hz) {
  using namespace Scale;
  const double f = clampOr(hz, kEqMinHz, kEqMaxHz, kEqMinHz);
  return static_cast<float>(kScreenRect.x + kScreenRect.w * std::log(f / kEqMinHz) / std::log(kEqMaxHz / kEqMinHz));
}

float eqY(double db) {
  // the preview's dy(): the scale reads 7 dB over the half height less 6 px, the value is limited to +-7.5 dB (the very
  // top and bottom reach the edge and stay there)
  const double d = clampOr(db, -Scale::kEqRangeDb, Scale::kEqRangeDb, 0.0);
  return static_cast<float>(kScreenRect.y + kScreenRect.h / 2.0 - d * (kScreenRect.h / 2.0 - 6.0) / 7.0);
}

float levelY(ScreenView view, double db) {
  const double lo = view == ScreenView::Limit ? Scale::kLimitLevelMinDb : Scale::kGlueLevelMinDb;
  const double d = clampOr(db, lo, 0.0, lo);
  return static_cast<float>(kScreenRect.y + kScreenRect.h - (d - lo) / -lo * (kScreenRect.h - 24.0));
}

float grY(ScreenView view, double db) {
  const double hi = view == ScreenView::Limit ? Scale::kLimitGrMaxDb : Scale::kGlueGrMaxDb;
  const double d = clampOr(db, 0.0, hi, 0.0);
  return static_cast<float>(kScreenRect.y + 22.0 + d / hi * (kScreenRect.h - 34.0));
}

namespace {
double transferX(double v, double max) {
  return kScreenRect.x + kMargin + clampOr(v, 0.0, max, 0.0) / max * (kScreenRect.w - 2.0 * kMargin);
}
double transferY(double v, double max) {
  return kScreenRect.y + kScreenRect.h - kMargin - clampOr(v, 0.0, max, 0.0) / max * (kScreenRect.h - 2.0 * kMargin);
}
} // namespace

float clipX(double in) { return static_cast<float>(transferX(in, Scale::kClipMax)); }
float clipY(double out) { return static_cast<float>(transferY(out, Scale::kClipMax)); }
float finalX(double in) { return static_cast<float>(transferX(in, Scale::kFinalMax)); }
float finalY(double out) { return static_cast<float>(transferY(out, Scale::kFinalMax)); }
float finalCeilingY(double ceilingLin) { return finalY(ceilingLin); }

float historyX(std::size_t i, std::size_t n, std::size_t capacity) {
  const double right = kScreenRect.x + kScreenRect.w;
  if (capacity < 2 || n == 0) return static_cast<float>(right);
  const std::size_t behind = n - 1 > i ? n - 1 - i : 0;  // points newer than this one
  return static_cast<float>(std::max<double>(kScreenRect.x, right - static_cast<double>(behind) * kScreenRect.w / static_cast<double>(capacity - 1)));
}

// ---- Paths ----------------------------------------------------------------------------------------------------------

std::vector<Point> curvePath(const EqCurves& curves, EqTrace trace, float pullLow, float pullAir) {
  using namespace Scale;
  std::vector<Point> out;
  out.reserve(kEqPoints);
  for (int i = 0; i < kEqPoints; ++i) {
    const double hz = kEqMinHz * std::pow(kEqMaxHz / kEqMinHz, static_cast<double>(i) / (kEqPoints - 1));
    double db = 0.0;
    switch (trace) {
      case EqTrace::FineSt: db = curves.fineDb(0, hz); break;
      case EqTrace::FineMid: db = curves.fineDb(1, hz); break;
      case EqTrace::FineSide: db = curves.fineDb(2, hz); break;
      case EqTrace::Vibe: db = curves.vibeDb(hz, pullLow, pullAir); break;
      case EqTrace::VibeStatic: db = curves.vibeStaticDb(hz); break;
    }
    out.push_back(inScreen(eqX(hz), eqY(db)));
  }
  return out;
}

std::vector<Point> curvePath(ScreenView view, const double* norm, double ceilingLin) {
  std::vector<Point> out;
  if (view == ScreenView::Clip) {
    out.reserve(Scale::kClipPoints);
    for (int i = 0; i < Scale::kClipPoints; ++i) {
      const double x = Scale::kClipMax * i / (Scale::kClipPoints - 1);
      out.push_back(inScreen(clipX(x), clipY(std::fabs(clipTransfer(x, norm)))));
    }
  } else if (view == ScreenView::Final) {
    out.reserve(Scale::kFinalPoints);
    for (int i = 0; i < Scale::kFinalPoints; ++i) {
      const double x = Scale::kFinalMax * i / (Scale::kFinalPoints - 1);
      out.push_back(inScreen(finalX(x), finalY(std::fabs(finalTransfer(x, norm, ceilingLin)))));
    }
  }
  return out;
}

} // namespace FairyTapes::HappyEnding::Ui
