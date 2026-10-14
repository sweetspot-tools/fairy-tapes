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

}  // namespace FairyTapes::Ui::Rack
