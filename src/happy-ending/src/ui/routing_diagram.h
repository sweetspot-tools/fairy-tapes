/**
 * Fairy Tapes · Happy Ending — the ROUTING tab of the back (docs/DESIGN_SYSTEM.md, "Scheda ROUTING"; spec §7), transcribed call
 * by call from the preview (docs/mockups/happy_ending_rack.html, ROUTING tab, the RT helpers) in the order of the audio
 * engine (engine.cpp): the input gain and the IN meter, FINE EQ (M/S encoder, the bands on ST, MID or SIDE, decoder), VIBE EQ
 * with the DYN detector (dashed) on LOW and AIR, GLUE in parallel (the DRY path and the MIX node; the feedback detector, the
 * compressed output through SC HPF, dashed) on the first line; CLIP, LIMIT, FINAL (INFLATE or CLIP in parallel with a
 * selector), METALHEAD (its IN drawn off, as by default), the output gain, the safety limiter and the ceiling guard, OUTPUT
 * on the second. The global BYPASS goes from the input jack to the output switch. Dashed control lines: CEILING to LIMIT,
 * FINAL, METALHEAD and the safety stage, the LOUDNESS macro to CLIP, LIMIT and FINAL, MATCH from the LUFS meter to LOUDNESS.
 * Measure points (small dots on a branch): IN and OUT meters and the GR / clip points of every stage. Fixed drawing: it does
 * not follow the state of the buttons. Pure, unit tested (tests/test_ui.cpp: the order, the box, the sources).
 *
 * The shared view draws every line in white at the width the primitive carries, and rectangles with square-ish corners at 2 px
 * (fairy_ui/back_view.cpp): the preview's faint boxes (FINAL's two alternatives, the oversampled zone) are drawn here as thin
 * lines, dashed for the zone, but in white. The abbreviations in amber are the block tags of the front (layout.h: engineTag);
 * the sources are named in CREDITS only.
 */
#pragma once

#include "params.h"
#include "ui/layout.h"
#include "ui/readouts.h"

#include "fairy_ui/routing.h"

#include <cmath>
#include <string>

namespace FairyTapes::HappyEnding::Ui {

inline FairyTapes::Ui::RoutingDiagram happyEndingRouting() {
  using FairyTapes::Ui::RoutingAlign;
  using FairyTapes::Ui::RoutingInk;
  FairyTapes::Ui::RoutingBuilder b;
  auto s = [](double v) { return std::to_string(v); };
  const double Y1 = 250, Y2 = 500;
  const std::string y1 = s(Y1), y2 = s(Y2);
  const std::string dot = "  \xC2\xB7  ", mid = " \xC2\xB7 ";   // " · " between words
  auto vertical = [&](double x, double y0, double y1v, bool dashed) { b.ln("M" + s(x) + " " + s(y0) + " V" + s(y1v), dashed); };

  // ---- row 1: input, FINE EQ (mid / side), VIBE EQ, GLUE ----
  b.jack(60, Y1, "IN L" + mid + "R");
  b.ln("M69 " + y1 + " H100");
  b.dot(82, Y1);
  b.amp(100, Y1, "INPUT");
  b.ln("M128 " + y1 + " H178");
  b.tap(150, Y1, 50, "IN METER", "CLIP IN");
  // FINE EQ: M/S encoder, the bands on ST, MID or SIDE, decoder
  b.ln("M190 " + y1 + " H194");
  b.block(194, 244, Y1, 48, "M/S", {"ENC"});
  b.ln("M244 " + s(Y1 - 12) + " H270 M244 " + s(Y1 + 12) + " H270");
  b.arrow(270, Y1 - 12);
  b.arrow(270, Y1 + 12);
  b.txt("M", 257, Y1 - 16, 9, RoutingInk::Dim);
  b.txt("S", 257, Y1 + 26, 9, RoutingInk::Dim);
  b.block(270, 420, Y1, 64, "FINE EQ", {engineTag(Box::FineEq, 0), "ST" + mid + "MID" + mid + "SIDE per band"}, false);
  b.ln("M420 " + s(Y1 - 12) + " H446 M420 " + s(Y1 + 12) + " H446");
  b.arrow(446, Y1 - 12);
  b.arrow(446, Y1 + 12);
  b.block(446, 496, Y1, 48, "M/S", {"DEC"}, false);
  b.inSwitch(178, Y1, 514, Y1 - 50);
  b.ln("M496 " + y1 + " H576");
  b.tap(524, Y1, 50, "CLIP FEQ");
  // VIBE EQ, with the DYN detector (control line) on LOW and AIR
  b.ln("M588 " + y1 + " H592");
  b.block(592, 732, Y1, 48, "VIBE EQ", {engineTag(Box::VibeEq, 0), "LOW" + mid + "AIR" + mid + "TILT" + mid + "TUBE"});
  b.inSwitch(576, Y1, 750, Y1 - 50);
  b.dot(562, Y1);
  b.ln("M562 " + y1 + " V" + s(Y1 + 90) + " H620", true);
  b.block(620, 680, Y1 + 90, 24, "DYN", {});
  b.ln("M680 " + s(Y1 + 90) + " H700 V" + s(Y1 + 31), true);
  b.arrowUp(700, Y1 + 25);
  b.txt("LOW" + mid + "AIR", 650, Y1 + 118, 9, RoutingInk::Dim);
  b.txt("TUBE OVERSAMPLED", 650, Y1 + 44, 8, RoutingInk::Dim);
  b.ln("M750 " + y1 + " H796");
  b.tap(770, Y1, 50, "CLIP VEQ");
  // GLUE: compressor in parallel (the dry path and MIX); the feedback detector hears the compressed output through SC HPF
  b.ln("M808 " + y1 + " H846");
  b.dot(820, Y1);
  b.ln("M820 " + y1 + " V" + s(Y1 + 70) + " H1020 V" + s(Y1 + 9));
  b.arrowUp(1020, Y1 + 9);
  b.txt("DRY", 990, Y1 + 64, 9, RoutingInk::Dim);
  b.block(846, 996, Y1, 48, "GLUE", {engineTag(Box::Glue, 0), "RATIO" + mid + "WARM"});
  b.ln("M996 " + y1 + " H1011");
  b.sum(1020, Y1);
  b.txt("MIX", 1020, Y1 - 15, 9, RoutingInk::Dim);
  b.ln("M1029 " + y1 + " H1046");
  b.inSwitch(796, Y1, 1046, Y1 - 50);
  b.dot(1004, Y1);
  b.ln("M1004 " + y1 + " V" + s(Y1 + 46) + " H943", true);
  b.arrowLeft(936, Y1 + 46);
  b.block(876, 936, Y1 + 46, 22, "SC HPF", {}, false);
  b.ln("M876 " + s(Y1 + 46) + " H860 V" + s(Y1 + 31), true);
  b.arrowUp(860, Y1 + 25);
  b.ln("M1046 " + y1 + " H1190");
  b.tap(1070, Y1, 50, "GR G", "CLIP GLU");
  b.arrow(1190, Y1);
  b.conn(1200, Y1, "A");
  // global BYPASS: from the input jack straight to the output switch
  b.ln("M82 " + y1 + " V140 H1236 V" + s(Y2 - 100) + " H1062 V" + s(Y2 - 16.6));
  b.txt("BYPASS", 660, 134, 9, RoutingInk::Dim);
  b.txt("output = input, bit for bit", 1228, Y2 - 108, 9, RoutingInk::Dim, RoutingAlign::End);

  // ---- row 2: clip, limit, final (inflate or clip), metalhead, output ----
  b.conn(60, Y2, "A");
  b.ln("M70 " + y2 + " H90");
  b.ln("M102 " + y2 + " H106");
  b.block(106, 216, Y2, 48, "CLIP", {engineTag(Box::Clip, 0), "level matched"});
  b.inSwitch(90, Y2, 234, Y2 - 50);
  b.ln("M216 " + y2 + " H280");
  b.tap(256, Y2, 52, "GR C", "CLIP CLP");
  b.ln("M292 " + y2 + " H296");
  b.block(296, 426, Y2, 48, "LIMIT", {"look-ahead", "MODERN" + mid + "PUNCH" + mid + "DYN"});
  b.inSwitch(280, Y2, 444, Y2 - 50);
  b.ln("M426 " + y2 + " H490");
  b.tap(466, Y2, 52, "GR L", "CLIP LIM");
  // FINAL: INFLATE or CLIP, a selector in front and the two paths joined
  b.inSwitch(490, Y2, 692, Y2 - 70);
  b.ln("M502 " + y2 + " H518");
  b.ring(518, Y2);
  b.ring(534, Y2 - 30);
  b.ring(534, Y2 + 30);
  b.ln("M518 " + y2 + " L533 " + s(Y2 - 28), false, 1.8);
  b.ln("M534 " + s(Y2 - 32.6) + " V" + s(Y2 - 30) + " M536.6 " + s(Y2 - 30) + " H544");
  b.ln("M536.6 " + s(Y2 + 30) + " H544");
  b.block(544, 664, Y2 - 30, 36, "INFLATE", {engineTag(Box::Final, 0)});
  b.block(544, 664, Y2 + 30, 36, "CLIP", {engineTag(Box::Final, 1)});
  b.txt("FINAL", 604, Y2 - 56, 10, RoutingInk::White);
  b.ln("M538 " + s(Y2 - 52) + " H672 V" + s(Y2 + 58) + " H538 V" + s(Y2 - 52), false, 1.2);   // the faint box of the preview
  b.ln("M664 " + s(Y2 - 30) + " H678 V" + s(Y2 + 30) + " H664");
  b.dot(678, Y2);
  b.ln("M678 " + y2 + " H738");
  b.tap(714, Y2, 52, "GR F", "CLIP FIN");
  // METALHEAD: its IN switch drawn off (the lever on the jump), as the plug-in starts
  b.ln("M750 " + y2 + " H754");
  b.block(754, 884, Y2, 48, "METALHEAD", {"brickwall"});
  b.ring(738, Y2);
  b.ring(750, Y2);
  b.ring(750, Y2 - 14);
  b.ln("M738 " + y2 + " L749 " + s(Y2 - 12), false, 1.8);
  b.ln("M750 " + s(Y2 - 16.6) + " V" + s(Y2 - 50) + " H902 V" + y2);
  b.dot(902, Y2);
  b.txt("IN" + mid + "OFF BY DEFAULT", (738 + 902) / 2.0, Y2 - 50 - 6, 9, RoutingInk::Dim);
  b.ln("M884 " + y2 + " H948");
  b.tap(914, Y2, 52, "GR M", "CLIP MTL");
  // the zone the OVERSAMPLING setting works on (CLIP to METALHEAD)
  b.ln("M84 " + s(Y2 - 96) + " H936 V" + s(Y2 + 88) + " H84 V" + s(Y2 - 96), true, 1);
  b.txt("OVERSAMPLED ZONE" + dot + "OFF / 2\xC3\x97 / 4\xC3\x97", 96, Y2 - 84, 8, RoutingInk::Dim, RoutingAlign::Start);
  b.amp(948, Y2, "OUTPUT");
  b.ln("M976 " + y2 + " H982");
  // the safety limiter and the ceiling guard, on when LIMIT, FINAL or METALHEAD is on
  b.block(982, 1054, Y2, 36, "SAFETY", {"LIM \xC2\xB7 GUARD"});
  b.ln("M1054 " + y2 + " H1060");
  b.ring(1062, Y2);
  b.ring(1062, Y2 - 14);
  b.ring(1078, Y2);
  b.ln("M1078 " + y2 + " L1064.5 " + s(Y2 - 1), false, 1.8);
  b.ln("M1080.6 " + y2 + " H1213");
  b.tap(1110, Y2, 52, "OUT METER", "CLIP OUT");
  b.jack(1222, Y2, "OUT L" + mid + "R");

  // ---- control lines: CEILING to every stage that holds the peaks, the LOUDNESS macro, MATCH from the LUFS meter ----
  const double CY = Y2 + 150, LY = Y2 + 190, MY = Y2 + 240;
  b.block(940, 1040, CY, 36, "CEILING", {detail::oneDecimal(Tables::kCeilings.front(), false) + " \xE2\x80\xA6 " + detail::oneDecimal(Tables::kCeilings.back(), false) + " dB"}, false);
  {   // from CEILING to the left, a hop over the LOUDNESS line at x 574 (the preview's arc, radius 6, up)
    std::string d = "M940 " + s(CY) + " H580";
    const double pi = std::acos(-1.0);
    for (int k = 1; k <= 12; ++k) d += " L" + s(574 + 6 * std::cos(pi * k / 12)) + " " + s(CY - 6 * std::sin(pi * k / 12));
    d += " H400";
    b.ln(d, true);
  }
  const double ceilingTargets[3][2] = {{400, Y2 + 24}, {634, Y2 + 58}, {819, Y2 + 24}};
  for (const auto& t : ceilingTargets) {
    vertical(t[0], CY, t[1] + 7, true);
    b.arrowUp(t[0], t[1]);
  }
  b.txt("CEILING", 700, CY - 6, 9, RoutingInk::Dim);
  vertical(1018, CY - 18, Y2 + 25, true);
  b.arrowUp(1018, Y2 + 18);
  b.block(60, 170, LY, 36, "LOUDNESS", {"macro"}, false);
  vertical(150, LY - 18, Y2 + 31, true);
  b.arrowUp(150, Y2 + 25);
  b.ln("M170 " + s(LY) + " H574", true);
  const double loudnessTargets[2][2] = {{361, Y2 + 24}, {574, Y2 + 58}};
  for (const auto& t : loudnessTargets) {
    vertical(t[0], LY, t[1] + 7, true);
    b.arrowUp(t[0], t[1]);
  }
  b.txt("drives CLIP, LIMIT and FINAL", 470, LY + 16, 9, RoutingInk::Dim);
  b.dot(1170, Y2);
  vertical(1170, Y2, LY - 25, true);
  b.arrowDown(1170, LY - 18);
  b.block(1120, 1220, LY, 36, "LUFS", {"short" + mid + "integrated"}, false);
  b.ln("M1170 " + s(LY + 18) + " V" + s(MY) + " H115 V" + s(LY + 25), true);
  b.arrowUp(115, LY + 18);
  b.txt("MATCH" + dot + "sets LOUDNESS once to reach the TARGET (" + detail::minusInt(Tables::kTargets.front()) + " \xE2\x80\xA6 " +
            detail::minusInt(Tables::kTargets.back()) + " LUFS)",
        640, MY - 6, 9, RoutingInk::Dim);
  return b.take();
}

// The symbols of the LEGEND and its note (the shared legend draws them left to right). The diagram also uses the MIX node of
// GLUE (a ring with a plus), which the shared legend has no symbol for; it is labelled MIX in the drawing.
inline FairyTapes::Ui::RoutingLegend happyEndingLegend() {
  using FairyTapes::Ui::LegendSymbol;
  FairyTapes::Ui::RoutingLegend l;
  l.symbols = {LegendSymbol::Module, LegendSymbol::GainStage, LegendSymbol::Switch, LegendSymbol::Jack,
               LegendSymbol::Control, LegendSymbol::MeterPoint, LegendSymbol::Connector};
  l.meterLabel = "meter / clip point";
  l.notes = {"M/S, the limiter styles, INFLATE / CLIP, WARM, RATIO and SC HPF change what the blocks do; the order of the blocks never changes."};
  return l;
}

} // namespace FairyTapes::HappyEnding::Ui
