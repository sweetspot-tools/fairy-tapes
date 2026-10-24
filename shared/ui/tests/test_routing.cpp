/** Fairy Tapes · shared UI — unit tests for the ROUTING diagram builder (no VSTGUI). */
#include "fairy_ui/routing.h"

#include <cmath>
#include <cstdio>

using namespace FairyTapes::Ui;

static int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)
static bool near(double a, double b) { return std::fabs(a - b) < 1e-6; }

static int count(const RoutingDiagram& d, RoutingPrim::Kind k) {
  int n = 0;
  for (const auto& p : d) n += p.kind == k ? 1 : 0;
  return n;
}

int main() {
  // paths: M starts a polyline, H and V move along one axis, L draws to a point
  {
    const auto polys = parseRoutingPath("M69 200 H100 M82 200 V128 H958 V300");
    CHECK(polys.size() == 2);
    CHECK(polys[0].size() == 2 && near(polys[0][1].x, 100) && near(polys[0][1].y, 200));
    CHECK(polys[1].size() == 4 && near(polys[1][1].y, 128) && near(polys[1][2].x, 958) && near(polys[1][3].y, 300));
    const auto lever = parseRoutingPath("M902 392 L888.5 391");
    CHECK(lever.size() == 1 && lever[0].size() == 2 && near(lever[0][1].x, 888.5));
    CHECK(parseRoutingPath("").empty() && parseRoutingPath("Q 1 2").empty());
  }
  // a block: its rectangle centred on the signal line, an input arrow, the name and the amber tags
  {
    RoutingBuilder b;
    b.block(194, 304, 200, 48, "FILTERS", {"HP · LP"});
    const auto& d = b.diagram();
    CHECK(count(d, RoutingPrim::Kind::Rect) == 1 && count(d, RoutingPrim::Kind::Polygon) == 1 && count(d, RoutingPrim::Kind::Text) == 2);
    CHECK(near(d[0].y, 176) && near(d[0].h, 48) && near(d[0].w, 110));
    CHECK(d.back().ink == RoutingInk::Amber && d.back().text == "HP · LP" && near(d.back().x, 249));
  }
  // an IN switch: three contacts, the lever, the jump line up to `top` and back down to the rejoin dot, the label
  {
    RoutingBuilder b;
    b.inSwitch(178, 200, 322, 150);
    const auto& d = b.diagram();
    CHECK(count(d, RoutingPrim::Kind::Circle) == 4);   // three rings and the rejoin dot
    bool jump = false;
    for (const auto& p : d)
      if (p.kind == RoutingPrim::Kind::Line && p.pts.size() == 4)
        jump = near(p.pts[1].y, 150) && near(p.pts[2].x, 322) && near(p.pts[3].y, 200);
    CHECK(jump);
    CHECK(d.back().kind == RoutingPrim::Kind::Text && d.back().text == "IN" && near(d.back().y, 144));
  }
  // the sum mark: a ring (default radius 9) with a plus inside, two lines of width 1.8
  {
    RoutingBuilder b;
    b.sum(262, 300);
    const auto& d = b.diagram();
    CHECK(d.size() == 3 && count(d, RoutingPrim::Kind::Circle) == 1 && count(d, RoutingPrim::Kind::Line) == 2);
    CHECK(near(d[0].x, 262) && near(d[0].y, 300) && near(d[0].r, 9) && !d[0].filled && d[0].panelFill && near(d[0].width, 1.4));
    CHECK(d[1].pts.size() == 2 && near(d[1].pts[0].x, 257) && near(d[1].pts[1].x, 267) && near(d[1].pts[0].y, 300) && near(d[1].width, 1.8));
    CHECK(d[2].pts.size() == 2 && near(d[2].pts[0].y, 295) && near(d[2].pts[1].y, 305) && near(d[2].pts[0].x, 262) && near(d[2].width, 1.8));
    RoutingBuilder c;
    c.sum(100, 50, 12);
    CHECK(near(c.diagram()[0].r, 12) && near(c.diagram()[1].pts[0].x, 100 - 12 * 5.0 / 9.0) && near(c.diagram()[2].pts[1].y, 50 + 12 * 5.0 / 9.0));
  }
  // control lines are dashed
  {
    RoutingBuilder b;
    b.ln("M69 290 H680 V232", true);
    CHECK(b.diagram().size() == 1 && b.diagram()[0].dashed);
  }
  // legend: one symbol and its label per entry, then the notes
  {
    RoutingLegend l{{LegendSymbol::Module, LegendSymbol::Connector}, "meter / clip point", {"one note"}};
    const auto d = legendDiagram(l, 552, 592);
    CHECK(count(d, RoutingPrim::Kind::Text) == 4);   // module, A, continues, the note
    CHECK(d.back().text == "one note" && near(d.back().y, 592) && d.back().align == RoutingAlign::Start);
  }
  if (g_failed) return 1;
  std::puts("test_routing: ok");
  return 0;
}
