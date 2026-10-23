// Pillow Talk GUI: the ROUTING tab of the back (docs/DESIGN_SYSTEM.md, "Scheda ROUTING"), transcribed call by call from
// the approved preview (docs/mockups/pillow_rack.html, ROUTING tab). Order of the engine (engine.cpp): the input splits
// into the dry path, the tape echo (wet, through the DUCK gain) and the detector (input or sidechain); MIX, then the
// global BYPASS (a short fade to the dry signal). Pure, tested.
#pragma once

#include "fairy_ui/routing.h"

#include <string>

namespace FairyTapes::PillowTalk::Ui {

inline FairyTapes::Ui::BackRouting pillowRouting() {
  using FairyTapes::Ui::LegendSymbol;
  using FairyTapes::Ui::RoutingAlign;
  using FairyTapes::Ui::RoutingInk;
  FairyTapes::Ui::RoutingBuilder b;
  auto s = [](double v) { return std::to_string(v); };
  const double YB = 124, YD = 158, YW = 226, YC = 300;
  // input, split into the dry path, the tape echo (wet) and the ducking detector
  b.jack(60, YW, "IN L \xC2\xB7 R");
  b.ln("M69 " + s(YW) + " H200");
  b.dot(82, YW);
  b.tap(110, YW, 40, "IN METER", "CLIP IN");
  b.dot(150, YW);
  b.ln("M150 " + s(YD) + " V" + s(YC));
  // dry path, over the top into the mix
  b.ln("M150 " + s(YD) + " H610 V197");
  b.arrowDown(610, 204);
  b.txt("DRY", 380, YD - 6, 9, RoutingInk::Dim);
  // wet path: tape echo, duck gain, mix
  b.block(200, 400, YW, 44, "DELAY", {"TAPE ECHO"});
  b.ln("M400 " + s(YW) + " H470");
  b.txt("WET", 435, YW - 6, 9, RoutingInk::Dim);
  b.amp(470, YW, "");
  b.txt("DUCK", 484, YW - 20, 10, RoutingInk::White);
  b.ln("M498 " + s(YW) + " H560");
  b.tap(528, YW, 40, "ECHO");
  b.block(560, 660, YW, 44, "MIX", {"DRY \xC2\xB7 WET"});
  b.ln("M660 " + s(YW) + " H878");
  // global BYPASS: the output fades to the dry signal
  b.ln("M82 " + s(YW) + " V" + s(YB) + " H880 V" + s(YW - 16.6));
  b.txt("BYPASS", 300, YB - 6, 9, RoutingInk::Dim);
  b.txt("output = dry signal (short fade)", 870, YB + 14, 9, RoutingInk::Dim, RoutingAlign::End);
  b.ring(880, YW);
  b.ring(880, YW - 14);
  b.ring(896, YW);
  b.ln("M896 " + s(YW) + " L882.5 " + s(YW - 1), false, 1.8);
  b.ln("M898.6 " + s(YW) + " H935");
  b.tap(916, YW, 40, "OUT METER", "CLIP OUT");
  b.jack(944, YW, "OUT L \xC2\xB7 R");
  // detector: the input or the sidechain (SIDECHAIN switch, default off), then the gain sent to DUCK
  b.ln("M150 " + s(YC) + " H247.4");
  b.jack(60, YC + 14, "SC IN");
  b.ln("M69 " + s(YC + 14) + " H247.4");
  b.ring(250, YC);
  b.ring(250, YC + 14);
  b.ring(266, YC);
  b.ln("M266 " + s(YC) + " L252.5 " + s(YC + 1), false, 1.8);
  b.txt("SIDECHAIN", 258, YC - 10, 9, RoutingInk::Dim);
  b.ln("M268.6 " + s(YC) + " H300");
  b.tap(284, YC, 16, "VOICE");
  b.block(300, 440, YC, 40, "AUTO DUCK", {"DETECTOR"});
  b.ln("M440 " + s(YC) + " H484 V242", true);
  b.arrowUp(484, 236);
  b.tap(462, YC, 16, "SEND LEVEL");

  FairyTapes::Ui::BackRouting r;
  r.flow = b.take();
  r.legend.symbols = {LegendSymbol::Module, LegendSymbol::GainStage, LegendSymbol::Switch, LegendSymbol::Jack,
                      LegendSymbol::Control, LegendSymbol::MeterPoint};
  r.legend.meterLabel = "meter / display point";
  r.legend.notes = {"SYNC takes TIME and HOLD from the host tempo; the tape always records the full phrase, the ducking acts "
                    "on what is played back."};
  return r;
}

} // namespace FairyTapes::PillowTalk::Ui
