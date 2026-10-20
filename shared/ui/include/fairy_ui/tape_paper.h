// Fairy Tapes shared UI: the paper body of a piece of tape stuck on the panel — ragged torn outline and faint fibres,
// from the seeded wear generator (header-only, no VSTGUI, unit tested in tests/test_tape.cpp). Drawn by drawTapePaper
// (fairy_ui/tape_draw.h). First written for The Stripper (src/the-stripper/src/ui/tape_pieces.h): makeTapePaper consumes
// the generator in the same order, so the same generator state gives the same paper.
#pragma once

#include "fairy_ui/wear.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace FairyTapes::Ui {

constexpr double kTapeRaggedX = 3.0;   // how far the torn ends stick out of (or into) the rectangle, px
constexpr double kTapeRaggedY = 2.5;   // same for the long edges

struct TapePaper {
  double w = 0, h = 0;                 // the unrotated rectangle
  std::vector<WearPt> outline;         // ragged outline, relative to the top left corner of the rectangle
  struct Fibre {
    double x1, y1, x2, y2, alpha;
  };
  std::vector<Fibre> fibres;           // faint fibres, relative to the top left corner
};

// The paper of a w x h tape. Empty for a degenerate size (nothing drawn, the generator untouched).
inline TapePaper makeTapePaper(double w, double h, WearRng& R) {
  TapePaper p;
  if (!(w > 0.0) || !(h > 4.0) || !std::isfinite(w) || !std::isfinite(h)) return p;
  p.w = w;
  p.h = h;
  // torn ends: a jittered top edge, jittered sides, a jittered bottom edge
  p.outline.push_back({0, R.range(0, kTapeRaggedY)});
  for (int k = 1; k <= 4; ++k) p.outline.push_back({w * k / 4, R.range(-1.2, 1.2)});
  for (int q = 0; q <= 5; ++q) p.outline.push_back({w + R.range(-kTapeRaggedX, kTapeRaggedX), h * q / 5});
  for (int k = 4; k >= 0; --k) p.outline.push_back({w * k / 4, h + R.range(-1.2, 1.2)});
  for (int q = 4; q >= 1; --q) p.outline.push_back({R.range(-kTapeRaggedX, kTapeRaggedX), h * q / 5});
  for (int f = 0; f < 5; ++f)
    p.fibres.push_back({R.range(0, w * 0.3), R.range(2, h - 2), R.range(w * 0.6, w), R.range(2, h - 2), R.range(0.08, 0.2)});
  return p;
}

inline TapePaper makeTapePaper(double w, double h, std::uint32_t seed) {
  WearRng R(seed);
  return makeTapePaper(w, h, R);
}

struct TapeBox {
  double left, top, right, bottom;
};

// Axis-aligned box around a w x h tape centred on (cx, cy), rotated by `rotDeg`, grown by `pad` on every side.
inline TapeBox tapeBounds(double cx, double cy, double w, double h, double rotDeg, double pad) {
  const double a = rotDeg * 3.14159265358979323846 / 180.0, c = std::fabs(std::cos(a)), s = std::fabs(std::sin(a));
  const double hx = 0.5 * (w * c + h * s) + pad, hy = 0.5 * (w * s + h * c) + pad;
  return {cx - hx, cy - hy, cx + hx, cy + hy};
}

} // namespace FairyTapes::Ui
