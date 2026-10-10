// Pillow Talk GUI: runtime panel wear. Ported from the look of docs/mockups/pillow_rack.html (wear()).
// Pure C++: anti-aliased thin strokes, soft blobs and polygons blended into a straight-alpha RGBA8 buffer.
#include "ui/wear.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace FairyTapes::PillowTalk::Ui {

namespace {

constexpr float kPi = 3.14159265358979f;

// mulberry32, same generator as the preview.
struct Rng {
  uint32_t a;
  explicit Rng(uint32_t s) : a(s) {}
  float next() {
    a += 0x6D2B79F5u;
    uint32_t t = (a ^ (a >> 15)) * (1u | a);
    t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
    return static_cast<float>(static_cast<double>(t ^ (t >> 14)) / 4294967296.0);
  }
  float rnd(float lo, float hi) { return lo + next() * (hi - lo); }
  int below(int n) { return std::min(n - 1, static_cast<int>(next() * static_cast<float>(n))); }
};

struct Rgb { float r, g, b; };
constexpr Rgb hex(uint32_t v) {
  return {static_cast<float>((v >> 16) & 255), static_cast<float>((v >> 8) & 255), static_cast<float>(v & 255)};
}

struct Canvas {
  uint8_t* px;
  int w, h;
  float sx, sy;   // design units -> pixels
  float minLine;  // floor for hairline widths, in pixels

  // Source-over of colour c with alpha a (0..1) onto straight-alpha RGBA8.
  void blend(int x, int y, const Rgb& c, float a) {
    if (x < 0 || y < 0 || x >= w || y >= h || a <= 0.f) return;
    uint8_t* p = px + (static_cast<size_t>(y) * w + x) * 4;
    const float da = p[3] * (1.f / 255.f);
    const float oa = a + da * (1.f - a);
    if (oa <= 0.f) return;
    const float k = da * (1.f - a);
    p[0] = static_cast<uint8_t>(std::lround(std::clamp((c.r * a + p[0] * k) / oa, 0.f, 255.f)));
    p[1] = static_cast<uint8_t>(std::lround(std::clamp((c.g * a + p[1] * k) / oa, 0.f, 255.f)));
    p[2] = static_cast<uint8_t>(std::lround(std::clamp((c.b * a + p[2] * k) / oa, 0.f, 255.f)));
    p[3] = static_cast<uint8_t>(std::lround(std::clamp(oa * 255.f, 0.f, 255.f)));
  }
};

Canvas makeCanvas(uint8_t* rgba, int w, int h, int scale) {
  Canvas c{rgba, w, h, static_cast<float>(w) / kWearDesignW, static_cast<float>(h) / kWearDesignH, 0.3f * static_cast<float>(std::max(1, scale))};
  return c;
}

// Box-filtered coverage of a line of width wpx at distance d from its centre.
inline float lineCov(float wpx, float d) { return std::clamp((1.f + wpx) * 0.5f - d, 0.f, std::min(wpx, 1.f)); }

// Anti-aliased disc (design units).
void disc(Canvas& cv, float cx, float cy, float r, const Rgb& col, float alpha) {
  const float px = cx * cv.sx, py = cy * cv.sy, rp = std::max(r * cv.sx, 0.15f);
  const int x0 = static_cast<int>(std::floor(px - rp - 1.f)), x1 = static_cast<int>(std::ceil(px + rp + 1.f));
  const int y0 = static_cast<int>(std::floor(py - rp - 1.f)), y1 = static_cast<int>(std::ceil(py + rp + 1.f));
  for (int y = std::max(0, y0); y <= std::min(cv.h - 1, y1); ++y)
    for (int x = std::max(0, x0); x <= std::min(cv.w - 1, x1); ++x) {
      const float d = std::hypot(x + 0.5f - px, y + 0.5f - py);
      const float cov = std::clamp(rp + 0.5f - d, 0.f, std::min(2.f * rp, 1.f));
      if (cov > 0.f) cv.blend(x, y, col, alpha * cov);
    }
}

// Soft-edged irregular blob: radius per angle interpolated from `n` random factors, edge softened over
// `soft` design units (stands in for the preview's Gaussian blur).
void softBlob(Canvas& cv, float cx, float cy, float rx, float ry, const float* k, int n, float soft, const Rgb& col,
              float alpha, float rotRad = 0.f) {
  const float px = cx * cv.sx, py = cy * cv.sy;
  const float ext = (std::max(rx, ry) * 1.2f + soft) * cv.sx;
  const float cs = std::cos(rotRad), sn = std::sin(rotRad);
  const float softN = soft / (0.5f * (rx + ry));
  const int x0 = std::max(0, static_cast<int>(std::floor(px - ext))), x1 = std::min(cv.w - 1, static_cast<int>(std::ceil(px + ext)));
  const int y0 = std::max(0, static_cast<int>(std::floor(py - ext))), y1 = std::min(cv.h - 1, static_cast<int>(std::ceil(py + ext)));
  for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x) {
      float dx = (x + 0.5f - px) / cv.sx, dy = (y + 0.5f - py) / cv.sy;
      const float lx = dx * cs + dy * sn, ly = -dx * sn + dy * cs;  // into the blob's frame
      const float u = std::hypot(lx / rx, ly / ry);
      float ang = std::atan2(ly / ry, lx / rx);
      if (ang < 0.f) ang += 2.f * kPi;
      const float f = ang / (2.f * kPi) * static_cast<float>(n);
      const int i0 = std::min(n - 1, static_cast<int>(f)), i1 = (i0 + 1) % n;
      const float fr = f - static_cast<float>(i0);
      const float edge = k[i0] + (k[i1] - k[i0]) * fr;
      float t = std::clamp((edge - u) / softN * 0.5f + 0.5f, 0.f, 1.f);
      t = t * t * (3.f - 2.f * t);
      if (t > 0.f) cv.blend(x, y, col, alpha * t);
    }
}

// Quadratic stroke in design units, AA, with optional dash pattern (6 lengths, units) and cap style.
struct Stroke {
  float x1, y1, mx, my, x2, y2;  // design units
  float width;                   // design units
  Rgb col;
  float alpha;
  const float* dash;             // 6 entries or nullptr
  bool roundCap;
  float ox, oy;                  // offset in design units
};

void strokeQuad(Canvas& cv, const Stroke& s) {
  const float wpx = std::max(s.width * cv.sx, cv.minLine);
  const float hw = wpx * 0.5f + 1.5f;
  auto P = [&](float t, float& x, float& y) {
    const float u = 1.f - t;
    x = (u * u * s.x1 + 2.f * u * t * s.mx + t * t * s.x2 + s.ox) * cv.sx;
    y = (u * u * s.y1 + 2.f * u * t * s.my + t * t * s.y2 + s.oy) * cv.sy;
  };
  const float approx = (std::hypot(s.mx - s.x1, s.my - s.y1) + std::hypot(s.x2 - s.mx, s.y2 - s.my)) * cv.sx;
  const int n = std::clamp(static_cast<int>(std::ceil(approx / 3.f)), 1, 160);
  float dashTotal = 0.f;
  if (s.dash)
    for (int i = 0; i < 6; ++i) dashTotal += s.dash[i];
  float ax, ay;
  P(0.f, ax, ay);
  float cum = 0.f;
  for (int i = 0; i < n; ++i) {
    float bx, by;
    P(static_cast<float>(i + 1) / static_cast<float>(n), bx, by);
    const float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy, len = std::sqrt(l2);
    if (l2 > 1e-9f) {
      const bool first = i == 0, last = i == n - 1;
      const int x0 = std::max(0, static_cast<int>(std::floor(std::min(ax, bx) - hw)));
      const int x1 = std::min(cv.w - 1, static_cast<int>(std::ceil(std::max(ax, bx) + hw)));
      const int y0 = std::max(0, static_cast<int>(std::floor(std::min(ay, by) - hw)));
      const int y1 = std::min(cv.h - 1, static_cast<int>(std::ceil(std::max(ay, by) + hw)));
      for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
          const float qx = x + 0.5f - ax, qy = y + 0.5f - ay;
          const float t = (qx * dx + qy * dy) / l2;
          if (t < 0.f && !(first && s.roundCap)) continue;
          if (t > 1.f && !(last && s.roundCap)) continue;
          if (t >= 1.f && !last) continue;
          const float tc = std::clamp(t, 0.f, 1.f);
          const float d = std::hypot(qx - tc * dx, qy - tc * dy);
          const float cov = lineCov(wpx, d);
          if (cov <= 0.f) continue;
          if (s.dash) {
            float u = std::fmod((cum + tc * len) / cv.sx, dashTotal);
            bool on = true;
            for (int k = 0; k < 6; ++k) {
              if (u < s.dash[k]) { on = (k % 2) == 0; break; }
              u -= s.dash[k];
            }
            if (!on) continue;
          }
          cv.blend(x, y, s.col, s.alpha * cov);
        }
    }
    cum += len;
    ax = bx;
    ay = by;
  }
}

// Anti-aliased polygon fill (4 sub-scanlines, exact horizontal coverage). Points in pixels.
constexpr int kMaxPoly = 48;
constexpr int kMaxRow = 4096;
void fillPoly(Canvas& cv, const float* xs, const float* ys, int n, const Rgb& col, float alpha, float ox, float oy) {
  if (n < 3 || n > kMaxPoly) return;
  float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
  for (int i = 0; i < n; ++i) {
    minx = std::min(minx, xs[i] + ox); maxx = std::max(maxx, xs[i] + ox);
    miny = std::min(miny, ys[i] + oy); maxy = std::max(maxy, ys[i] + oy);
  }
  const int bx0 = std::max(0, static_cast<int>(std::floor(minx))), bx1 = std::min(cv.w - 1, static_cast<int>(std::ceil(maxx)));
  const int by0 = std::max(0, static_cast<int>(std::floor(miny))), by1 = std::min(cv.h - 1, static_cast<int>(std::ceil(maxy)));
  if (bx1 < bx0 || by1 < by0 || bx1 - bx0 + 1 > kMaxRow) return;
  float cov[kMaxRow];
  for (int y = by0; y <= by1; ++y) {
    std::fill(cov, cov + (bx1 - bx0 + 1), 0.f);
    for (int sub = 0; sub < 4; ++sub) {
      const float sy = y + (sub + 0.5f) * 0.25f;
      float cx[kMaxPoly];
      int nc = 0;
      for (int i = 0; i < n; ++i) {
        const int j = (i + 1) % n;
        const float ya = ys[i] + oy, yb = ys[j] + oy;
        if ((ya <= sy) == (yb <= sy)) continue;
        cx[nc++] = xs[i] + ox + (sy - ya) / (yb - ya) * (xs[j] - xs[i]);
      }
      std::sort(cx, cx + nc);
      for (int c = 0; c + 1 < nc; c += 2) {
        const float a = std::max(cx[c], static_cast<float>(bx0)), b = std::min(cx[c + 1], static_cast<float>(bx1 + 1));
        if (b <= a) continue;
        const int ia = static_cast<int>(std::floor(a)), ib = static_cast<int>(std::floor(b));
        if (ia == ib) { cov[ia - bx0] += (b - a) * 0.25f; continue; }
        cov[ia - bx0] += (static_cast<float>(ia + 1) - a) * 0.25f;
        for (int x = ia + 1; x < ib; ++x) cov[x - bx0] += 0.25f;
        if (ib <= bx1) cov[ib - bx0] += (b - static_cast<float>(ib)) * 0.25f;
      }
    }
    for (int x = bx0; x <= bx1; ++x)
      if (cov[x - bx0] > 0.f) cv.blend(x, y, col, alpha * std::min(1.f, cov[x - bx0]));
  }
}

int count(float base, float amt) { return static_cast<int>(std::lround(base * amt)); }
float amount(Wear w) { return w == Wear::Light ? 1.f : w == Wear::Heavy ? 2.2f : 0.f; }

} // namespace

// ---------------------------------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------------------------------

WearLayout layoutFromSeed(uint32_t seed, Wear w, const Rect& panel) {
  WearLayout out;
  const float kx = panel.w / kWearDesignW, ky = panel.h / kWearDesignH;

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
    const int want = w == Wear::Off ? 0 : (w == Wear::Light ? 1 : 2) + extra;
    for (int t = 0; t < 3; ++t) {
      const TapeSlot& s = kTapeSlots[order[t]];
      const float jx = r.rnd(-35.f, 35.f), jy = r.rnd(-14.f, 14.f);
      const float tw = r.rnd(58.f, 96.f), th = r.rnd(19.f, 26.f), rot = r.rnd(-9.f, 9.f);
      if (t >= want) continue;
      TapePlacement p{panel.x + (s.cx + jx) * kx, panel.y + (s.cy + jy) * ky, tw * kx, th * kx, rot};
      const float rad = rot * kPi / 180.f, c = std::fabs(std::cos(rad)), sn = std::fabs(std::sin(rad));
      const float hx = 0.5f * (p.w * c + p.h * sn) + 1.f, hy = 0.5f * (p.w * sn + p.h * c) + 1.f;
      p.cx = std::clamp(p.cx, panel.x + hx, std::max(panel.x + hx, panel.x + panel.w - hx));
      p.cy = std::clamp(p.cy, panel.y + hy, std::max(panel.y + hy, panel.y + panel.h - hy));
      out.tapes.push_back(p);
    }
  }
  return out;
}

// ---------------------------------------------------------------------------------------------------
// Wear
// ---------------------------------------------------------------------------------------------------

void renderWear(uint8_t* rgba, int w, int h, int scale, uint32_t seed, Wear level) {
  if (!rgba || w <= 0 || h <= 0) return;
  std::memset(rgba, 0, static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
  const float amt = amount(level);
  if (amt <= 0.f) return;
  Canvas cv = makeCanvas(rgba, w, h, scale);
  Rng R(seed ^ 0x51u);

  // 1. Faint stains (blurred, warm or cool).
  for (int i = 0; i < count(6, amt); ++i) {
    const bool warm = R.next() < 0.45f;
    const float cx = R.rnd(40.f, 960.f), cy = R.rnd(100.f, 600.f), rx = R.rnd(25.f, 95.f), ry = R.rnd(12.f, 45.f);
    float k[9];
    for (float& v : k) v = R.rnd(0.55f, 1.15f);
    const float op = R.rnd(0.012f, 0.04f);
    softBlob(cv, cx, cy, rx, ry, k, 9, 10.f, warm ? hex(0xb99a5a) : hex(0xd8d8d2), op);
  }

  // 2. Stuck-on residue as dry-brush bundles of thin bristle marks.
  static constexpr uint32_t kBrush[] = {0x6a5a38, 0x54472d, 0x7a6a46, 0x8c8778, 0x9a8e6e, 0x5b3f2c, 0x47402f};
  for (int q = 0; q < count(9, amt); ++q) {
    const bool big = R.next() < 0.7f;
    const float cx0 = R.rnd(30.f, 970.f), cy0 = R.rnd(100.f, 600.f), dir = R.rnd(0.f, 2.f * kPi);
    const float len = big ? R.rnd(22.f, 70.f) : R.rnd(7.f, 16.f), width = big ? R.rnd(5.f, 13.f) : R.rnd(2.5f, 5.f);
    const int m = static_cast<int>(std::lround(width / R.rnd(0.55f, 0.9f)));
    const Rgb col = hex(kBrush[R.below(7)]);
    const float cs = std::cos(dir), sn = std::sin(dir);
    for (int i = 0; i < m; ++i) {
      const float t = (static_cast<float>(i) - static_cast<float>(m - 1) * 0.5f) / static_cast<float>(std::max(1, m - 1));
      const float off = t * width + R.rnd(-0.35f, 0.35f), edge = std::fabs(t) * 2.f;
      const float l = len * R.rnd(0.45f, 1.f) * (1.f - edge * 0.35f);
      const float s0 = -len / 2.f + R.rnd(0.f, len * 0.28f) + edge * len * 0.08f;
      const float x1 = cx0 - sn * off + cs * s0, y1 = cy0 + cs * off + sn * s0, x2 = x1 + cs * l, y2 = y1 + sn * l;
      const float bend = R.rnd(-1.6f, 1.6f);
      float dash[6];
      const bool dashed = R.next() < 0.55f;
      if (dashed) {
        dash[0] = R.rnd(3.f, 16.f); dash[1] = R.rnd(0.8f, 4.f); dash[2] = R.rnd(2.f, 9.f);
        dash[3] = R.rnd(1.f, 5.f);  dash[4] = R.rnd(6.f, 20.f); dash[5] = R.rnd(1.f, 6.f);
      }
      const bool dark = R.next() < 0.14f;
      const float sw = R.rnd(0.45f, 1.15f) * (1.f - edge * 0.3f);
      const bool rcap = R.next() >= 0.5f;
      strokeQuad(cv, {x1, y1, (x1 + x2) / 2.f - sn * bend, (y1 + y2) / 2.f + cs * bend, x2, y2, sw,
                      dark ? hex(0x16120a) : col, 1.f, dashed ? dash : nullptr, rcap, 0.f, 0.f});
    }
    const int loads = 1 + static_cast<int>(R.next() * 3.f);
    for (int hh = 0; hh < loads; ++hh) {
      const float ho = R.rnd(-width * 0.4f, width * 0.4f);
      const float hx = cx0 - len / 2.f * cs - sn * ho, hy = cy0 - len / 2.f * sn + cs * ho;
      const float hl = len * R.rnd(0.1f, 0.25f), hw = R.rnd(1.2f, 2.2f);
      strokeQuad(cv, {hx, hy, hx + cs * hl / 2.f, hy + sn * hl / 2.f, hx + cs * hl, hy + sn * hl, hw, col, 1.f, nullptr, true, 0.f, 0.f});
    }
  }

  // 3. Scratches: a dark edge and a light core.
  for (int j = 0; j < count(55, amt); ++j) {
    const float x = R.rnd(20.f, 980.f), y = R.rnd(20.f, 608.f);
    const float len = R.next() < 0.12f ? R.rnd(120.f, 260.f) : R.rnd(14.f, 90.f);
    const float ang = R.next() < 0.6f ? R.rnd(-0.35f, 0.35f) : R.rnd(0.f, 2.f * kPi), cur = R.rnd(-0.12f, 0.12f);
    const float x2 = x + std::cos(ang) * len, y2 = y + std::sin(ang) * len;
    const float mx = (x + x2) / 2.f + std::sin(ang) * len * cur, my = (y + y2) / 2.f - std::cos(ang) * len * cur;
    const float wd = R.rnd(0.35f, 0.9f), a1 = R.rnd(0.35f, 0.6f), a2 = R.rnd(0.08f, 0.26f);
    strokeQuad(cv, {x, y, mx, my, x2, y2, wd, hex(0x000000), a1, nullptr, true, 0.5f, 0.6f});
    strokeQuad(cv, {x, y, mx, my, x2, y2, wd, hex(0xd6d6d0), a2, nullptr, true, 0.f, 0.f});
  }

  // 4. Chips and specks.
  for (int c = 0; c < count(90, amt); ++c) {
    const float cx = R.rnd(10.f, 990.f), cy = R.rnd(10.f, 618.f), r = R.rnd(0.3f, 1.1f);
    const bool light = R.next() < 0.7f;
    disc(cv, cx, cy, r, light ? hex(0xcfcfc8) : hex(0x000000), R.rnd(0.1f, 0.4f));
  }

  // 5. Faint residue marks left by old tape.
  for (int m = 0; m < count(1.4f, amt); ++m) {
    const float rx = R.rnd(60.f, 880.f), ry = R.rnd(110.f, 560.f), rw = R.rnd(50.f, 90.f), rh = R.rnd(18.f, 26.f), rr = R.rnd(-9.f, 9.f);
    float k[14];
    for (float& v : k) v = R.rnd(0.55f, 1.15f);
    const float rad = rr * kPi / 180.f;
    softBlob(cv, rx + rw / 2.f, ry + rh / 2.f, rw / 2.f, rh / 2.f, k, 14, 2.5f, hex(0xb8a66b), 0.07f, rad);
    for (int d = 0; d < 10; ++d) {
      const float lx = R.rnd(2.f, rw - 2.f) - rw / 2.f, ly = R.rnd(2.f, rh - 2.f) - rh / 2.f;
      disc(cv, rx + rw / 2.f + lx * std::cos(rad) - ly * std::sin(rad), ry + rh / 2.f + lx * std::sin(rad) + ly * std::cos(rad),
           R.rnd(0.4f, 1.4f), hex(0x9d8c58), R.rnd(0.08f, 0.2f));
    }
  }
}

// ---------------------------------------------------------------------------------------------------
// Tape bodies
// ---------------------------------------------------------------------------------------------------

void renderTapes(uint8_t* rgba, int w, int h, int scale, uint32_t seed, Wear level) {
  if (!rgba || w <= 0 || h <= 0) return;
  std::memset(rgba, 0, static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
  if (level == Wear::Off) return;
  Canvas cv = makeCanvas(rgba, w, h, scale);
  const WearLayout lay = layoutFromSeed(seed, level, {0.f, 0.f, kWearDesignW, kWearDesignH});
  Rng R(seed ^ 0x7E11u);
  for (const TapePlacement& t : lay.tapes) {
    // Ragged outline (design units, local frame centred on the strip).
    float lx[kMaxPoly], ly[kMaxPoly];
    int n = 0;
    const float tw = t.w, th = t.h;
    lx[n] = 0.f; ly[n++] = R.rnd(0.f, 2.5f);
    for (int k = 1; k <= 4; ++k) { lx[n] = tw * k / 4.f; ly[n++] = R.rnd(-1.2f, 1.2f); }
    for (int q = 1; q <= 5; ++q) { lx[n] = tw + R.rnd(-3.f, 3.f); ly[n++] = th * q / 5.f; }
    for (int k = 3; k >= 0; --k) { lx[n] = tw * k / 4.f; ly[n++] = th + R.rnd(-1.2f, 1.2f); }
    for (int q = 4; q >= 1; --q) { lx[n] = R.rnd(-3.f, 3.f); ly[n++] = th * q / 5.f; }
    const float rad = t.rotDeg * kPi / 180.f, c = std::cos(rad), s = std::sin(rad);
    float xs[kMaxPoly], ys[kMaxPoly];
    for (int i = 0; i < n; ++i) {
      const float ux = lx[i] - tw / 2.f, uy = ly[i] - th / 2.f;
      xs[i] = (t.cx + ux * c - uy * s) * cv.sx;
      ys[i] = (t.cy + ux * s + uy * c) * cv.sy;
    }
    // Soft shadow: three slightly offset translucent copies.
    const float offs[3][2] = {{0.8f, 1.6f}, {1.3f, 1.9f}, {0.4f, 1.3f}};
    for (const auto& o : offs) fillPoly(cv, xs, ys, n, hex(0x000000), 0.2f, o[0] * cv.sx, o[1] * cv.sx);
    fillPoly(cv, xs, ys, n, hex(0xd9cfa6), 0.8f, 0.f, 0.f);
    // Paper fibres.
    for (int f = 0; f < 5; ++f) {
      const float a0 = R.rnd(0.f, tw * 0.3f), a1 = R.rnd(tw * 0.6f, tw), b0 = R.rnd(2.f, th - 2.f), b1 = R.rnd(2.f, th - 2.f);
      const float op = R.rnd(0.08f, 0.2f);
      auto tr = [&](float px, float py, float& ox, float& oy) {
        const float ux = px - tw / 2.f, uy = py - th / 2.f;
        ox = t.cx + ux * c - uy * s;
        oy = t.cy + ux * s + uy * c;
      };
      float p0x, p0y, p1x, p1y;
      tr(a0, b0, p0x, p0y);
      tr(a1, b1, p1x, p1y);
      strokeQuad(cv, {p0x, p0y, (p0x + p1x) / 2.f, (p0y + p1y) / 2.f, p1x, p1y, 0.5f, hex(0x8f8560), op, nullptr, false, 0.f, 0.f});
    }
  }
}

} // namespace FairyTapes::PillowTalk::Ui
