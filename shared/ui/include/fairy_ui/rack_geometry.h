// Fairy Tapes shared UI: geometry of the rack look (knob scale, button face and LED, text baselines).
// Pure math, no VSTGUI: unit tested. Numbers from the approved preview (src/the-stripper/docs/mockups/stripper_rack.html).
#pragma once

#include <cmath>

namespace FairyTapes::Ui::Rack {

struct Pt {
  double x, y;
};

// ---- Knob ---------------------------------------------------------------------------------------------------------
constexpr int kScaleDots = 11;             // printed dots around the cap, every 27 degrees
constexpr double kScaleStartDeg = 225.0;   // first dot (lower left); angles are counter-clockwise, y up
constexpr double kScaleSweepDeg = 270.0;
constexpr double kDefaultCapRadius = 19.0;
constexpr double kScaleGap = 9.0;          // dots sit this far outside the cap

constexpr double kPi = 3.14159265358979323846;

// Position of scale dot i (0..10) around a cap of radius `r` centred at (cx, cy).
inline Pt scaleDot(double cx, double cy, double r, int i) {
  const double a = (kScaleStartDeg - kScaleSweepDeg * i / (kScaleDots - 1)) * kPi / 180.0;
  return {cx + (r + kScaleGap) * std::cos(a), cy - (r + kScaleGap) * std::sin(a)};
}
constexpr double scaleDotRadius(int i) { return i % 5 == 0 ? 1.9 : 1.2; }   // ends and middle are larger

// Pointer angle (degrees, counter-clockwise, y up) for a normalized value: the same sweep as the dots.
constexpr double pointerDegrees(double norm) { return kScaleStartDeg - kScaleSweepDeg * norm; }

// End values ("-20" ... "+20"): the low one is right-aligned left of the lower left dot, the high one left-aligned right of it.
inline Pt endLabelLow(double cx, double cy, double r) {
  const double e = (r + kScaleGap) * 0.7071;
  return {cx - e - 6, cy + e + 3.5};
}
inline Pt endLabelHigh(double cx, double cy, double r) {
  const double e = (r + kScaleGap) * 0.7071;
  return {cx + e + 6, cy + e + 3.5};
}

// Text baselines under the knob.
constexpr double nameBaseline(double cy, double r) { return cy + r + 24.0; }    // centred, 12 px bold
constexpr double valueBaseline(double cy, double r) { return cy + r + 39.5; }   // centred, 10 px bold, yellow

// ---- Button -------------------------------------------------------------------------------------------------------
constexpr double kFaceWidth = 24, kFaceHeight = 22;
constexpr double kLedOffsetY = 21;    // LED centre below the face centre
constexpr double kLedRadius = 3.4;
constexpr double kLabelOffsetY = -17;   // label baseline above the face centre (11 px bold)
constexpr double kPressDepth = 1.5;     // the face sinks this much when the button is lit

// ---- Cycling selector (design system, 2026-10-21: M/S, Ratio, SC HPF, Ceil) ------------------------------------------
// A grey face like an on/off button; the LED (yellow) is lit while the value is not the default; the current value is
// printed under the LED, amber, 10 px (preview deep_throat_rack.html: face at 544, value at 583).
constexpr double kCycleValueOffsetY = 39;   // value baseline below the face centre

// The next value of a selector of `count` values (wraps; an index out of range is clamped first).
inline int cycleNextIndex(int index, int count) {
  if (count <= 0) return 0;
  const int i = index < 0 ? 0 : index >= count ? count - 1 : index;
  return (i + 1) % count;
}

// ---- Face-lit exclusive choice (design system, 2026-10-21) ----------------------------------------------------------
// The name is on the face, there is no LED, the active face lights amber. The buttons of a group sit in a grid of
// equal cells (row-major); each face is centred in its cell.
constexpr double kLitFaceWidth = 34, kLitFaceHeight = 20;   // preview default; wider faces for longer names
constexpr double kLitTextSize = 9;                          // bold, centred
constexpr double kLitTextBaseline = 3.5;                    // below the face centre

struct Box {
  double left, top, right, bottom;
};

inline int litRows(int count, int columns) { return columns > 0 ? (count + columns - 1) / columns : 0; }

// Cell `index` of a group of `count` buttons in `columns` columns, in a view of w x h (view coordinates from 0).
inline Box litChoiceCell(double w, double h, int count, int columns, int index) {
  const int rows = litRows(count, columns);
  if (rows <= 0) return {0, 0, 0, 0};
  const double cw = w / columns, ch = h / rows;
  const int col = index % columns, row = index / columns;
  return {col * cw, row * ch, (col + 1) * cw, (row + 1) * ch};
}

inline Box litFace(const Box& cell, double faceW, double faceH) {
  const double cx = (cell.left + cell.right) / 2, cy = (cell.top + cell.bottom) / 2;
  return {cx - faceW / 2, cy - faceH / 2, cx + faceW / 2, cy + faceH / 2};
}

// Index of the button whose cell holds (x, y), or -1 (outside the view, or an empty cell of an incomplete grid).
inline int litChoiceHit(double w, double h, int count, int columns, double x, double y) {
  const int rows = litRows(count, columns);
  if (count <= 0 || rows <= 0 || x < 0 || y < 0 || x >= w || y >= h) return -1;
  const int index = static_cast<int>(y / (h / rows)) * columns + static_cast<int>(x / (w / columns));
  return index < count ? index : -1;
}

}  // namespace FairyTapes::Ui::Rack
