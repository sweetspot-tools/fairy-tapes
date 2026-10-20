/**
 * Fairy Tapes shared UI — panel wear generator (docs/DESIGN_SYSTEM.md, "Usura"). Deterministic for a seed: the same
 * seed, level and panel size give the same list of marks on every platform (mulberry32, no std::random). Header-only and
 * free of VSTGUI so it is unit tested; WearView (fairy_ui/wear_view.h) draws the marks.
 *
 * Marks (ported from the approved preview, first written for The Stripper): faint stains, dry-brush residue (bundles of
 * thin bristle marks), scratches (a dark line offset under a light one) and specks. Tape pieces and screws are not part
 * of this list (they are interactive and placed by each plug-in).
 *
 * For the reference panel (1000 x 628, the default) the output is bit-identical to The Stripper's original generator:
 * same RNG call order, same constants. Another panel size scales the positions only; sizes, widths and colours of the
 * marks stay in panel units.
 */

#pragma once

#include "fairy_ui/wear_rng.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace FairyTapes::Ui {

// User preference: how worn the panel looks.
enum class WearLevel { Off, Light, Heavy };

constexpr double kWearWidth = 1000, kWearHeight = 628;   // reference panel size
constexpr std::uint32_t kDefaultWearSeed = 0x51;

struct WearPt {
  double x, y;
};

struct WearMark {
  enum class Kind { Stain, Bristle, Scratch, Speck };
  Kind kind = Kind::Speck;
  std::vector<WearPt> pts;     // Stain: outline polygon. Bristle/Scratch: from, control, to (quadratic curve). Speck: centre.
  double width = 1;            // line width, or speck radius
  std::uint32_t rgb = 0;       // 0xRRGGBB
  double alpha = 1;            // main opacity
  double shadowAlpha = 0;      // Scratch: opacity of the dark line under the light one (offset 0.5, 0.6)
  bool roundCap = false;       // Bristle
  std::vector<double> dashes;  // Bristle: dash pattern, empty = solid
};

// mulberry32 (fairy_ui/wear_rng.h, shared with the control wear): the same sequence for the same seed.

// Box that holds everything a mark can paint (its points, padded by the line width plus 2): used to skip marks
// outside the area being redrawn. For curves the control point is included, so the box is never too small.
struct WearBox {
  double left, top, right, bottom;
};
inline WearBox wearMarkBounds(const WearMark& m) {
  double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
  for (const auto& p : m.pts) {
    x0 = std::min(x0, p.x);
    y0 = std::min(y0, p.y);
    x1 = std::max(x1, p.x);
    y1 = std::max(y1, p.y);
  }
  const double pad = m.width + 2;
  return {x0 - pad, y0 - pad, x1 + pad, y1 + pad};
}

// How much wear each level gives (multiplies the mark counts).
inline double wearAmount(WearLevel level) { return level == WearLevel::Off ? 0.0 : level == WearLevel::Heavy ? 2.2 : 1.0; }

// Marks for a panel of `width` x `height` (panel units, origin top left).
inline std::vector<WearMark> generateWear(std::uint32_t seed, WearLevel level, double width = kWearWidth,
                                          double height = kWearHeight) {
  std::vector<WearMark> out;
  const double amt = wearAmount(level);
  if (amt <= 0.0 || !(width > 0.0) || !(height > 0.0)) return out;
  // Positions are written for the reference panel and scaled (x 1.0 exactly for the reference, so nothing changes).
  const double sx = width / kWearWidth, sy = height / kWearHeight;
  WearRng R(seed);
  auto n = [&](double c) { return static_cast<int>(std::lround(c * amt)); };
  constexpr double kPi = 3.14159265358979323846;

  // faint stains (dust and sweat): a ragged blob, almost transparent
  for (int i = 0; i < n(6); ++i) {
    const bool warm = R.next() < 0.45;
    const double cx = R.range(40 * sx, 960 * sx), cy = R.range(100 * sy, 600 * sy), rx = R.range(25, 95), ry = R.range(12, 45);
    WearMark m;
    m.kind = WearMark::Kind::Stain;
    constexpr int kPoints = 9;
    for (int k = 0; k < kPoints; ++k) {
      const double a = 2 * kPi * k / kPoints, j = R.range(0.55, 1.15);
      m.pts.push_back({cx + std::cos(a) * rx * j, cy + std::sin(a) * ry * j});
    }
    m.rgb = warm ? 0xb99a5a : 0xd8d8d2;
    m.alpha = R.range(0.012, 0.04);
    out.push_back(std::move(m));
  }

  // dry-brush residue: bundles of thin bristle marks, heavy where the stroke lands, with ragged tails
  static const std::uint32_t kBristleColours[] = {0x6a5a38, 0x54472d, 0x7a6a46, 0x8c8778, 0x9a8e6e, 0x5b3f2c, 0x47402f};
  for (int q = 0; q < n(9); ++q) {
    const bool big = R.next() < 0.7;
    const double cx0 = R.range(30 * sx, 970 * sx), cy0 = R.range(100 * sy, 600 * sy), dir = R.range(0, 2 * kPi);
    const double len = big ? R.range(22, 70) : R.range(7, 16), width = big ? R.range(5, 13) : R.range(2.5, 5);
    const int bristles = static_cast<int>(std::lround(width / R.range(0.55, 0.9)));
    const std::uint32_t col = kBristleColours[R.pick(7)];
    const double cs = std::cos(dir), sn = std::sin(dir);
    for (int i = 0; i < bristles; ++i) {
      const double t = (i - (bristles - 1) / 2.0) / std::max(1, bristles - 1);
      const double off = t * width + R.range(-0.35, 0.35), edge = std::fabs(t) * 2;
      const double l = len * R.range(0.45, 1) * (1 - edge * 0.35);
      const double s0 = -len / 2 + R.range(0, len * 0.28) + edge * len * 0.08;
      const double x1 = cx0 - sn * off + cs * s0, y1 = cy0 + cs * off + sn * s0;
      const double x2 = x1 + cs * l, y2 = y1 + sn * l;
      const double bend = R.range(-1.6, 1.6);
      WearMark m;
      m.kind = WearMark::Kind::Bristle;
      m.pts = {{x1, y1}, {(x1 + x2) / 2 - sn * bend, (y1 + y2) / 2 + cs * bend}, {x2, y2}};
      if (R.next() < 0.55)
        for (int d = 0; d < 6; ++d) m.dashes.push_back(d % 2 ? R.range(0.8, 6) : R.range(2, 20));
      m.rgb = R.next() < 0.14 ? 0x16120a : col;
      m.width = R.range(0.45, 1.15) * (1 - edge * 0.3);
      m.roundCap = R.next() >= 0.5;
      out.push_back(std::move(m));
    }
    const int loads = 1 + static_cast<int>(R.next() * 3);
    for (int h = 0; h < loads; ++h) {
      const double ho = R.range(-width * 0.4, width * 0.4);
      const double hx = cx0 - len / 2 * cs - sn * ho, hy = cy0 - len / 2 * sn + cs * ho;
      const double ex = hx + cs * len * R.range(0.1, 0.25), ey = hy + sn * len * R.range(0.1, 0.25);
      WearMark m;
      m.kind = WearMark::Kind::Bristle;
      m.pts = {{hx, hy}, {(hx + ex) / 2, (hy + ey) / 2}, {ex, ey}};
      m.rgb = col;
      m.width = R.range(1.2, 2.2);
      m.roundCap = true;
      out.push_back(std::move(m));
    }
  }

  // scratches: a dark line offset under a light one, mostly nearly horizontal, a few long ones
  for (int j = 0; j < n(55); ++j) {
    const double x = R.range(20 * sx, 980 * sx), y = R.range(20 * sy, 608 * sy);
    const double len = R.next() < 0.12 ? R.range(120, 260) : R.range(14, 90);
    const double ang = R.next() < 0.6 ? R.range(-0.35, 0.35) : R.range(0, 2 * kPi), cur = R.range(-0.12, 0.12);
    const double x2 = x + std::cos(ang) * len, y2 = y + std::sin(ang) * len;
    WearMark m;
    m.kind = WearMark::Kind::Scratch;
    m.pts = {{x, y}, {(x + x2) / 2 + std::sin(ang) * len * cur, (y + y2) / 2 - std::cos(ang) * len * cur}, {x2, y2}};
    m.width = R.range(0.35, 0.9);
    m.shadowAlpha = R.range(0.35, 0.6);
    m.rgb = 0xd6d6d0;
    m.alpha = R.range(0.08, 0.26);
    out.push_back(std::move(m));
  }

  // chips and specks
  for (int c = 0; c < n(90); ++c) {
    WearMark m;
    m.kind = WearMark::Kind::Speck;
    m.pts = {{R.range(10 * sx, 990 * sx), R.range(10 * sy, 618 * sy)}};
    m.width = R.range(0.3, 1.1);
    m.rgb = R.next() < 0.7 ? 0xcfcfc8 : 0x000000;
    m.alpha = R.range(0.1, 0.4);
    out.push_back(std::move(m));
  }
  return out;
}

} // namespace FairyTapes::Ui
