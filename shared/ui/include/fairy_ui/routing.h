/**
 * Fairy Tapes shared UI — the ROUTING tab of the back (docs/DESIGN_SYSTEM.md, "Scheda ROUTING"): a fixed block diagram of
 * the signal path in the style of the outboard manuals. A plug-in describes its diagram with RoutingBuilder, whose calls
 * mirror the RT helpers of the previews (stripper_rack.html, pillow_rack.html: ln, arrow, dot, ring, jack, amp, block,
 * inSwitch, tap, conn, txt), so the code is a transcription of the approved drawing; the builder expands every call
 * into plain primitives (lines, circles, rectangles, polygons, text) that BackView draws.
 *
 * Header-only and free of VSTGUI so it is unit tested (tests/test_routing.cpp). Coordinates are editor px of the panel.
 */
#pragma once

#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace FairyTapes::Ui {

struct RoutingPt {
  double x = 0, y = 0;
};

enum class RoutingInk { White, Dim, Amber };
enum class RoutingAlign { Start, Middle, End };

struct RoutingPrim {
  enum class Kind { Line, Circle, Rect, Polygon, Text };
  Kind kind = Kind::Line;
  std::vector<RoutingPt> pts;   // Line: polyline; Polygon: closed outline
  double x = 0, y = 0, w = 0, h = 0, r = 0;   // Circle: centre and r; Rect: box; Text: anchor (baseline)
  double width = 2;             // stroke width (Line, Polygon outline, Circle and Rect outline)
  bool dashed = false;          // Line: control signal (6 on, 5 off)
  bool filled = false;          // Circle / Polygon: filled with the ink; a ring (Circle, not filled) is filled with the panel
  bool panelFill = false;       // Circle: filled with the panel colour under the outline (switch contacts)
  RoutingInk ink = RoutingInk::White;
  std::string text;             // Text
  double size = 10;             // Text: font size (bold)
  RoutingAlign align = RoutingAlign::Middle;
};

using RoutingDiagram = std::vector<RoutingPrim>;

// Parses the absolute SVG path commands used by the previews (M x y, L x y, H x, V y; numbers separated by spaces) into
// polylines, one per M. Anything else ends the parse.
inline std::vector<std::vector<RoutingPt>> parseRoutingPath(const std::string& d) {
  std::vector<std::vector<RoutingPt>> out;
  const char* p = d.c_str();
  RoutingPt cur;
  auto skip = [&] { while (*p == ' ' || *p == ',') ++p; };
  auto num = [&](double& v) {
    skip();
    char* end = nullptr;
    v = std::strtod(p, &end);
    if (end == p) return false;
    p = end;
    return true;
  };
  for (;;) {
    skip();
    const char c = *p;
    if (c == '\0') break;
    ++p;
    if (c == 'M' || c == 'L') {
      RoutingPt q;
      if (!num(q.x) || !num(q.y)) break;
      if (c == 'M' || out.empty()) out.push_back({});
      if (c == 'L' && out.back().empty()) out.back().push_back(cur);
      out.back().push_back(q);
      cur = q;
    } else if (c == 'H' || c == 'V') {
      double v = 0;
      if (!num(v) || out.empty()) break;
      if (c == 'H') cur.x = v;
      else cur.y = v;
      out.back().push_back(cur);
    } else {
      break;
    }
  }
  return out;
}

class RoutingBuilder {
public:
  const RoutingDiagram& diagram() const { return out_; }
  RoutingDiagram take() { return std::move(out_); }

  // ln(d, dash, w): solid white lines, or dashed control lines
  void ln(const std::string& d, bool dashed = false, double width = 2) {
    for (auto& poly : parseRoutingPath(d)) {
      RoutingPrim p;
      p.kind = RoutingPrim::Kind::Line;
      p.pts = std::move(poly);
      p.dashed = dashed;
      p.width = width;
      out_.push_back(std::move(p));
    }
  }
  // Filled arrow heads: the tip at (x, y)
  void arrow(double x, double y) { poly({{x - 7, y - 4}, {x, y}, {x - 7, y + 4}}, true); }
  void arrowLeft(double x, double y) { poly({{x + 7, y - 4}, {x, y}, {x + 7, y + 4}}, true); }
  void arrowUp(double x, double y) { poly({{x - 4, y + 7}, {x, y}, {x + 4, y + 7}}, true); }
  void arrowDown(double x, double y) { poly({{x - 4, y - 7}, {x, y}, {x + 4, y - 7}}, true); }
  void dot(double x, double y) { circle(x, y, 3, true, false, 0); }
  void ring(double x, double y, double r = 2.6) { circle(x, y, r, false, true, 1.4); }
  // The "sum" mark (a mixing point): a ring of radius r with a plus inside (arms 5/9 of r, so 5 px at the default 9).
  void sum(double x, double y, double r = 9) {
    ring(x, y, r);
    const double a = r * 5.0 / 9.0;
    ln("M" + n(x - a) + " " + n(y) + " H" + n(x + a) + " M" + n(x) + " " + n(y - a) + " V" + n(y + a), false, 1.8);
  }
  void jack(double x, double y, const std::string& label) {
    circle(x, y, 9, false, false, 2);
    circle(x, y, 3.2, true, false, 0);
    txt(label, x, y + 24, 10, RoutingInk::White);
  }
  void amp(double x0, double y, const std::string& label) {
    poly({{x0, y - 14}, {x0 + 28, y}, {x0, y + 14}}, false);
    if (!label.empty()) txt(label, x0 + 12, y + 30, 10, RoutingInk::White);
  }
  // A module: the name of the front panel and its tags (amber) centred in the box; an arrow on the input side.
  void block(double x0, double x1, double y, double h, const std::string& name, const std::vector<std::string>& tags = {},
             bool arrowIn = true) {
    RoutingPrim r;
    r.kind = RoutingPrim::Kind::Rect;
    r.x = x0;
    r.y = y - h / 2;
    r.w = x1 - x0;
    r.h = h;
    out_.push_back(std::move(r));
    if (arrowIn) arrow(x0, y);
    const double ty = y + 4 - static_cast<double>(tags.size()) * 5.5;
    txt(name, (x0 + x1) / 2, ty, 11, RoutingInk::White);
    for (std::size_t i = 0; i < tags.size(); ++i) txt(tags[i], (x0 + x1) / 2, ty + 13 + 11 * static_cast<double>(i), 9, RoutingInk::Amber);
  }
  // IN switch in its default position (module in) and the line that jumps the module, rejoining at rx.
  void inSwitch(double px, double y, double rx, double top, const std::string& label = "IN") {
    ring(px, y);
    ring(px + 12, y);
    ring(px + 12, y - 14);
    ln("M" + n(px) + " " + n(y) + " L" + n(px + 11) + " " + n(y - 2), false, 1.8);
    ln("M" + n(px + 12) + " " + n(y - 16.6) + " V" + n(top) + " H" + n(rx) + " V" + n(y));
    dot(rx, y);
    txt(label, (px + rx) / 2, top - 6, 9, RoutingInk::Dim);
  }
  // A meter or clip point: a branch down from the signal line, with its labels.
  void tap(double x, double y, double len, const std::string& l1, const std::string& l2 = "") {
    dot(x, y);
    ln("M" + n(x) + " " + n(y) + " V" + n(y + len), false, 1.4);
    dot(x, y + len);
    txt(l1, x, y + len + 14, 9, RoutingInk::White);
    if (!l2.empty()) txt(l2, x, y + len + 25, 9, RoutingInk::Dim);
  }
  // Connector to the next line of the diagram.
  void conn(double x, double y, const std::string& key) {
    circle(x, y, 10, false, false, 2);
    txt(key, x, y + 4, 11, RoutingInk::White);
  }
  void txt(const std::string& s, double x, double y, double size, RoutingInk ink, RoutingAlign align = RoutingAlign::Middle) {
    RoutingPrim t;
    t.kind = RoutingPrim::Kind::Text;
    t.text = s;
    t.x = x;
    t.y = y;
    t.size = size;
    t.ink = ink;
    t.align = align;
    out_.push_back(std::move(t));
  }
  void rect(double x, double y, double w, double h) {
    RoutingPrim r;
    r.kind = RoutingPrim::Kind::Rect;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    out_.push_back(std::move(r));
  }

private:
  static std::string n(double v) {
    std::string s = std::to_string(v);
    return s;
  }
  void poly(std::vector<RoutingPt> pts, bool filled) {
    RoutingPrim p;
    p.kind = RoutingPrim::Kind::Polygon;
    p.pts = std::move(pts);
    p.filled = filled;
    out_.push_back(std::move(p));
  }
  void circle(double x, double y, double r, bool filled, bool panelFill, double width) {
    RoutingPrim c;
    c.kind = RoutingPrim::Kind::Circle;
    c.x = x;
    c.y = y;
    c.r = r;
    c.filled = filled;
    c.panelFill = panelFill;
    c.width = width;
    out_.push_back(std::move(c));
  }

  RoutingDiagram out_;
};

// ---- LEGEND -------------------------------------------------------------------------------------------------------------
// The symbols a diagram uses, left to right at the x of the previews; the note lines under them.
enum class LegendSymbol { Module, GainStage, Switch, Jack, Control, MeterPoint, Connector };
struct RoutingLegend {
  std::vector<LegendSymbol> symbols;
  std::string meterLabel = "meter / clip point";
  std::vector<std::string> notes;   // at most two lines
};

inline RoutingDiagram legendDiagram(const RoutingLegend& l, double y, double noteBaseline) {
  RoutingBuilder b;
  for (LegendSymbol s : l.symbols) {
    switch (s) {
    case LegendSymbol::Module:
      b.rect(50, y - 12, 34, 24);
      b.txt("module", 92, y + 4, 10, RoutingInk::Dim, RoutingAlign::Start);
      break;
    case LegendSymbol::GainStage:
      b.ln("M160 " + std::to_string(y - 11) + " L182 " + std::to_string(y) + " L160 " + std::to_string(y + 11) + " L160 " +
           std::to_string(y - 11));
      b.txt("gain stage", 190, y + 4, 10, RoutingInk::Dim, RoutingAlign::Start);
      break;
    case LegendSymbol::Switch:
      b.ring(268, y);
      b.ring(280, y);
      b.ring(280, y - 14);
      b.ln("M268 " + std::to_string(y) + " L279 " + std::to_string(y - 2), false, 1.8);
      b.txt("switch, default position", 290, y + 4, 10, RoutingInk::Dim, RoutingAlign::Start);
      break;
    case LegendSymbol::Jack:
      b.jack(438, y, "");
      b.txt("input / output", 454, y + 4, 10, RoutingInk::Dim, RoutingAlign::Start);
      break;
    case LegendSymbol::Control:
      b.ln("M552 " + std::to_string(y) + " H600", true);
      b.txt("control signal", 608, y + 4, 10, RoutingInk::Dim, RoutingAlign::Start);
      break;
    case LegendSymbol::MeterPoint:
      b.dot(712, y);
      b.ln("M712 " + std::to_string(y) + " V" + std::to_string(y - 12), false, 1.4);
      b.txt(l.meterLabel, 722, y + 4, 10, RoutingInk::Dim, RoutingAlign::Start);
      break;
    case LegendSymbol::Connector:
      b.conn(858, y, "A");
      b.txt("continues", 874, y + 4, 10, RoutingInk::Dim, RoutingAlign::Start);
      break;
    }
  }
  for (std::size_t i = 0; i < l.notes.size(); ++i)
    b.txt(l.notes[i], 50, noteBaseline + 16 * static_cast<double>(i), 11, RoutingInk::Dim, RoutingAlign::Start);
  return b.take();
}

// The routing tab of a plug-in: the SIGNAL FLOW diagram and its legend.
struct BackRouting {
  RoutingDiagram flow;
  RoutingLegend legend;
};

// Bounding box of a primitive (text: its anchor only, width unknown without a font).
inline void routingExtent(const RoutingPrim& p, double& x0, double& y0, double& x1, double& y1) {
  switch (p.kind) {
  case RoutingPrim::Kind::Line:
  case RoutingPrim::Kind::Polygon:
    x0 = y0 = 1e9;
    x1 = y1 = -1e9;
    for (const auto& q : p.pts) {
      x0 = std::min(x0, q.x);
      y0 = std::min(y0, q.y);
      x1 = std::max(x1, q.x);
      y1 = std::max(y1, q.y);
    }
    break;
  case RoutingPrim::Kind::Circle: x0 = p.x - p.r; y0 = p.y - p.r; x1 = p.x + p.r; y1 = p.y + p.r; break;
  case RoutingPrim::Kind::Rect: x0 = p.x; y0 = p.y; x1 = p.x + p.w; y1 = p.y + p.h; break;
  case RoutingPrim::Kind::Text: x0 = x1 = p.x; y0 = p.y - p.size; y1 = p.y; break;
  }
}

} // namespace FairyTapes::Ui
