// Hotline GUI: the ROUTING tab of the back (docs/DESIGN_SYSTEM.md, "Scheda ROUTING"; spec §4), transcribed call by call from
// the approved preview (src/hotline/docs/mockups/hotline_rack.html, ROUTING tab) in the order of the audio engine (engine.cpp):
// IN -> meter IN -> MONO (L+R sum, switch drawn off) -> dry tap -> BAND -> DRIVE (clip DRV) -> CONE -> CRUSH -> COMPANDER ->
// NOISE sum -> ROOM -> MIX (dry from the tap after MONO) -> OUTPUT gain -> meter OUT; the global BYPASS goes from the input to
// the output switch; DEVICE · MORPH is a dashed control line into the blocks the device table sets.
// The preview draws this on its 628 px tall back; the editor is 470 px tall (the kShort template, SIGNAL FLOW 102..352), so the
// two rows are closer together: same blocks, same order, same x positions, only the y positions and a few lengths differ.
// The preview's "sum" mark is not in the shared builder: it is a ring of radius 9 with a plus. Pure, unit tested
// (tests/test_back.cpp).
#pragma once

#include "fairy_ui/routing.h"

#include <string>

namespace FairyTapes::Hotline::Ui {

inline FairyTapes::Ui::BackRouting hotlineRouting() {
  using FairyTapes::Ui::LegendSymbol;
  using FairyTapes::Ui::RoutingAlign;
  using FairyTapes::Ui::RoutingInk;
  FairyTapes::Ui::RoutingBuilder b;
  auto s = [](double v) { return std::to_string(v); };
  const double Y1 = 160;       // first row: input, mono, band, drive, cone, crush
  const double CB = 222;       // the DEVICE · MORPH control line
  const double Y2 = 292;       // second row: compander, noise, room, mix, output
  const double TOP = 110;      // the global BYPASS line, over the first row
  const double YB = 248;       // ... down the right side and back to the output switch
  const double Ysw = Y1 - 26;  // the line that jumps MONO
  const auto K = "\xC2\xB7";   // middle dot

  // ---- row 1: input, mono sum (off by default), dry tap, band, drive, cone, crush
  b.jack(60, Y1, std::string("IN L ") + K + " R");
  b.ln("M69 " + s(Y1) + " H158");
  b.dot(82, Y1);
  b.tap(112, Y1, 22, "IN METER", "CLIP IN");
  b.ln("M170 " + s(Y1) + " H174");
  b.block(174, 254, Y1, 44, "MONO", {"L + R"});
  // the MONO switch in its default position (off): the lever is thrown on the line that jumps the block
  b.ring(158, Y1);
  b.ring(170, Y1);
  b.ring(170, Y1 - 14);
  b.ln("M158 " + s(Y1) + " L169 " + s(Y1 - 12), false, 1.8);
  b.ln("M170 " + s(Y1 - 16.6) + " V" + s(Ysw) + " H272 V" + s(Y1));
  b.dot(272, Y1);
  b.txt("MONO", 215, Ysw - 6, 9, RoutingInk::Dim);
  b.ln("M254 " + s(Y1) + " H300");
  // the dry tap after MONO and before BAND: it comes back into MIX (connector B)
  b.dot(286, Y1);
  b.ln("M286 " + s(Y1) + " V" + s(Y1 + 16));
  b.conn(286, Y1 + 26, "B");
  b.txt("DRY", 299, Y1 + 36, 9, RoutingInk::Dim, RoutingAlign::Start);
  b.block(300, 400, Y1, 44, "BAND", {"HPF " + std::string(K) + " LPF"});
  b.ln("M400 " + s(Y1) + " H424");
  b.block(424, 524, Y1, 44, "DRIVE", {"waveshaper"});
  b.ln("M524 " + s(Y1) + " H564");
  b.tap(544, Y1, 18, "CLIP DRV");
  b.block(564, 664, Y1, 44, "CONE", {"peak EQ " + std::string(K) + " horn"});
  b.ln("M664 " + s(Y1) + " H688");
  b.block(688, 788, Y1, 44, "CRUSH", {"rate " + std::string(K) + " bits"});
  b.ln("M788 " + s(Y1) + " H910");
  b.arrow(910, Y1);
  b.conn(920, Y1, "A");

  // ---- DEVICE x MORPH: a control line into every block the device table sets
  b.rect(40, CB - 11, 116, 22);
  b.txt(std::string("DEVICE ") + K + " MORPH", 98, CB + 4, 9, RoutingInk::White);
  b.ln("M156 " + s(CB) + " H738", true);
  for (double x : {350.0, 474.0, 614.0, 738.0}) {
    b.ln("M" + s(x) + " " + s(CB) + " V" + s(Y1 + 33), true);
    b.arrowUp(x, Y1 + 26);
  }
  for (double x : {180.0, 262.0, 376.0}) {
    const bool noise = x == 262.0;
    b.ln("M" + s(x) + " " + s(CB) + " V" + s(noise ? Y2 - 64 : Y2 - 30), true);
    b.arrowDown(x, noise ? Y2 - 57 : Y2 - 23);
  }

  // ---- global BYPASS: from the input jack straight to the output switch
  b.ln("M82 " + s(Y1) + " V" + s(TOP) + " H958 V" + s(YB) + " H886 V" + s(Y2 - 16.6));
  b.txt("BYPASS", 700, TOP + 13, 9, RoutingInk::Dim);
  b.txt("output = input, bit for bit", 950, YB - 8, 9, RoutingInk::Dim, RoutingAlign::End);

  // ---- row 2: compander, noise added, room, mix (dry from B), output
  b.conn(60, Y2, "A");
  b.ln("M70 " + s(Y2) + " H90");
  b.block(90, 210, Y2, 44, "COMPANDER", {"squelch"});
  b.ln("M210 " + s(Y2) + " H253");
  b.block(217, 307, Y2 - 42, 30, "NOISE", {"hiss " + std::string(K) + " crackle"}, false);
  b.ln("M262 " + s(Y2 - 27) + " V" + s(Y2 - 9));
  b.sum(262, Y2);
  b.ln("M271 " + s(Y2) + " H316");
  b.block(316, 436, Y2, 44, "ROOM", {"short reverb"});
  b.ln("M436 " + s(Y2) + " H480");
  b.block(480, 600, Y2, 44, "MIX", {"dry / wet"});
  b.conn(540, Y2 + 48, "B");
  b.ln("M540 " + s(Y2 + 38) + " V" + s(Y2 + 31));
  b.arrowUp(540, Y2 + 24);
  b.txt("DRY", 552, Y2 + 34, 9, RoutingInk::Dim, RoutingAlign::Start);
  b.ln("M600 " + s(Y2) + " H660");
  b.amp(660, Y2, "OUTPUT");
  b.ln("M688 " + s(Y2) + " H884");
  b.ring(886, Y2);
  b.ring(886, Y2 - 14);
  b.ring(902, Y2);
  b.ln("M902 " + s(Y2) + " L888.5 " + s(Y2 - 1), false, 1.8);
  b.ln("M904.6 " + s(Y2) + " H935");
  b.tap(912, Y2, 30, "OUT METER", "CLIP OUT");
  b.jack(944, Y2, std::string("OUT L ") + K + " R");

  FairyTapes::Ui::BackRouting r;
  r.flow = b.take();
  r.legend.symbols = {LegendSymbol::Module, LegendSymbol::GainStage, LegendSymbol::Switch,    LegendSymbol::Jack,
                      LegendSymbol::Control, LegendSymbol::MeterPoint, LegendSymbol::Connector};
  r.legend.meterLabel = "meter / clip point";
  // One line: the LEGEND box of the 470 px back has room for one note. CHARACTER and its IN are explained here; they do not
  // change the path.
  r.legend.notes = {"MORPH blends the blocks from neutral to the DEVICE. "
                    "CHARACTER scales Drive, Tone, Noise, Room and Mix; with its IN off they keep the device values."};
  return r;
}

} // namespace FairyTapes::Hotline::Ui
