/**
 * Fairy Tapes · The Stripper — what the back of the panel says and offers (spec 4, "Retro del pannello"): the credits with
 * the licenses, the help for the buttons that impose settings, the settings options. Header-only and free of VSTGUI so it
 * is unit tested; BackPanel draws it. All visible text is English.
 *
 * The help is generated from the same layout table the editor is built from (ui/layout.h): the titles are the labels of
 * the controls, and every button of the table must have an explanation (the test fails otherwise), so the help cannot
 * fall behind the code.
 */

#pragma once

#include "preferences.h"
#include "version.h"
#include "ui/layout.h"
#include "ui/model_source.h"
#include "ui/tape_font_id.h"

#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace FairyTapes::TheStripper::Ui {

// ---- Credits ------------------------------------------------------------------------------------------------------
struct CreditEntry {
  const char* component;
  const char* author;
  const char* license;
};

// Components shipped inside the plug-in, with the licenses read from their LICENSE files (rechecked 2026-10-15).
constexpr CreditEntry kCredits[] = {
    {"4K-EQ \xC2\xB7 Multi-Comp", "Dusk Audio", "GPL-3.0"},
    {"Airwindows (BlockParty, DeNoise)", "Chris Johnson", "MIT"},
    {"Neural Amp Modeler Core", "Steven Atkinson", "MIT"},
    {"AudioDSPTools", "Steven Atkinson", "MIT"},
    {"Eigen", "Eigen contributors", "MPL-2.0"},
    {"nlohmann/json", "Niels Lohmann", "MIT"},
    {"VST 3 SDK", "Steinberg Media Technologies", "MIT"},
    {"VSTGUI", "Steinberg Media Technologies", "BSD-3-Clause"},
    {"Zeyada (tape font)", "Kimberly Geswein", "OFL-1.1"},
    {"Rock Salt (tape font)", "Font Diner", "Apache-2.0"},
    {"Homemade Apple (tape font)", "Font Diner", "Apache-2.0"},
    {"libcurl (TONE3000 login and downloads)", "Daniel Stenberg and contributors", "curl license"},
    {"OpenSSL (Linux and macOS builds)", "The OpenSSL Project Authors", "Apache-2.0"},
};
constexpr int kCreditCount = static_cast<int>(sizeof(kCredits) / sizeof(kCredits[0]));

constexpr const char* kLicenseLine = "Free software: released under the GNU General Public License v3.0 (GPL-3.0), full text in the LICENSE file.";
constexpr const char* kLicenseLine2 = "The plugin includes GPL-3.0 components, so the combined work is distributed under the same license.";
constexpr const char* kLicenseLine3 = "Source code and build instructions are available in the project repository.";

constexpr const char* kNotices[] = {
    "VST is a trademark of Steinberg Media Technologies GmbH, registered in Europe and other countries.",
    ".nam models belong to their authors; those from TONE3000 carry the license shown with each model. Powered by TONE3000.",
    "Third-party names are cited for attribution only.",
};

// ---- Help for the buttons that impose settings --------------------------------------------------------------------
constexpr unsigned kNoModelHelpId = 0xFFFF0001u;      // the NO MODEL button of the model popup (not a parameter)
constexpr unsigned kModelStripHelpId = 0xFFFF0002u;   // the paper strip with the model name

struct HelpEntry {
  std::string title;              // the labels of the control(s), as printed on the panel
  std::string what;               // what the control is for
  std::string imposes;            // the values and settings it imposes or changes
  std::vector<unsigned> ids;      // the parameters (or the special ids above) it covers
};

inline const char* moduleName(Module m) {
  switch (m) {
  case Module::Filters: return "FILTERS";
  case Module::Eq: return "EQUALISER";
  case Module::Comp: return "COMPRESSION";
  case Module::Drive: return "DRIVE";
  case Module::Limiter: return "LIMITER";
  case Module::Gate: return "GATE";
  default: return "";
  }
}

namespace help_detail {
inline std::string joinChoices(const ControlSpec& c) {
  std::string out;
  for (int k = 0; k < 3 && c.choices[k]; ++k) out += (out.empty() ? "" : " / ") + std::string(c.choices[k]);
  return out;
}
}  // namespace help_detail

inline std::vector<HelpEntry> helpEntries() {
  std::vector<HelpEntry> out;
  HelpEntry in{"IN", "", "", {}};
  HelpEntry bell{"BELL", "", "", {}};
  std::string inModules;
  for (const ControlSpec& c : kControls) {
    if (c.kind == Kind::Knob) continue;
    if (c.isIn) {
      in.ids.push_back(c.id);
      inModules += (inModules.empty() ? "" : ", ") + std::string(moduleName(c.module));
      continue;
    }
    if (isBellButton(c.id)) {
      bell.ids.push_back(c.id);
      continue;
    }
    HelpEntry e;
    e.ids = {c.id};
    e.title = c.kind == Kind::Choice ? help_detail::joinChoices(c) : std::string(c.label ? c.label : "");
    switch (c.id) {
    case kStripMode:
      e.what = "Chooses the processing: a channel strip (CHAN) or a mix bus (BUS).";
      e.imposes = "CHAN: EQ \"Black\" (G series), compressor character selectable (VOICE, DRUMS, CLASSIC), 4K saturation of the G series. "
                  "BUS: EQ \"Brown\" (E series), 4K saturation of the E series, compressor fixed to the Bus compressor, "
                  "VOICE / DRUMS / CLASSIC hidden and the Bell buttons out of use. No knob value is changed.";
      break;
    case kCompressorMode:
      e.what = "Chooses the character of the compressor (CHAN only).";
      e.imposes = "VOICE: OPTO compressor (smooth, program dependent). DRUMS: FET compressor (fast, punchy). CLASSIC: VCA compressor "
                  "(soft knee, all round). Amount and Mix keep their values. In BUS the Bus compressor is always used.";
      break;
    case kDriveNamOn:
      e.what = "Chooses what drives the Drive stage.";
      e.imposes = "NAM: the loaded .nam model, with the Drive knob as its amount; without a model the signal passes through unchanged. "
                  "4K: the 4K console saturation, with its own amount on the same knob. Each engine keeps its own knob value; "
                  "the paper strip reads \"4K Saturate\" while 4K is on.";
      break;
    case kBypass:
      e.what = "Bypasses the whole plug-in.";
      e.imposes = "The output equals the input: every module is out, the gain reduction meters are at zero and only the IN and OUT clip LEDs stay on. "
                  "No parameter value is changed; the modules and their IN buttons dim.";
      break;
    default:
      e.what = "(no description)";
      e.imposes = "(no description)";
      break;
    }
    out.push_back(std::move(e));
  }
  in.what = "Puts a module in or out of the signal path: " + inModules + ".";
  in.imposes = "Lit: the module is in (its bypass parameter is 0). Off: the module is bypassed (parameter 1) and its controls dim. "
               "FILTERS has one button for High Pass and Low Pass together. Knob values are not changed.";
  bell.what = "Chooses the shape of the LF and HF bands of the equaliser.";
  bell.imposes = "Lit: bell (peak) filter. Off: shelf filter. Only the CHAN equaliser has the bell; in BUS the buttons are out of use. "
                 "The band's frequency and gain are not changed.";
  if (!in.ids.empty()) out.push_back(std::move(in));
  if (!bell.ids.empty()) out.push_back(std::move(bell));
  out.push_back({"NO MODEL", "Unloads the NAM model (button of the model picker).",
                 "The model path is cleared and saved empty. With NAM selected the Drive passes the signal through unchanged. The Drive knob value is not changed.",
                 {kNoModelHelpId}});
  out.push_back({"MODEL STRIP", "The paper strip in the Drive box: click it to pick a .nam model (NAM only).",
                 "LOAD loads the chosen file on a separate thread (the audio never stops) and saves its path with the project. "
                 "A file that cannot be loaded shows its error on the strip and the Drive passes the signal through.",
                 {kModelStripHelpId}});
  return out;
}

// ---- Word wrapping and text checks --------------------------------------------------------------------------------
// True when the string is well-formed UTF-8 (no truncated or stray continuation bytes).
inline bool decodeValid(const std::string& s) {
  for (std::size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    const std::size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
    if (len == 0 || i + len > s.size()) return false;
    for (std::size_t k = 1; k < len; ++k)
      if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
    i += len;
  }
  return true;
}

// Breaks `text` into lines no wider than `maxWidth` (`measure` gives the pixel width of a string), at spaces; a word wider than
// a line is cut (never in the middle of a UTF-8 sequence). Nothing is dropped.
inline std::vector<std::string> wrapText(const std::string& text, double maxWidth, const std::function<double(const std::string&)>& measure) {
  std::vector<std::string> lines;
  std::vector<std::string> words;
  {
    std::string w;
    for (char c : text) {
      if (c == ' ' || c == '\n') {
        if (!w.empty()) words.push_back(w);
        w.clear();
      } else {
        w.push_back(c);
      }
    }
    if (!w.empty()) words.push_back(w);
  }
  std::string line;
  auto flush = [&] {
    if (!line.empty()) lines.push_back(line);
    line.clear();
  };
  for (const std::string& word : words) {
    const std::string candidate = line.empty() ? word : line + " " + word;
    if (measure(candidate) <= maxWidth) {
      line = candidate;
      continue;
    }
    flush();
    std::string rest = word;
    while (!rest.empty() && measure(rest) > maxWidth) {   // a word wider than the line: cut it by measured width
      std::size_t cut = rest.size();
      while (cut > 0) {
        do { --cut; } while (cut > 0 && (static_cast<unsigned char>(rest[cut]) & 0xC0) == 0x80);
        if (cut == 0 || measure(rest.substr(0, cut)) <= maxWidth) break;
      }
      if (cut == 0) {   // not even one character fits: take one anyway so the text still ends
        cut = 1;
        while (cut < rest.size() && (static_cast<unsigned char>(rest[cut]) & 0xC0) == 0x80) ++cut;
      }
      lines.push_back(rest.substr(0, cut));
      rest = rest.substr(cut);
    }
    line = rest;
  }
  flush();
  return lines;
}

// ---- Settings: options and their mapping to the preferences ---------------------------------------------------------
inline int wearIndex(const Preferences& p) { return static_cast<int>(p.wear); }
inline void setWearIndex(Preferences& p, int i) {
  if (i >= 0 && i <= 2) p.wear = static_cast<WearLevel>(i);
}
inline int notesIndex(const Preferences& p) { return static_cast<int>(p.notes); }
inline void setNotesIndex(Preferences& p, int i) {
  if (i >= 0 && i <= 1) p.notes = static_cast<NotesLevel>(i);
}
inline int valuesFontIndex(const Preferences& p) { return static_cast<int>(p.valuesFont); }
inline void setValuesFontIndex(Preferences& p, int i) {
  if (i >= 0 && i <= 1) p.valuesFont = static_cast<ValuesFont>(i);
}
inline int tapeFontIndex(const Preferences& p) { return static_cast<int>(p.tapeFont); }
inline void setTapeFontIndex(Preferences& p, int i) {
  p.tapeFont = static_cast<TapeFont>(i < 0 ? 0 : i > 2 ? 2 : i);
}
inline int tapeFontStep(int current, int delta) { return ((current + delta) % 3 + 3) % 3; }   // the stepper wraps around

// METER HOLD: OFF / 1.5 S / 3 S / INF; METER RELEASE: SLOW / NORMAL / FAST (dB per second).
inline double holdSecondsForIndex(int i) {
  static const double v[] = {0.0, 1.5, 3.0, kMaxMeterHoldSeconds};
  return v[i < 0 ? 0 : i > 3 ? 3 : i];
}
inline double releaseForIndex(int i) {
  static const double v[] = {10.0, 20.0, 40.0};
  return v[i < 0 ? 0 : i > 2 ? 2 : i];
}
inline bool holdIsForever(double seconds) { return seconds >= kMaxMeterHoldSeconds; }
inline int holdIndex(const Preferences& p) {
  int best = 0;
  for (int i = 1; i < 4; ++i)
    if (std::fabs(holdSecondsForIndex(i) - p.meterHoldSeconds) < std::fabs(holdSecondsForIndex(best) - p.meterHoldSeconds)) best = i;
  return best;
}
inline int releaseIndex(const Preferences& p) {
  int best = 0;
  for (int i = 1; i < 3; ++i)
    if (std::fabs(releaseForIndex(i) - p.meterReleaseDbPerSec) < std::fabs(releaseForIndex(best) - p.meterReleaseDbPerSec)) best = i;
  return best;
}
inline void setHoldIndex(Preferences& p, int i) {
  if (i >= 0 && i <= 3) p.meterHoldSeconds = holdSecondsForIndex(i);
}
inline void setReleaseIndex(Preferences& p, int i) {
  if (i >= 0 && i <= 2) p.meterReleaseDbPerSec = releaseForIndex(i);
}

// ---- Tabs ----------------------------------------------------------------------------------------------------------
inline const char* tabLabel(BackTab t) { return t == BackTab::Credits ? "CREDITS" : t == BackTab::Buttons ? "BUTTONS" : "SETTINGS"; }
inline BackTab tabAfterClick(BackTab /*current*/, BackTab clicked) { return clicked; }

} // namespace FairyTapes::TheStripper::Ui
