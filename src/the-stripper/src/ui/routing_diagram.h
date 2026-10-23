// The Stripper GUI: the ROUTING tab of the back (docs/DESIGN_SYSTEM.md, "Scheda ROUTING"), transcribed call by call from
// the approved preview (docs/mockups/stripper_rack.html, ROUTING tab). Order of the audio engine (processor.cpp): input
// gain, filters, equaliser, compression (external sidechain), drive (NAM or 4K), limiter, gate, output gain. Pure, tested.
#pragma once

#include "fairy_ui/routing.h"

#include <string>

namespace FairyTapes::TheStripper::Ui {


inline FairyTapes::Ui::BackRouting stripperRouting() {
  using FairyTapes::Ui::LegendSymbol;
  using FairyTapes::Ui::RoutingAlign;
  using FairyTapes::Ui::RoutingInk;
  FairyTapes::Ui::RoutingBuilder b;
  auto s = [](double v) { return std::to_string(v); };
  const double Y1 = 200, Y2 = 392;
  // row 1: input, filters, equaliser, compression
  b.jack(60, Y1, "IN L \xC2\xB7 R");
  b.ln("M69 " + s(Y1) + " H100");
  b.dot(82, Y1);
  b.amp(100, Y1, "INPUT");
  b.ln("M128 " + s(Y1) + " H178");
  b.tap(150, Y1, 50, "IN METER", "CLIP IN");
  b.ln("M190 " + s(Y1) + " H194");
  b.block(194, 304, Y1, 48, "FILTERS", {"HP \xC2\xB7 LP"});
  b.inSwitch(178, Y1, 322, 150);
  b.ln("M304 " + s(Y1) + " H344");
  b.ln("M356 " + s(Y1) + " H360");
  b.block(360, 490, Y1, 48, "EQUALISER", {"4K-G \xC2\xB7 4K-E"});
  b.inSwitch(344, Y1, 508, 150);
  b.ln("M490 " + s(Y1) + " H556");
  b.tap(528, Y1, 50, "CLIP EQ");
  b.ln("M568 " + s(Y1) + " H572");
  b.block(572, 722, Y1, 48, "COMPRESSION", {"OPTO-2A \xC2\xB7 FET-76", "VCA-160 \xC2\xB7 BUS"});
  b.inSwitch(556, Y1, 740, 150);
  b.ln("M722 " + s(Y1) + " H910");
  b.tap(762, Y1, 50, "GR C", "CLIP CMP");
  b.arrow(910, Y1);
  b.conn(920, Y1, "A");
  // sidechain: control line into the compressor detector
  b.jack(60, 290, "SC IN");
  b.ln("M69 290 H680 V232", true);
  b.arrowUp(680, 226);
  b.txt("SIDECHAIN", 380, 284, 9, RoutingInk::Dim);
  // global BYPASS: from the input jack straight to the output switch
  b.ln("M82 " + s(Y1) + " V128 H958 V300 H886 V" + s(Y2 - 16.6));
  b.txt("BYPASS", 520, 122, 9, RoutingInk::Dim);
  b.txt("output = input, bit for bit", 950, 292, 9, RoutingInk::Dim, RoutingAlign::End);
  // row 2: drive (NAM or 4K), limiter, gate, output
  b.conn(60, Y2, "A");
  b.ln("M70 " + s(Y2) + " H90");
  b.inSwitch(90, Y2, 292, 316);
  b.ln("M102 " + s(Y2) + " H118");
  b.ring(118, Y2);
  b.ring(134, Y2 - 30);
  b.ring(134, Y2 + 30);
  b.ln("M118 " + s(Y2) + " L133 " + s(Y2 - 28), false, 1.8);
  b.ln("M134 " + s(Y2 - 32.6) + " V" + s(Y2 - 30) + " M136.6 " + s(Y2 - 30) + " H144");
  b.ln("M136.6 " + s(Y2 + 30) + " H144");
  b.block(144, 264, Y2 - 30, 36, "NAM", {".nam model"});
  b.block(144, 264, Y2 + 30, 36, "4K", {"saturation"});
  b.txt("DRIVE", 204, Y2 - 56, 10, RoutingInk::White);
  b.ln("M264 " + s(Y2 - 30) + " H278 V" + s(Y2 + 30) + " H264");
  b.dot(278, Y2);
  b.ln("M278 " + s(Y2) + " H330");
  b.tap(306, Y2, 52, "CLIP DRV");
  b.ln("M342 " + s(Y2) + " H346");
  b.block(346, 476, Y2, 48, "LIMITER", {"B-PARTY"});
  b.inSwitch(330, Y2, 494, 342);
  b.ln("M476 " + s(Y2) + " H540");
  b.tap(514, Y2, 52, "GR L", "CLIP LIM");
  b.ln("M552 " + s(Y2) + " H556");
  b.block(556, 686, Y2, 48, "GATE", {"D-NOISE"});
  b.inSwitch(540, Y2, 704, 342);
  b.ln("M686 " + s(Y2) + " H744");
  b.tap(722, Y2, 52, "GR G");
  b.amp(744, Y2, "OUTPUT");
  b.ln("M772 " + s(Y2) + " H884");
  b.ring(886, Y2);
  b.ring(886, Y2 - 14);
  b.ring(902, Y2);
  b.ln("M902 " + s(Y2) + " L888.5 " + s(Y2 - 1), false, 1.8);
  b.ln("M904.6 " + s(Y2) + " H935");
  b.tap(918, Y2, 52, "OUT METER", "CLIP OUT");
  b.jack(944, Y2, "OUT L \xC2\xB7 R");

  FairyTapes::Ui::BackRouting r;
  r.flow = b.take();
  r.legend.symbols = {LegendSymbol::Module, LegendSymbol::GainStage, LegendSymbol::Switch, LegendSymbol::Jack,
                      LegendSymbol::Control, LegendSymbol::MeterPoint, LegendSymbol::Connector};
  r.legend.notes = {"CHAN / BUS, VOICE / DRUMS / CLASSIC and NAM / 4K change the character of the blocks; the order of the "
                    "blocks never changes."};
  return r;
}

} // namespace FairyTapes::TheStripper::Ui
