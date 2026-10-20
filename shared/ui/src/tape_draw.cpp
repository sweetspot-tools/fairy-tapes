// Fairy Tapes shared UI: paper tape and handwriting from the vector tape fonts (first written for The Stripper).
#include "fairy_ui/tape_draw.h"

#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cgraphicstransform.h"

#include <cmath>

namespace FairyTapes::Ui {

using namespace VSTGUI;

double drawTapeText(CDrawContext* ctx, TapeFontId font, const std::string& utf8, const CPoint& origin, double emPx,
                    const CColor& colour, std::uint32_t seed, double variation, TapeAlign align) {
  const auto placed = layoutText(font, utf8, emPx, seed, variation);
  if (placed.empty()) return 0.0;
  const TapeFontData& data = tapeFontData(font);
  const double width = placed.back().x + placed.back().glyph->advance / 1000.0 * emPx;
  const double left = align == TapeAlign::Left ? origin.x : align == TapeAlign::Center ? origin.x - width / 2 : origin.x - width;
  constexpr double kPi = 3.14159265358979323846;

  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
  ctx->setFillColor(colour);
  for (const PlacedGlyph& p : placed) {
    const TapeGlyph& g = *p.glyph;
    if (g.count == 0) continue;   // the space
    auto path = owned(ctx->createGraphicsPath());
    if (!path) continue;
    const double s = emPx * p.scale / 1000.0, a = p.angleDeg * kPi / 180.0, ca = std::cos(a), sa = std::sin(a);
    const double ox = left + p.x, oy = origin.y + p.y;
    auto map = [&](int u, int v) {   // font units (y up) -> editor px, tilted around the glyph origin
      const double lx = u * s, ly = -v * s;
      return CPoint(ox + lx * ca - ly * sa, oy + lx * sa + ly * ca);
    };
    const std::int16_t* st = data.stream + g.offset;
    CPoint cur;
    for (std::uint32_t i = 0; i < g.count;) {
      switch (st[i]) {
      case 0: cur = map(st[i + 1], st[i + 2]); path->beginSubpath(cur); i += 3; break;
      case 1: cur = map(st[i + 1], st[i + 2]); path->addLine(cur); i += 3; break;
      case 2: {   // quadratic (control c, end e) as the cubic VSTGUI draws
        const CPoint c = map(st[i + 1], st[i + 2]), e = map(st[i + 3], st[i + 4]);
        path->addBezierCurve(CPoint(cur.x + 2.0 / 3.0 * (c.x - cur.x), cur.y + 2.0 / 3.0 * (c.y - cur.y)),
                             CPoint(e.x + 2.0 / 3.0 * (c.x - e.x), e.y + 2.0 / 3.0 * (c.y - e.y)), e);
        cur = e;
        i += 5;
        break;
      }
      default: path->closeSubpath(); i += 1; break;
      }
    }
    ctx->drawGraphicsPath(path, CDrawContext::kPathFilled);
  }
  ctx->restoreGlobalState();
  return width;
}

void drawTapePaper(CDrawContext* ctx, const TapePaper& paper, const CRect& t, double rotationDeg) {
  if (!ctx || paper.outline.size() < 3) return;
  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
  CGraphicsTransform tilt;
  tilt.rotate(rotationDeg, t.getCenter());
  {
    CDrawContext::Transform transform(*ctx, tilt);
    if (auto outline = owned(ctx->createGraphicsPath())) {
      outline->beginSubpath(CPoint(t.left + paper.outline[0].x, t.top + paper.outline[0].y));
      for (std::size_t i = 1; i < paper.outline.size(); ++i)
        outline->addLine(CPoint(t.left + paper.outline[i].x, t.top + paper.outline[i].y));
      outline->closeSubpath();
      CGraphicsTransform shift;
      shift.translate(0.8, 1.6);
      ctx->setFillColor(kTapeShadow);
      ctx->drawGraphicsPath(outline, CDrawContext::kPathFilled, &shift);
      ctx->setFillColor(kTapePaper);
      ctx->drawGraphicsPath(outline, CDrawContext::kPathFilled);
    }
    ctx->setLineWidth(0.5);
    for (const auto& f : paper.fibres) {
      ctx->setFrameColor(CColor(kTapeFibre.red, kTapeFibre.green, kTapeFibre.blue, static_cast<uint8_t>(f.alpha * 255)));
      ctx->drawLine(CPoint(t.left + f.x1, t.top + f.y1), CPoint(t.left + f.x2, t.top + f.y2));
    }
  }
  ctx->restoreGlobalState();
}

} // namespace FairyTapes::Ui
