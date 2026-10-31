/**
 * Fairy Tapes · Deep Throat — the MODES tab of the back ("WHAT THE MODE BUTTONS DO", spec §8): what SPEAK, SING and PUKE
 * set inside the chain. Every cell, the column heads and the notes are generated from the mode table (mode_table.h) with
 * the same strings the front shows (ui/dynamic_text.h): nothing is typed twice. The editor draws it (a custom tab of the
 * shared BackView) with the column of the active mode in yellow. Pure (no VSTGUI), unit tested (tests/test_ui.cpp).
 * Texts and coordinates from the preview (deep_throat_rack.html, MODES tab), editor px of the panel.
 */
#pragma once

#include "../mode_table.h"
#include "../params.h"
#include "ui/dynamic_text.h"

#include "fairy_ui/back_panel.h"

#include <array>
#include <cstdio>
#include <string>
#include <vector>

namespace FairyTapes::DeepThroat::Ui {

struct ModesRow {
  std::string label;
  std::array<std::string, 3> cells;   // SPEAK, SING, PUKE
};

// Geometry of the tab (the preview's coordinates).
namespace ModesTab {
constexpr const char* kTitle = "WHAT THE MODE BUTTONS DO";
constexpr FairyTapes::Ui::BackBox kBox{24, 102, 952, 488};    // inside the template's custom area (24, 102, 952, 508)
constexpr double kLabelX = 50, kRight = 950;
constexpr double kIntroBaseline[2] = {142, 160};              // 12 px white, 11 px dim
constexpr double kColumnX[3] = {570, 730, 890};               // centres of the SPEAK, SING, PUKE columns
constexpr double kHeadBaseline = 200, kHeadSize = 15;
constexpr double kRuleY = 212;                                // white rule under the heads
constexpr double kFirstRowBaseline = 242, kRowStep = 30, kRowSize = 13;
constexpr double kSeparatorBelow = 12;                        // dim rule 12 px under each row but the last
constexpr double kNoteFirstBaseline = 510, kNoteStep = 18, kNoteSize = 11;
} // namespace ModesTab

namespace detail {
inline std::string modeNames(const ModeRow (&t)[3], bool (*pick)(const ModeRow&)) {
  std::string s;
  for (const ModeRow& r : t)
    if (pick(r)) s += (s.empty() ? "" : ", ") + std::string(r.name);
  return s;
}
} // namespace detail

// The rows of the table, in the preview's order, for a table (the real one: modesRows()).
inline std::vector<ModesRow> modesRows(const ModeRow (&t)[3]) {
  auto row = [&t](const char* label, std::string (*cell)(const ModeRow&)) {
    return ModesRow{label, {cell(t[0]), cell(t[1]), cell(t[2])}};
  };
  return {
      row("Breath detector", [](const ModeRow& r) { return std::string(detectorTag(r)); }),
      row("Rider max range", [](const ModeRow& r) { return riderRangeText(r); }),
      row("Rider speed", [](const ModeRow& r) { return riderSpeedText(r); }),
      row("HPF", [](const ModeRow& r) { return eqHpfText(r, 0.5); }),               // EQ Amount 50 %: the table's value
      row("Mud cut (at 300 Hz)", [](const ModeRow& r) { return eqMudText(r, 0.5); }),
      row("De-ess", [](const ModeRow& r) { return deEssScaleText(r); }),
      row("Comp ratio", [](const ModeRow& r) { return compRatioText(r); }),
      row("Dist", [](const ModeRow& r) { return distName(r); }),
      row("Tone curve (mud / presence / air, dB at +100%)", [](const ModeRow& r) { return toneCurveText(r); }),
  };
}
inline std::vector<ModesRow> modesRows() { return modesRows(kModeTable); }

// The column heads: the mode names.
inline std::array<std::string, 3> modesHeader(const ModeRow (&t)[3]) { return {t[0].name, t[1].name, t[2].name}; }
inline std::array<std::string, 3> modesHeader() { return modesHeader(kModeTable); }

// The two lines over the table.
inline std::vector<std::string> modesIntro() {
  return {"SPEAK, SING and PUKE set the internal values below for the whole chain. The knobs keep their values.",
          "The same table drives the read-only values on the front; the active mode is highlighted."};
}

// The notes under the table. The De-ess line names each mode that scales the de-esser (none: no line); the detector line
// names the modes with the LEVEL detector.
inline std::vector<std::string> modesNotes(const ModeRow (&t)[3]) {
  std::vector<std::string> out;
  out.push_back("RATIO AUTO and DIST AUTO use the values above; the other choices ignore the mode.");
  for (const ModeRow& r : t) {
    if (r.deEssScale >= 1.0) continue;
    const std::string k = detail::fmt("%g", r.deEssScale);
    out.push_back("De-ess x" + k + ": the DE-ESS amount is multiplied by " + k + " in " + r.name +
                  (r.deEssScale <= 0.25 ? ", so the stage is almost off." : "."));
  }
  const std::string level = detail::modeNames(t, [](const ModeRow& r) { return r.levelDetector; });
  out.push_back("Breath detector: SPECTRAL tells breaths from voice by spectrum; LEVEL" + (level.empty() ? "" : " (" + level + ")") +
                " uses the level only, because growls and screams are noisy.");
  out.push_back("Rider max range is the full travel of the RANGE knob; the rider never moves faster than the speed shown.");
  return out;
}
inline std::vector<std::string> modesNotes() { return modesNotes(kModeTable); }

// The column drawn in yellow: the row of the table the engine reads for `m` (modeRow clamps the same way).
inline int activeModeColumn(Mode m) {
  const int i = static_cast<int>(m);
  return i < 0 ? 0 : (i > 2 ? 2 : i);
}

} // namespace FairyTapes::DeepThroat::Ui
