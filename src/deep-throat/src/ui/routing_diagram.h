// Deep Throat GUI: the ROUTING tab of the back (docs/DESIGN_SYSTEM.md, "Scheda ROUTING"; spec §8), transcribed call by
// call from the approved preview (docs/mockups/deep_throat_rack.html, ROUTING tab). Order of the engine (engine.cpp): the
// input meter and clip on the raw input, the input gain, the lookahead delay, Rider, Breath, EQ, De-ess (first line), Comp,
// Tone, the two Listen taps, the output gain (second line); the detection runs on the gained, live (not delayed) input,
// stereo-linked mono, and drives the Rider and the breath reducer (dashed control lines); the global BYPASS sends the raw
// input, delayed, to the output switch. Pure, tested.
#pragma once

#include "fairy_ui/routing.h"

#include <string>

namespace FairyTapes::DeepThroat::Ui {

inline FairyTapes::Ui::BackRouting deepThroatRouting() {
  using FairyTapes::Ui::LegendSymbol;
  using FairyTapes::Ui::RoutingAlign;
  using FairyTapes::Ui::RoutingInk;
  FairyTapes::Ui::RoutingBuilder b;
  auto s = [](double v) { return std::to_string(v); };
  const double Y1 = 178, YD = 300, Y2 = 424;
  const std::string y1 = s(Y1), yd = s(YD), y2 = s(Y2);

  // line 1: input meter and clip (raw input), input gain, lookahead delay, Rider, Breath, EQ, De-ess
  b.jack(60, Y1, "IN L \xC2\xB7 R");
  b.ln("M69 " + y1 + " H112");
  b.tap(86, Y1, 36, "IN METER", "CLIP IN");
  b.dot(102, Y1);
  b.amp(112, Y1, "INPUT");
  b.ln("M140 " + y1 + " H172");
  b.dot(154, Y1);
  b.block(172, 250, Y1, 44, "DELAY", {"LOOKAHEAD"});
  b.ln("M250 " + y1 + " H268");
  b.ln("M280 " + y1 + " H284");
  b.block(284, 364, Y1, 44, "RIDER", {"GAIN"});
  b.inSwitch(268, Y1, 382, 140);
  b.ln("M364 " + y1 + " H396");
  b.ln("M408 " + y1 + " H412");
  b.block(412, 492, Y1, 44, "BREATH", {"REDUCER"});
  b.inSwitch(396, Y1, 510, 140);
  b.ln("M492 " + y1 + " H524");
  b.ln("M536 " + y1 + " H540");
  b.block(540, 620, Y1, 44, "EQ", {"HPF \xC2\xB7 MUD CUT"});
  b.inSwitch(524, Y1, 638, 140);
  b.ln("M620 " + y1 + " H652");
  b.ln("M664 " + y1 + " H668");
  b.block(668, 760, Y1, 44, "DE-ESS", {"SPLIT BAND"});
  b.inSwitch(652, Y1, 778, 140);
  b.ln("M760 " + y1 + " H910");
  b.arrow(910, Y1);
  b.conn(920, Y1, "A");
  // what the Listen buttons play: the band the de-esser removes, the part of the breath the reducer removes
  b.ln("M740 200 V240 H870");
  b.arrow(870, 240);
  b.conn(880, 240, "B");
  b.txt("removed band", 806, 234, 9, RoutingInk::Dim);
  b.ln("M480 200 V262 H910");
  b.arrow(910, 262);
  b.conn(920, 262, "C");
  b.txt("removed breath", 700, 256, 9, RoutingInk::Dim);
  // detection: gained, live (not delayed) input, stereo-linked mono; control lines into the Rider and the breath reducer
  b.ln("M154 " + y1 + " V" + yd + " H160", true);
  b.block(160, 220, YD, 32, "L + R", {"MONO"});
  b.ln("M220 " + yd + " H244 M232 " + yd + " V350 H244", true);
  b.dot(232, YD);
  b.block(244, 320, YD, 32, "LEVEL", {"RIDER METER"});
  b.ln("M320 " + yd + " H334 V207", true);
  b.arrowUp(334, 200);
  b.block(244, 356, 350, 36, "BREATH DETECTOR", {"SPECTRAL / LEVEL"});
  b.ln("M356 350 H380", true);
  b.block(380, 460, 350, 36, "MAX", {"LOOKAHEAD"});
  b.ln("M430 332 V207", true);
  b.arrowUp(430, 200);
  b.ln("M400 332 V278 H342", true);
  b.arrowLeft(335, 278);
  b.txt("HOLD ON BREATH", 372, 272, 8, RoutingInk::Dim);
  // global BYPASS: the raw input, delayed, straight to the output switch
  b.ln("M102 " + y1 + " V120 H958 V360 H700 V" + s(Y2 - 16.6));
  b.txt("BYPASS", 300, 114, 9, RoutingInk::Dim);
  b.txt("output = input, delayed by the lookahead", 950, 352, 9, RoutingInk::Dim, RoutingAlign::End);
  // line 2: Comp, Tone, Listen, output gain, output switch
  b.conn(60, Y2, "A");
  b.ln("M70 " + y2 + " H90");
  b.ln("M102 " + y2 + " H106");
  b.block(106, 236, Y2, 48, "COMP", {"RATIO \xC2\xB7 DIST", "SC HPF ON DETECTOR"});
  b.inSwitch(90, Y2, 254, 390);
  b.ln("M236 " + y2 + " H290");
  b.tap(270, Y2, 26, "GR");
  b.ln("M302 " + y2 + " H306");
  b.block(306, 410, Y2, 48, "TONE", {"MUD \xC2\xB7 PRES \xC2\xB7 AIR"});
  b.inSwitch(290, Y2, 428, 390);
  b.ln("M410 " + y2 + " H470");
  b.block(470, 560, Y2, 48, "LISTEN", {"DE-ESS \xC2\xB7 BREATH"});
  b.conn(490, 372, "B");
  b.conn(540, 372, "C");
  b.ln("M490 382 V393 M540 382 V393");
  b.arrowDown(490, 400);
  b.arrowDown(540, 400);
  b.ln("M560 " + y2 + " H600");
  b.amp(600, Y2, "OUTPUT");
  b.ln("M628 " + y2 + " H697.4");
  b.ring(700, Y2);
  b.ring(700, Y2 - 14);
  b.ring(716, Y2);
  b.ln("M716 " + y2 + " L702.5 " + s(Y2 - 1), false, 1.8);
  b.ln("M718.6 " + y2 + " H935");
  b.tap(760, Y2, 26, "OUT METER", "CLIP OUT");
  b.jack(944, Y2, "OUT L \xC2\xB7 R");

  FairyTapes::Ui::BackRouting r;
  r.flow = b.take();
  r.legend.symbols = {LegendSymbol::Module, LegendSymbol::GainStage, LegendSymbol::Switch, LegendSymbol::Jack,
                      LegendSymbol::Control, LegendSymbol::MeterPoint, LegendSymbol::Connector};
  r.legend.meterLabel = "meter / clip point";
  r.legend.notes = {"SPEAK / SING / PUKE change the values inside the blocks (see MODES). LISTEN plays what DE-ESS or BREATH "
                    "removes (BREATH wins); off, the mix."};
  return r;
}

} // namespace FairyTapes::DeepThroat::Ui
