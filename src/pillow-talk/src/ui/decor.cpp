// Pillow Talk GUI: tape and screw placement from the instance seed (look of docs/mockups/pillow_rack.html).
#include "ui/decor.h"

#include <algorithm>
#include <cmath>

namespace FairyTapes::PillowTalk::Ui {

namespace {

constexpr float kPi = 3.14159265358979f;

// The shared mulberry32 (fairy_ui/wear.h), in float as the placement was first written.
struct Rng {
  FairyTapes::Ui::WearRng r;
  explicit Rng(uint32_t s) : r(s) {}
  float next() { return static_cast<float>(r.next()); }
  float rnd(float lo, float hi) { return lo + next() * (hi - lo); }
  int below(int n) { return std::min(n - 1, static_cast<int>(next() * static_cast<float>(n))); }
};

} // namespace

WearLayout layoutFromSeed(uint32_t seed, WearLevel w, const Rect& panel) {
  WearLayout out;
  const float kx = panel.w / kDecorDesignW, ky = panel.h / kDecorDesignH;

  // Screws: independent of the wear level.
  {
    Rng r(seed ^ 0xA5u);
    bool cross[4];
    float ang[4];
    for (int i = 0; i < 4; ++i) {
      cross[i] = r.next() >= 0.5f;
      ang[i] = std::round(r.rnd(0.f, 180.f));
    }
    int missing = -1;
    if (r.next() < 0.06f) missing = r.below(4);
    for (int i = 0; i < 4; ++i)
      out.screws.push_back({panel.x + kScrewPos[i][0] * kx, panel.y + kScrewPos[i][1] * ky, ang[i], cross[i], i == missing});
  }

  // Tapes: draw parameters for three slots always, so Light is a prefix of Heavy.
  {
    Rng r(seed ^ 0x7A9Eu);
    int order[kNumTapeSlots];
    for (int i = 0; i < kNumTapeSlots; ++i) order[i] = i;
    for (int i = kNumTapeSlots - 1; i > 0; --i) std::swap(order[i], order[r.below(i + 1)]);
    const int extra = r.next() < 0.5f ? 1 : 0;
    const int want = w == WearLevel::Off ? 0 : (w == WearLevel::Light ? 1 : 2) + extra;
    for (int t = 0; t < 3; ++t) {
      const TapeSlot& s = kTapeSlots[order[t]];
      const float jx = r.rnd(-s.jx, s.jx), jy = r.rnd(-s.jy, s.jy);
      const float tw = r.rnd(58.f, 96.f), th = r.rnd(19.f, 26.f), rot = r.rnd(-9.f, 9.f);
      if (t >= want) continue;
      TapePlacement p{panel.x + (s.cx + jx) * kx, panel.y + (s.cy + jy) * ky, tw * kx, th * kx, rot, order[t]};
      const float rad = rot * kPi / 180.f, c = std::fabs(std::cos(rad)), sn = std::fabs(std::sin(rad));
      const float hx = 0.5f * (p.w * c + p.h * sn) + 1.f, hy = 0.5f * (p.w * sn + p.h * c) + 1.f;
      p.cx = std::clamp(p.cx, panel.x + hx, std::max(panel.x + hx, panel.x + panel.w - hx));
      p.cy = std::clamp(p.cy, panel.y + hy, std::max(panel.y + hy, panel.y + panel.h - hy));
      out.tapes.push_back(p);
    }
  }
  return out;
}

} // namespace FairyTapes::PillowTalk::Ui
