/**
 * Fairy Tapes · Happy Ending — the BUTTONS tab of the back, "WHAT THE BUTTONS DO" (spec §7; preview
 * docs/mockups/happy_ending_rack.html, the `var E=[...]` list of the BUTTONS tab). Fourteen entries in the preview's order:
 * the name of the button (amber), one line of what it does (white), the consequences (dim). The lines are the preview's;
 * where a sentence cites a number (ratios, SC HPF frequencies, the mono-bass scale, targets, the 0.5 LU window, ceilings,
 * the maximum FINAL DRIVE) it is built from params.h / readouts.h, so the text follows the tables. The long sentences are
 * broken by hand into lines that fit the box (the preview let them run past the panel).
 *
 * Pure (no VSTGUI), unit tested (tests/test_ui.cpp); the editor draws it in the back's custom area (ui/editor.cpp). All
 * visible text is English; no source is named here (CREDITS does that).
 */
#pragma once

#include "params.h"
#include "ui/readouts.h"

#include "fairy_ui/back_panel.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace FairyTapes::HappyEnding::Ui {

struct ButtonsEntry {
  const char* name;                     // the button(s), amber
  std::vector<std::string> what;        // what it does, white, 12 px: one line each
  std::vector<std::string> details;     // what follows from it, dim, 11 px: one line each
};

// Geometry of the tab: the preview's numbers (editor px of the panel), the template's custom area is {24, 102, 1232, 700}.
namespace ButtonsTab {
constexpr FairyTapes::Ui::BackBox kBox{24, 102, 1232, 700};
constexpr const char* kTitle = "WHAT THE BUTTONS DO";
constexpr double kNameX = 50, kTextX = 270, kRight = 1230;    // the text column ends at the box's inner edge
constexpr double kNameWidth = 208;                             // a longer name breaks at its spaces (in the preview it ran into the text)
constexpr double kFirstBaseline = 142;
constexpr double kNameSize = 13, kWhatSize = 12, kDetailSize = 11;
constexpr double kWhatStep = 14, kDetailStep = 13;             // between the lines of one entry
constexpr double kMinAdvance = 16, kGap = 12;                  // the next entry starts max(last line, 16 below the name) + 12 on
} // namespace ButtonsTab

// Where an entry sits: the baseline of its name and first "what" line, of its first detail line and of its last line.
struct ButtonsRow {
  double nameBaseline = 0, detailFirst = 0, lastBaseline = 0;
};

namespace detail {

inline std::string numberText(double v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%g", v);
  return buf;
}
// "a, b or c" (two items: "a or b").
inline std::string listOr(const std::vector<std::string>& v) {
  std::string out;
  for (std::size_t i = 0; i < v.size(); ++i) {
    if (i > 0) out += i + 1 == v.size() ? " or " : ", ";
    out += v[i];
  }
  return out;
}

inline std::string ratiosText() {
  std::vector<std::string> v;
  for (double r : Tables::kGlueRatios) v.push_back(numberText(r));
  return listOr(v) + " to 1";
}
inline std::string scHpfText() {
  std::vector<std::string> v;
  for (double hz : Tables::kScHpfHz) v.push_back(hz == 0 ? "Off" : numberText(hz));
  return listOr(v) + " Hz";
}
inline std::string targetsText() {
  std::vector<std::string> v;
  for (double t : Tables::kTargets) v.push_back(minusInt(t));
  return listOr(v) + " LUFS";
}
inline std::string ceilingsText() {
  std::vector<std::string> v;
  for (double c : Tables::kCeilings) v.push_back(oneDecimal(c, false));
  return listOr(v) + " dB";
}
// "Off, 60-200 Hz": the mono-bass scale of the high pass on SIDE.
inline std::string monoBassText() {
  return std::string("Off, ") + numberText(Tables::kHpSideFreqs[1]) + "\xE2\x80\x93" + numberText(Tables::kHpSideFreqs.back()) + " Hz";
}
inline std::string defaultSettingText() {
  return numberText(Tables::kGlueRatios[static_cast<std::size_t>(kParams[kGlueRatio].defaultIndex)]) + ":1, " +
         numberText(Tables::kScHpfHz[static_cast<std::size_t>(kParams[kGlueScHpf].defaultIndex)]) + " Hz";
}

inline std::vector<ButtonsEntry> makeEntries() {
  const std::string c0 = oneDecimal(Tables::kCeilings[0], false), c1 = oneDecimal(Tables::kCeilings[1], false),
                    c3 = oneDecimal(Tables::kCeilings[3], false);
  return {
      {"BYPASS",
       {"Bypasses the whole plug-in."},
       {"The output equals the input. Every stage dims and locks; only BYPASS, the level meters and the LUFS readout",
        "stay live. No parameter value is changed."}},
      {"IN",
       {"Puts a band or a whole stage in or out of the signal path."},
       {"Off: the band or stage is bypassed and its controls dim and lock; only its IN stays live. Knob values are not changed."}},
      {"M/S",
       {"Chooses what a FINE EQ band works on: ST (both channels), MID or SIDE."},
       {"As soon as one band works on MID or SIDE the screen shows two curves. On SIDE the high pass switches to the",
        "mono-bass scale (" + monoBassText() + ")."}},
      {"LISTEN",
       {"Solos one FINE EQ bell to find a resonance by ear."},
       {"Only one band at a time; the other bands dim and lock. It switches off when the editor closes and is never",
        "saved with the project, so a forgotten LISTEN can never reach a render."}},
      {"DYN",
       {"Makes the VIBE EQ LOW or AIR boost pull back on loud program."},
       {"Fixed threshold and timing. The amber ACT LED next to it lights as much as the boost is being pulled back."}},
      {"EQ / GLUE / CLIP / LIMIT / FINAL",
       {"Choose what the screen shows."},
       {"EQ: the FINE EQ and VIBE EQ curves. GLUE and LIMIT: level, gain reduction and threshold or ceiling, scrolling.",
        "CLIP and FINAL: the transfer curve, with a dot that follows the program peaks."}},
      {"RATIO / SC HPF",
       {"Click selectors of the GLUE compressor: ratio " + ratiosText() + "; side-chain high pass " + scHpfText() + "."},
       {"The value is printed under the button; its LED lights when the setting is not the default (" + defaultSettingText() + ")."}},
      {"WARM",
       {"Softens the attack of the GLUE compressor."},
       {"Threshold, ratio and the other knobs keep their values."}},
      {"MODERN / PUNCH / DYN",
       {"Chooses the style of the limiter: one engine, three tunings."},
       {"MODERN: clean, fast release. PUNCH: lets a touch of the transient through. DYN (dynamic): slower, program-dependent release that keeps the breathing.",
        "Each style has its own release; RELEASE is a trim around it (centre = as the style wants) and keeps its position. THRESH keeps its value."}},
      {"METALHEAD IN",
       {"Switches on the optional last stage, off by default."},
       {"A brickwall digital limiter after FINAL for extreme loudness: PUSH drives into the output CEILING. Shame on you."}},
      {"LOUDNESS / TARGET / MATCH",
       {"LOUDNESS adds drive on top of your settings: CLIP first, then LIMIT, then FINAL. The stage knobs keep their values."},
       {"TARGET chooses the loudness to aim for (" + targetsText() + "); the LUFS screen shows the gap and lights green within",
        numberText(kTargetWindowLu) + " LU. MATCH, pressed on the loudest part, sets LOUDNESS once to centre the target; it never moves by itself."}},
      {"CEILING",
       {"Output ceiling for delivery (OUTPUT): " + ceilingsText() + "."},
       {"The highest peak that leaves the plug-in: LIMIT, the FINAL clip and inflate, and METALHEAD all stay under it.",
        c0 + " / " + c1 + " for loud masters and CD, " + c3 + " for streaming (no true-peak detection, so keep the margin)."}},
      {"INFLATE / CLIP",
       {"Chooses the FINAL stage: the inflator or the safety clip."},
       {"INFLATE adds loudness by rounding the waveform (EFFECT, CURVE). CLIP pushes into a clean clipper just under",
        "0 dBFS (DRIVE up to +" + numberText(Tables::kFinalDrive.back()) + " dB). Only the controls of the selected mode are shown; both keep their values."}},
      {"SET",
       {"Guided setting of the limiter release (LIMIT)."},
       {"Forces a heavy squash and locks every LIMIT control except RELEASE, so you can hear the pumping while you turn it.",
        "Press SET again to restore the threshold and unlock. It switches off when the editor closes and is never saved."}},
  };
}

} // namespace detail

// The fourteen entries, in the preview's order.
inline const std::vector<ButtonsEntry>& buttonsEntries() {
  static const std::vector<ButtonsEntry> entries = detail::makeEntries();
  return entries;
}

// The vertical layout of the preview: an entry starts at the name's baseline; its "what" lines go 14 px apart, the details
// 13 px apart after them; the next entry starts 12 px under the last line (at least 16 px under the name).
inline std::vector<ButtonsRow> buttonsRows() {
  namespace T = ButtonsTab;
  std::vector<ButtonsRow> rows;
  double y = T::kFirstBaseline;
  for (const ButtonsEntry& e : buttonsEntries()) {
    ButtonsRow r;
    r.nameBaseline = y;
    r.detailFirst = y + T::kWhatStep * static_cast<double>(e.what.size());
    r.lastBaseline = r.detailFirst + T::kDetailStep * (static_cast<double>(e.details.size()) - 1);
    rows.push_back(r);
    const double end = r.detailFirst + T::kDetailStep * static_cast<double>(e.details.size());   // the preview's `ly`
    y = std::max(end, y + T::kMinAdvance) + T::kGap;
  }
  return rows;
}

} // namespace FairyTapes::HappyEnding::Ui
