// The Stripper GUI: the ROUTING tab of the back (docs/DESIGN_SYSTEM.md, "Scheda ROUTING"), transcribed call by call from
// the approved preview (docs/mockups/stripper_rack.html, ROUTING tab). Order of the audio engine (processor.cpp): input
// gain, [drive in CHAN], filters, equaliser with the 4K saturation, compression (external sidechain), [drive in BUS],
// limiter, gate, output gain; the drive block holds NAM then TAPE, each with its own switch. Pure, tested.
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
  // CHAN / BUS deviator of the drive block: closed = the block is played here; drawn in the default position (CHAN)
  auto modeSwitch = [&](double px, double y, double rx, double top, const std::string& label, bool closed) {
    b.ring(px, y);
    b.ring(px + 12, y);
    b.ring(px + 12, y - 14);
    if (closed)
      b.ln("M" + s(px) + " " + s(y) + " L" + s(px + 11) + " " + s(y - 2), false, 1.8);
    else
      b.ln("M" + s(px) + " " + s(y) + " L" + s(px + 11) + " " + s(y - 12.6), false, 1.8);
    b.ln("M" + s(px + 12) + " " + s(y - 16.6) + " V" + s(top) + " H" + s(rx) + " V" + s(y));
    b.dot(rx, y);
    b.txt(label, (px + rx) / 2, top - 6, 9, RoutingInk::Amber);
  };
  // row 1: input, drive in CHAN, filters, equaliser, compression
  b.jack(60, Y1, "IN L \xC2\xB7 R");
  b.ln("M69 " + s(Y1) + " H100");
  b.dot(82, Y1);
  b.amp(100, Y1, "INPUT");
  b.ln("M128 " + s(Y1) + " H176");
  b.tap(148, Y1, 50, "IN METER", "CLIP IN");
  b.ln("M188 " + s(Y1) + " H192");
  b.block(192, 282, Y1, 48, "DRIVE", {"NAM \xC2\xB7 TAPE"});
  modeSwitch(176, Y1, 298, 150, "CHAN", true);
  b.ln("M282 " + s(Y1) + " H314");
  b.ln("M326 " + s(Y1) + " H330");
  b.block(330, 420, Y1, 48, "FILTERS", {"HP \xC2\xB7 LP"});
  b.inSwitch(314, Y1, 436, 150);
  b.ln("M420 " + s(Y1) + " H462");
  b.ln("M474 " + s(Y1) + " H478");
  b.block(478, 588, Y1, 48, "EQUALISER", {"4K-G \xC2\xB7 4K-E", "+ SATURATE"});
  b.inSwitch(462, Y1, 604, 150);
  b.ln("M588 " + s(Y1) + " H658");
  b.tap(626, Y1, 50, "CLIP EQ");
  b.ln("M670 " + s(Y1) + " H674");
  b.block(674, 810, Y1, 48, "COMPRESSION", {"OPTO-2A \xC2\xB7 FET-76", "VCA-160 \xC2\xB7 BUS"});
  b.inSwitch(658, Y1, 826, 150);
  b.ln("M810 " + s(Y1) + " H910");
  b.tap(856, Y1, 50, "GR C", "CLIP CMP");
  b.arrow(910, Y1);
  b.conn(920, Y1, "A");
  // sidechain: control line into the compressor detector
  b.jack(60, 290, "SC IN");
  b.ln("M69 290 H770 V232", true);
  b.arrowUp(770, 226);
  b.txt("SIDECHAIN", 420, 284, 9, RoutingInk::Dim);
  // global BYPASS: from the input jack straight to the output switch
  b.ln("M82 " + s(Y1) + " V128 H958 V300 H886 V" + s(Y2 - 16.6));
  b.txt("BYPASS", 520, 122, 9, RoutingInk::Dim);
  b.txt("output = input, bit for bit", 950, 292, 9, RoutingInk::Dim, RoutingAlign::End);
  // row 2: drive in BUS (NAM then TAPE, each with its own switch), limiter, gate, output
  b.conn(60, Y2, "A");
  b.ln("M70 " + s(Y2) + " H86");
  modeSwitch(84, Y2, 320, 318, "BUS", false);
  b.inSwitch(100, Y2, 312, 338);
  b.ln("M112 " + s(Y2) + " H118");
  // the drive module: NAM then TAPE, each with its own on/off switch
  b.rect(118, Y2 - 38, 186, 76);
  b.arrow(118, Y2);
  b.txt("DRIVE", 211, Y2 + 54, 10, RoutingInk::White);
  b.ln("M118 " + s(Y2) + " H126");
  b.inSwitch(126, Y2, 206, Y2 - 22, "NAM");
  b.block(144, 196, Y2, 30, "NAM");
  b.ln("M196 " + s(Y2) + " H210");
  b.inSwitch(210, Y2, 294, Y2 - 22, "TAPE");
  b.block(228, 284, Y2, 30, "TAPE");
  b.ln("M284 " + s(Y2) + " H304");
  b.ln("M304 " + s(Y2) + " H342");
  b.tap(328, Y2, 52, "CLIP DRV");
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
  r.legend.notes = {"CHAN / BUS moves the DRIVE block (CHAN: after the INPUT, BUS: before the LIMITER) and sets the TAPE "
                    "voicing; the other modes only change the character."};
  return r;
}

} // namespace FairyTapes::TheStripper::Ui
