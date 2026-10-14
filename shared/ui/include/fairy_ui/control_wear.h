// Fairy Tapes shared UI: wear of the controls (knob caps and button faces), seeded and deterministic.
// Header-only and free of VSTGUI: unit tested. The controls draw the marks over their base look, at 25 % opacity
// (spec 4: the material must show through); chips and specks keep their own opacity inside that.
//
// Knob: dirt where the cap meets the panel (more of it toward the bottom), a worn edge on the side of the finger
// (paint chipped away, bare metal showing), dents, residue and specks on the cap, a dull pointer inlay.
// Button: dirt around the face, worn corners and bottom edge, light scratches, specks.
#pragma once

#include "fairy_ui/wear_rng.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace FairyTapes::Ui {

struct ControlWearMark {
  enum class Kind { Blob, Chip, Speck, Stroke, Pointer };
  Kind kind = Kind::Speck;
  std::vector<std::pair<double, double>> pts;   // Blob/Chip: polygon. Speck: centre. Stroke: start, control, end (quadratic)
  double size = 1;                              // Speck radius, Stroke width, Pointer: position along the inlay (0..1)
  double length = 0;                            // Pointer: length along the inlay (0..1)
  std::uint32_t rgb = 0;
  double alpha = 1;
  bool underCap = false;                        // knob: drawn below the cap (dirt around its base)
};

namespace control_wear_detail {
constexpr double kPi = 3.14159265358979323846;

// An irregular blob around (cx, cy), `n` points, like the preview's sblobG (smooth outline, here as a polygon).
inline std::vector<std::pair<double, double>> blob(WearRng& R, double cx, double cy, double rx, double ry, int n, double rot = 0) {
  std::vector<std::pair<double, double>> p;
  for (int i = 0; i < n; ++i) {
    const double a = 2 * kPi * i / n, m = R.range(0.62, 1.18);
    const double x = std::cos(a) * rx * m, y = std::sin(a) * ry * m;
    p.push_back({cx + x * std::cos(rot) - y * std::sin(rot), cy + x * std::sin(rot) + y * std::cos(rot)});
  }
  return p;
}

// Dirt in browns or scratched paint in greys.
inline std::uint32_t speckColour(WearRng& R) {
  static const std::uint32_t browns[] = {0x6a5a38, 0x54472d, 0x3a2f1d, 0x7a6a46, 0x4a3d27};
  static const std::uint32_t greys[] = {0x8d8d90, 0xa2a2a6, 0x6f6f73, 0xb4b4b8};
  return R.next() < 0.5 ? browns[R.pick(5)] : greys[R.pick(4)];
}
}  // namespace control_wear_detail

// Marks of one knob, relative to the cap centre, for a cap of radius R. `amount`: 0 = none, 1 = light, 2.2 = heavy.
inline std::vector<ControlWearMark> knobWear(std::uint32_t seed, double amount, double R) {
  using namespace control_wear_detail;
  std::vector<ControlWearMark> out;
  if (amount <= 0.0 || R <= 0.0) return out;
  const double Z = std::max(0.35, R / 56.0);   // the preview scales its fine marks with the cap
  auto n = [&](double c) { return static_cast<int>(std::lround(c * amount)); };

  {  // dirt where the cap meets the panel: ragged opaque patches, more of them toward the bottom, plus specks
    WearRng r(seed ^ 0x11u);
    for (int i = 0; i < n(26); ++i) {
      const double a = r.range(0, 2 * kPi), down = 0.35 + 0.65 * std::pow(std::sin(a) * 0.5 + 0.5, 1.5);
      if (r.next() > down) continue;
      const double d = r.range(1.04, 1.3) * R;
      ControlWearMark m;
      m.kind = ControlWearMark::Kind::Blob;
      m.pts = blob(r, std::cos(a) * d, std::sin(a) * d, Z * r.range(2, 8), Z * r.range(1.2, 3.6), 7);
      m.rgb = r.next() < 0.7 ? 0x0c0a07 : 0x17130d;
      m.underCap = true;
      out.push_back(std::move(m));
    }
    for (int i = 0; i < n(14); ++i) {
      const double a = r.range(0, 6.28), d = r.range(1.04, 1.34) * R;
      ControlWearMark m;
      m.kind = ControlWearMark::Kind::Speck;
      m.pts = {{std::cos(a) * d, std::sin(a) * d}};
      m.size = Z * r.range(0.4, 1.1);
      m.rgb = speckColour(r);
      m.alpha = 1;
      m.underCap = true;
      out.push_back(std::move(m));
    }
  }
  {  // worn edge on the finger side (lower right): chipped bands with bare metal, and tiny dents
    WearRng r(seed ^ 0x33u);
    const double bias = 0.78 + r.range(-0.25, 0.25);
    for (int i = 0; i < n(40); ++i) {
      const double a3 = r.range(0, 2 * kPi), wt = std::pow(std::cos((a3 - bias) / 2), 2), p = 0.05 + 0.6 * wt;
      if (r.next() > p) continue;
      const double len = r.range(0.03, 0.14) * (0.5 + wt);
      const int steps = 3 + static_cast<int>(len * 40);
      ControlWearMark m;
      m.kind = ControlWearMark::Kind::Chip;
      for (int k = 0; k <= steps; ++k) {
        const double an = a3 + len * k / steps;
        m.pts.push_back({std::cos(an) * R * 0.997, std::sin(an) * R * 0.997});
      }
      for (int k = steps; k >= 0; --k) {
        const double an = a3 + len * k / steps;
        const double dp = (k == 0 || k == steps) ? 0.004 : r.range(0.01, 0.075) * (0.45 + wt);
        m.pts.push_back({std::cos(an) * R * (1 - dp), std::sin(an) * R * (1 - dp)});
      }
      m.rgb = 0xc9cbd0;
      m.alpha = r.range(0.6, 0.95);
      out.push_back(std::move(m));
    }
    for (int i = 0; i < n(4); ++i) {
      const double a = r.range(0, 2 * kPi);
      ControlWearMark dent;
      dent.kind = ControlWearMark::Kind::Speck;
      dent.pts = {{std::cos(a) * R * 0.985, std::sin(a) * R * 0.985}};
      dent.size = Z * r.range(0.8, 1.6);
      dent.rgb = 0x050505;
      out.push_back(std::move(dent));
    }
  }
  {  // residue and specks on the cap (dirt = browns, scratched paint = greys)
    WearRng r(seed ^ 0x44u);
    static const std::uint32_t residue[] = {0x34302a, 0x2a251d, 0x3d3a36, 0x2f2a20};
    for (int i = 0; i < n(3); ++i) {
      const double a = r.range(0, 6.28), d = r.range(0.25, 0.8) * R;
      const std::uint32_t col = residue[r.pick(4)];
      ControlWearMark m;
      m.kind = ControlWearMark::Kind::Blob;
      m.pts = blob(r, std::cos(a) * d, std::sin(a) * d, R * r.range(0.06, 0.2), R * r.range(0.04, 0.12), 8, r.range(0, kPi));
      m.rgb = col;
      out.push_back(std::move(m));
    }
    for (int i = 0; i < n(16); ++i) {
      const double a = r.range(0, 6.28), d = std::sqrt(r.next()) * R * 0.95;
      ControlWearMark m;
      m.kind = ControlWearMark::Kind::Speck;
      m.pts = {{std::cos(a) * d, std::sin(a) * d}};
      m.size = Z * r.range(0.3, 0.9);
      m.rgb = speckColour(r);
      out.push_back(std::move(m));
    }
  }
  {  // the pointer inlay worn through in places (the cap colour showing) and one or two nicks
    WearRng r(seed ^ 0x55u);
    for (int i = 0, k = 3 + static_cast<int>(r.next() * 2); i < k && amount > 0; ++i) {
      const double c0 = r.range(0.06, 0.8), w = r.range(0.12, 0.32);
      ControlWearMark m;
      m.kind = ControlWearMark::Kind::Pointer;
      m.size = c0;
      m.length = std::min(w, 1 - c0);
      m.rgb = 0x232427;
      m.alpha = r.range(0.4, 0.62);
      out.push_back(std::move(m));
    }
    for (int i = 0, k = 2 + static_cast<int>(r.next() * 2); i < k; ++i) {
      const double k0 = r.range(0.1, 0.9);
      ControlWearMark m;
      m.kind = ControlWearMark::Kind::Pointer;
      m.size = k0;
      m.length = std::min(r.range(0.03, 0.06), 1 - k0);
      m.rgb = 0x0c0c0d;
      m.alpha = 1;
      out.push_back(std::move(m));
    }
  }
  return out;
}

// Marks of one button face of w x h, relative to its top left corner.
inline std::vector<ControlWearMark> buttonWear(std::uint32_t seed, double amount, double w, double h) {
  using namespace control_wear_detail;
  std::vector<ControlWearMark> out;
  if (amount <= 0.0 || w <= 0.0 || h <= 0.0) return out;
  auto n = [&](double c) { return static_cast<int>(std::lround(c * amount)); };
  WearRng r(seed ^ 0x66u);

  {  // dirt in the gap around the face: specks just outside its edges
    WearRng g(seed ^ 0x22u);
    for (int i = 0; i < n(14); ++i) {
      const bool side = g.next() < 0.5;
      double px, py;
      if (side) {
        px = g.range(-3, w + 3);
        py = g.next() < 0.5 ? -g.range(1, 3.5) : h + g.range(1, 3.5);
      } else {
        px = g.next() < 0.5 ? -g.range(1, 3.5) : w + g.range(1, 3.5);
        py = g.range(-3, h + 3);
      }
      ControlWearMark m;
      m.kind = ControlWearMark::Kind::Speck;
      m.pts = {{px, py}};
      m.size = g.range(0.3, 0.9);
      m.rgb = speckColour(g);
      m.underCap = true;   // below the face
      out.push_back(std::move(m));
    }
  }
  // worn corners: bare metal along the two edges that meet there (the lower ones wear most)
  const double chance[4] = {0.45, 0.45, 0.65, 0.95};   // top left, top right, bottom left, bottom right
  const double sx[4] = {1, -1, 1, -1}, sy[4] = {1, 1, -1, -1};
  const double cxs[4] = {0, w, 0, w}, cys[4] = {0, 0, h, h};
  for (int c = 0; c < 4; ++c) {
    if (r.next() > chance[c] * std::min(1.0, amount + 0.2)) continue;
    const double L = r.range(w * 0.1, w * 0.3);
    ControlWearMark m;
    m.kind = ControlWearMark::Kind::Stroke;
    m.pts = {{cxs[c] + sx[c] * L, cys[c]}, {cxs[c], cys[c]}, {cxs[c], cys[c] + sy[c] * L * 0.8}};
    m.size = r.range(0.9, 1.8);
    m.rgb = 0xe0e2e6;
    out.push_back(std::move(m));
  }
  for (int i = 0; i < n(4); ++i) {   // bare metal along the bottom edge
    const double ex = r.range(w * 0.05, w * 0.95), L2 = std::min(r.range(w * 0.06, w * 0.22), w - ex);   // stays on the edge
    ControlWearMark m;
    m.kind = ControlWearMark::Kind::Stroke;
    m.pts = {{ex, h - 0.5}, {ex + L2 / 2, h - 0.5}, {ex + L2, h - 0.5}};
    m.size = r.range(0.6, 1.1);
    m.rgb = 0xe0e2e6;
    m.alpha = 0.9;
    out.push_back(std::move(m));
  }
  for (int i = 0; i < n(3); ++i) {   // fine light scratches across the face
    const double x0 = r.range(0, w), y0 = r.range(0, h);
    const double x1 = std::clamp(x0 + r.range(-w * 0.4, w * 0.4), 0.0, w), y1 = std::clamp(y0 + r.range(-h * 0.3, h * 0.3), 0.0, h);   // stays on the face
    ControlWearMark m;
    m.kind = ControlWearMark::Kind::Stroke;
    m.pts = {{x0, y0}, {(x0 + x1) / 2, (y0 + y1) / 2}, {x1, y1}};
    m.size = 0.3;
    m.rgb = 0xffffff;
    m.alpha = r.range(0.12, 0.3);
    out.push_back(std::move(m));
  }
  for (int i = 0; i < n(8); ++i) {
    ControlWearMark m;
    m.kind = ControlWearMark::Kind::Speck;
    m.pts = {{r.range(1, w - 1), r.range(1, h - 1)}};
    m.size = r.range(0.25, 0.7);
    m.rgb = speckColour(r);
    out.push_back(std::move(m));
  }
  return out;
}

}  // namespace FairyTapes::Ui
