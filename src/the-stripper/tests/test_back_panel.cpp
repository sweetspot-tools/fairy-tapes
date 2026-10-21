/**
 * Fairy Tapes · The Stripper — what the back of the panel says: credits, the help for the buttons that impose settings
 * (generated from the layout table), the settings options and word wrapping (header-only, no VSTGUI).
 */

#include "ui/back_panel.h"

#include <cmath>
#include <cstdio>
#include <set>
#include <string>

using namespace FairyTapes::TheStripper;
using namespace FairyTapes::TheStripper::Ui;

namespace {
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

bool contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

void test_credits_list_every_component_with_its_license() {
  std::string all;
  for (const auto& c : kCredits) {
    CHECK(c.component && *c.component && c.author && *c.author && c.license && *c.license);
    all += std::string(c.component) + "|" + c.author + "|" + c.license + "\n";
  }
  // the components of the plug-in and their licenses, as checked on the LICENSE files (2026-10-15)
  CHECK(contains(all, "Dusk Audio|GPL-3.0"));
  CHECK(contains(all, "Chris Johnson|MIT"));
  CHECK(contains(all, "Neural Amp Modeler Core|Steven Atkinson|MIT"));
  CHECK(contains(all, "Eigen") && contains(all, "MPL-2.0"));
  CHECK(contains(all, "nlohmann/json|Niels Lohmann|MIT"));
  CHECK(contains(all, "VST 3 SDK|Steinberg Media Technologies|MIT"));
  CHECK(contains(all, "VSTGUI"));
  // the three tape fonts (their licenses oblige us to say so)
  CHECK(contains(all, "Zeyada") && contains(all, "OFL-1.1"));
  CHECK(contains(all, "Rock Salt") && contains(all, "Homemade Apple") && contains(all, "Apache-2.0"));
  // the network part of the TONE3000 tab (libcurl is built in; OpenSSL is what it uses outside Windows)
  CHECK(contains(all, "libcurl") && contains(all, "curl license") && contains(all, "OpenSSL"));
  std::set<std::string> names;
  for (const auto& c : kCredits) CHECK(names.insert(c.component).second);   // no component twice
}

void test_notices_and_license_text() {
  CHECK(std::string(kPluginVersion) == "1.0.0" || *kPluginVersion != '\0');
  std::string notices;
  for (const char* n : kNotices) notices += n;
  CHECK(contains(notices, "trademark") && contains(notices, "Steinberg"));   // the VST trademark notice
  CHECK(contains(notices, ".nam"));                                         // the models carry their own licenses
  CHECK(contains(notices, "Powered by TONE3000"));                           // a condition of the TONE3000 API terms
  CHECK(contains(std::string(kLicenseLine), "GPL-3.0"));
}

void test_help_covers_every_button_of_the_layout() {
  const auto help = helpEntries();
  // every non-knob control of the layout appears in some entry (its parameter id is listed), and every entry names real controls
  std::set<unsigned> covered;
  for (const auto& e : help) {
    CHECK(!e.title.empty() && !e.what.empty() && !e.imposes.empty());
    for (unsigned id : e.ids) covered.insert(id);
  }
  for (const auto& c : kControls) {
    if (c.kind == Kind::Knob) continue;
    CHECK(covered.count(c.id) == 1);   // a button without an explanation fails here: the help cannot fall behind the code
  }
  for (unsigned id : covered) {
    bool exists = false;
    for (const auto& c : kControls) exists |= c.id == id && c.kind != Kind::Knob;
    CHECK(exists || id == kNoModelHelpId || id == kModelStripHelpId);   // no orphan entries
  }
}

void test_help_titles_come_from_the_layout_labels() {
  const auto help = helpEntries();
  auto find = [&](unsigned id) -> const HelpEntry* { for (const auto& e : help) for (unsigned i : e.ids) if (i == id) return &e; return nullptr; };
  const HelpEntry* mode = find(kStripMode);
  CHECK(mode && mode->title == "CHAN / BUS");
  const HelpEntry* comp = find(kCompressorMode);
  CHECK(comp && comp->title == "VOICE / DRUMS / CLASSIC");
  const HelpEntry* eng = find(kDriveNamOn);
  CHECK(eng && eng->title == "NAM / 4K");
  const HelpEntry* by = find(kBypass);
  CHECK(by && by->title == "BYPASS");
  const HelpEntry* in = find(kBypassEQ);
  CHECK(in && in->title == "IN" && find(kBypassLPHP) == in && find(kBypassGate) == in);   // one entry for the six IN buttons
  CHECK(contains(in->what, "FILTERS") || contains(in->imposes, "FILTERS"));
  CHECK(find(kEqLFBell) == find(kEqHFBell) && find(kEqLFBell) != nullptr);
}

void test_help_says_the_values_it_imposes() {
  const auto help = helpEntries();
  std::string everything;
  for (const auto& e : help) everything += e.what + " " + e.imposes + "\n";
  // the facts a user needs: what changes with Chan/Bus, with NAM/4K, and with NO MODEL
  CHECK(contains(everything, "Black") && contains(everything, "Brown"));
  CHECK(contains(everything, "Bus compressor") || contains(everything, "Bus"));
  CHECK(contains(everything, "OPTO") && contains(everything, "FET") && contains(everything, "VCA"));
  CHECK(contains(everything, "passes") || contains(everything, "pass"));
  CHECK(contains(everything, "not changed") || contains(everything, "unchanged"));
}

void test_word_wrap() {
  auto measure = [](const std::string& s) { return 6.0 * static_cast<double>(utf8Length(s)); };   // every character 6 px
  const auto lines = wrapText("the quick brown fox jumps over the lazy dog", 66, measure);        // 11 characters per line
  CHECK(lines.size() >= 4);
  for (const auto& l : lines) CHECK(measure(l) <= 66.0 && !l.empty() && l.front() != ' ' && l.back() != ' ');
  std::string joined;
  for (const auto& l : lines) joined += (joined.empty() ? "" : " ") + l;
  CHECK(joined == "the quick brown fox jumps over the lazy dog");              // nothing lost, nothing added
  CHECK(wrapText("", 100, measure).empty());
  const auto longWord = wrapText("supercalifragilistic short", 36, measure);   // a word wider than the line is cut, never dropped
  CHECK(!longWord.empty());
  std::string all;
  for (const auto& l : longWord) { CHECK(measure(l) <= 36.0); all += l; }
  CHECK(all == "supercalifragilisticshort");
  CHECK(wrapText("a b", 0.5, measure).size() >= 1);                            // nothing fits: no loop, no crash
  const auto utf = wrapText("\xC3\xA8\xC3\xA8\xC3\xA8\xC3\xA8\xC3\xA8\xC3\xA8 x", 24, measure);   // never cuts a UTF-8 sequence
  for (const auto& l : utf) CHECK(decodeValid(l));
}

void test_settings_options_map_to_preferences() {
  Preferences p;
  CHECK(wearIndex(p) == 1 && tapeFontIndex(p) == 0 && valuesFontIndex(p) == 1 && notesIndex(p) == 1);
  setWearIndex(p, 2);  CHECK(p.wear == WearLevel::Heavy && wearIndex(p) == 2);
  setWearIndex(p, 0);  CHECK(p.wear == WearLevel::Off);
  setWearIndex(p, 99); CHECK(p.wear == WearLevel::Off);                        // out of range: unchanged
  setNotesIndex(p, 0); CHECK(p.notes == NotesLevel::Hide);
  setValuesFontIndex(p, 0); CHECK(p.valuesFont == ValuesFont::Standard);
  for (int i = 0; i < 3; ++i) { setTapeFontIndex(p, i); CHECK(tapeFontIndex(p) == i); }
  setTapeFontIndex(p, 5); CHECK(tapeFontIndex(p) == 2);                        // steps are clamped, not wrapped here
  CHECK(tapeFontStep(0, -1) == 2 && tapeFontStep(2, 1) == 0 && tapeFontStep(1, 1) == 2);   // the stepper wraps around
}

void test_meter_options() {
  // OFF / 1.5 S / 3 S / INF and SLOW / NORMAL / FAST
  CHECK(holdSecondsForIndex(0) == 0.0 && holdSecondsForIndex(1) == 1.5 && holdSecondsForIndex(2) == 3.0 && holdSecondsForIndex(3) == kMaxMeterHoldSeconds);
  CHECK(releaseForIndex(0) < releaseForIndex(1) && releaseForIndex(1) < releaseForIndex(2) && releaseForIndex(1) == 20.0);
  Preferences p;
  CHECK(holdIndex(p) == 1 && releaseIndex(p) == 1);                            // the defaults
  p.meterHoldSeconds = 2.9; CHECK(holdIndex(p) == 2);                          // a value from a hand-edited file: the nearest option
  p.meterHoldSeconds = 0.2; CHECK(holdIndex(p) == 0);
  p.meterHoldSeconds = 9.9; CHECK(holdIndex(p) == 3);
  p.meterReleaseDbPerSec = 150; CHECK(releaseIndex(p) == 2);
  setHoldIndex(p, 3);   CHECK(p.meterHoldSeconds == kMaxMeterHoldSeconds);
  setReleaseIndex(p, 0); CHECK(p.meterReleaseDbPerSec == releaseForIndex(0));
  setHoldIndex(p, -1);  CHECK(p.meterHoldSeconds == kMaxMeterHoldSeconds);     // out of range: unchanged
  CHECK(holdIsForever(kMaxMeterHoldSeconds) && !holdIsForever(3.0));
}

void test_tabs() {
  CHECK(tabLabel(BackTab::Settings) == std::string("SETTINGS") && tabLabel(BackTab::Credits) == std::string("CREDITS") &&
        tabLabel(BackTab::Buttons) == std::string("BUTTONS"));
  CHECK(tabAfterClick(BackTab::Settings, BackTab::Credits) == BackTab::Credits);
}
} // namespace

int main() {
  test_credits_list_every_component_with_its_license();
  test_notices_and_license_text();
  test_help_covers_every_button_of_the_layout();
  test_help_titles_come_from_the_layout_labels();
  test_help_says_the_values_it_imposes();
  test_word_wrap();
  test_settings_options_map_to_preferences();
  test_meter_options();
  test_tabs();
  if (g_failed) { std::fprintf(stderr, "%d check(s) failed\n", g_failed); return 1; }
  std::puts("test_back_panel: ok");
  return 0;
}
