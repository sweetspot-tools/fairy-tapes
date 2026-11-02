/**
 * Fairy Tapes · Happy Ending — the model behind the shared screen (spec §4, §13): pure, no VSTGUI.
 *
 *  - EqCurves: FINE EQ and VIBE EQ responses, computed by the editor on its OWN FineEq / VibeEq (prepared at 48 kHz,
 *    set from the parameter values through mapParams): never read from the engine, never run on audio.
 *  - clipTransfer / finalTransfer: the static transfer curves of CLIP and FINAL, the same maths as the DSP blocks
 *    (clip_softly.h, clip_only2.h, shared/inflator) with the stage values plus the LOUDNESS push of kLoudCurve.
 *  - ScrollHistory: the ring behind the GLUE / LIMIT scrolling view (oldest first).
 *  - The axis scales of the preview (happy_ending_rack.html: fx/dy, lvY/grY, llY/lgY, ccx/ccy, fcx/fcy) in the
 *    coordinates of kScreenRect, and curvePath(): the points to draw, every one inside kScreenRect whatever the input
 *    (extreme values, NaN, infinities).
 *
 * Differences from the preview, forced by the wider screen (388 x 232 instead of the small boxes): the CLIP axes run to
 * 1.05 on both sides (the preview ran 0..1 and let the last 5 % leave its box) and the EQ line at +-7.5 dB is clipped
 * to the screen edge as the preview's clip path did.
 */

#pragma once

#include "dsp/fine_eq.h"
#include "dsp/vibe_eq.h"
#include "ui/layout.h"

#include <array>
#include <cstddef>
#include <vector>

namespace FairyTapes::HappyEnding::Ui {

enum class ScreenView { Eq, Glue, Clip, Limit, Final };

// ---- EQ curves ------------------------------------------------------------------------------------------------------

class EqCurves {
public:
  static constexpr double kSampleRate = 48000.0;

  // Starts from the default parameters.
  EqCurves();
  EqCurves(const EqCurves&) = delete;             // VibeEq holds atomics: one owner per screen
  EqCurves& operator=(const EqCurves&) = delete;

  // Sets both blocks from the stored table (kNumParams normalized values, ParamID order). LISTEN never enters.
  void update(const double* norm);

  // Some band that is on works on MID or SIDE (and the stage is on): the screen then draws a MID and a SIDE curve.
  bool msActive() const { return msActive_; }

  // FINE EQ response in dB at `hz`. Target 0 = bands on ST; 1 = MID: those plus the bands on MID; 2 = SIDE: those plus
  // the bands on SIDE. 0 dB while the stage is off (the signal passes untouched).
  double fineDb(int target, double hz) const;
  // VIBE EQ response with the DYN pull-back at this moment: pullLow / pullAir are the published amounts 0..1
  // (kDispDynLow / kDispDynAir; clamped, NaN = 0). 0 dB while the stage is off.
  double vibeDb(double hz, float pullLow, float pullAir) const;
  // The same with the settings alone (the dashed line when the DYN is pulling back).
  double vibeStaticDb(double hz) const;

private:
  DSP::FineEq fine_;
  DSP::VibeEq vibe_;
  bool fineOn_ = true;
  bool msActive_ = false;
};

// ---- Transfer curves ------------------------------------------------------------------------------------------------

// CLIP: output for an input sample x (signed, linear), at the DRIVE of the table plus the LOUDNESS push. Identity while
// the stage is off. The static response of ClipSoftly (a constant input settles to sin(clamp(g x)) / g).
double clipTransfer(double x, const double* norm);

// FINAL: output for an input sample x (signed, linear) under the ceiling `ceilingLin` (linear), in the active mode
// (INFLATE: EFFECT + push, CURVE; CLIP: DRIVE + push), the static response of FinalStage. Identity while the stage is off.
double finalTransfer(double x, const double* norm, double ceilingLin);

// The CEILING of the table as a linear amplitude (the line the FINAL view draws, the ceiling argument of finalTransfer).
double ceilingLinear(const double* norm);

// ---- Scrolling history ----------------------------------------------------------------------------------------------

// One sample of the GLUE / LIMIT view. For GLUE: threshDb is the threshold line, netGrDb the reduction after the MIX;
// for LIMIT: threshDb is the line to draw (the ceiling), netGrDb is unused.
struct ScrollPoint {
  float levelDb = -120.0f;
  float grDb = 0.0f;
  float netGrDb = 0.0f;
  float threshDb = 0.0f;
};

template <std::size_t N>
class ScrollHistory {
  static_assert(N >= 2, "a history needs at least two points");

public:
  static constexpr std::size_t capacity() { return N; }
  std::size_t size() const { return count_; }
  void clear() { head_ = 0; count_ = 0; }
  void push(const ScrollPoint& p) {
    buf_[head_] = p;
    head_ = (head_ + 1) % N;
    if (count_ < N) ++count_;
  }
  // i = 0 is the oldest point; an index past the newest reads the newest; an empty history reads the default point.
  ScrollPoint at(std::size_t i) const {
    if (count_ == 0) return ScrollPoint{};
    if (i >= count_) i = count_ - 1;
    return buf_[(head_ + N - count_ + i) % N];
  }

private:
  std::array<ScrollPoint, N> buf_{};
  std::size_t head_ = 0;  // where the next point goes
  std::size_t count_ = 0;
};

// ---- Axis scales (in the coordinates of kScreenRect) ----------------------------------------------------------------

namespace Scale {
inline constexpr double kEqMinHz = 20.0, kEqMaxHz = 20000.0;  // logarithmic
inline constexpr double kEqRangeDb = 7.5;                      // +-
inline constexpr double kGlueLevelMinDb = -36.0, kGlueGrMaxDb = 12.0;
inline constexpr double kLimitLevelMinDb = -24.0, kLimitGrMaxDb = 8.0;
inline constexpr double kClipMax = 1.05;                       // input and output
inline constexpr double kFinalMax = 1.25;
inline constexpr int kEqPoints = 241;                          // points of an EQ curve
inline constexpr int kClipPoints = 121;
inline constexpr int kFinalPoints = 141;
} // namespace Scale

float eqX(double hz);                                 // 20 Hz at the left edge, 20 kHz at the right
float eqY(double db);                                 // 0 dB on the middle line, +-7.5 dB as the preview's dy()
float levelY(ScreenView view, double db);             // GLUE (any view but LIMIT): -36..0 dBFS from the bottom; LIMIT: -24..0
float grY(ScreenView view, double db);                // GLUE: 0..12 dB from the top; LIMIT: 0..8
float clipX(double in);                               // 0..1.05 inside a 10 px margin
float clipY(double out);
float finalX(double in);                              // 0..1.25
float finalY(double out);
float finalCeilingY(double ceilingLin);               // the ceiling line of the FINAL view
// x of point i of n in a history of `capacity` points: the newest (n - 1) at the right edge, the oldest of a full one
// at the left edge.
float historyX(std::size_t i, std::size_t n, std::size_t capacity);

// ---- Paths ----------------------------------------------------------------------------------------------------------

enum class EqTrace { FineSt, FineMid, FineSide, Vibe, VibeStatic };
enum class HistoryTrace { Level, LevelFill, Gr, NetGr, Thresh };

// An EQ curve, 20 Hz to 20 kHz left to right. pullLow / pullAir are read by EqTrace::Vibe only.
std::vector<Point> curvePath(const EqCurves& curves, EqTrace trace, float pullLow = 0.0f, float pullAir = 0.0f);

// The transfer curve of CLIP or FINAL (the absolute value, as the preview draws it); empty for any other view.
// ceilingLin is read by FINAL only.
std::vector<Point> curvePath(ScreenView view, const double* norm, double ceilingLin);

// A trace of the GLUE / LIMIT history, oldest to newest (LevelFill: the level closed down to the bottom edge, for the
// grey area); empty for an empty history or any other view.
template <std::size_t N>
std::vector<Point> curvePath(ScreenView view, HistoryTrace trace, const ScrollHistory<N>& history) {
  std::vector<Point> out;
  if (view != ScreenView::Glue && view != ScreenView::Limit) return out;
  const std::size_t n = history.size();
  out.reserve(n + 2);
  for (std::size_t i = 0; i < n; ++i) {
    const ScrollPoint p = history.at(i);
    const float x = historyX(i, n, N);
    switch (trace) {
      case HistoryTrace::Level:
      case HistoryTrace::LevelFill: out.push_back({x, levelY(view, p.levelDb)}); break;
      case HistoryTrace::Gr: out.push_back({x, grY(view, p.grDb)}); break;
      case HistoryTrace::NetGr: out.push_back({x, grY(view, p.netGrDb)}); break;
      case HistoryTrace::Thresh: out.push_back({x, levelY(view, p.threshDb)}); break;
    }
  }
  if (trace == HistoryTrace::LevelFill && n > 0) {
    const float bottom = kScreenRect.y + kScreenRect.h;
    out.push_back({out.back().x, bottom});
    out.push_back({out.front().x, bottom});
  }
  return out;
}

} // namespace FairyTapes::HappyEnding::Ui
