/**
 * Fairy Tapes · The Stripper — what the back of the panel says: credits, the help for the buttons that impose settings
 * (generated from the layout table), the settings options and word wrapping (header-only, no VSTGUI).
 */

#include "ui/back_content.h"
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
  std::set<std::string> names;
  for (const auto& c : kCredits) CHECK(names.insert(c.component).second);   // no component twice
}

void test_notices_and_license_text() {
  CHECK(std::string(kPluginVersion) == "1.0.0" || *kPluginVersion != '\0');
  std::string notices;
  for (const char* n : kNotices) notices += n;
  CHECK(contains(notices, "trademark") && contains(notices, "Steinberg"));   // the VST trademark notice
  CHECK(contains(notices, ".nam"));                                         // the models carry their own licenses
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
  // the preference stores a BackTab, the common view an index: the order shown is SETTINGS, BUTTONS, ROUTING, CREDITS
  CHECK(backTabIndex(BackTab::Settings) == 0 && backTabIndex(BackTab::Buttons) == 1 && backTabIndex(BackTab::Routing) == 2 &&
        backTabIndex(BackTab::Credits) == 3);
  for (BackTab t : {BackTab::Settings, BackTab::Buttons, BackTab::Routing, BackTab::Credits}) CHECK(backTabOfIndex(backTabIndex(t)) == t);
  CHECK(backTabOfIndex(-5) == BackTab::Settings && backTabOfIndex(99) == BackTab::Credits);   // out of range: the nearest tab
}

// What the common back view is given.
FairyTapes::Ui::BackContent content(const Preferences& p, int recall = 0, const BackActions& a = {}) { return makeBackContent(p, recall, a); }

void test_the_content_fits_the_common_template() {
  const auto problems = FairyTapes::Ui::backContentProblems(content(Preferences{}));
  for (const auto& pr : problems) std::fprintf(stderr, "  content problem: %s\n", pr.c_str());
  CHECK(problems.empty());
  const auto c = content(Preferences{});
  CHECK(c.tabs.size() == 4 && c.tabs[0].label == "SETTINGS" && c.tabs[1].label == "BUTTONS" && c.tabs[2].label == "ROUTING" &&
        c.tabs[3].label == "CREDITS");
  CHECK(c.tabs[2].kind == FairyTapes::Ui::BackTabKind::Routing && !c.routing.flow.empty());   // the diagram of the preview
  CHECK(c.tabs[1].kind == FairyTapes::Ui::BackTabKind::Custom);                                   // the BUTTONS tab is drawn by the editor
  CHECK(c.items.size() == 11 && c.notes.size() == 4 && c.credits.entries.size() == static_cast<std::size_t>(kCreditCount));
  CHECK(c.headerLine == "THE STRIPPER  \xC2\xB7  CHANNEL STRIP VST3" && c.credits.title == "THE STRIPPER");
  CHECK(c.credits.licenseLines.size() == 3 && c.credits.notices.size() == 3);
  CHECK(c.credits.versionLine.find("Fairy Tapes  \xC2\xB7  github.com/sweetspot-tools") != std::string::npos);   // suite and publisher
}

void test_rows_show_the_preferences_and_the_recall_mode() {
  Preferences p;
  p.wear = WearLevel::Heavy; p.notes = NotesLevel::Hide; p.tapeFont = TapeFont::RockSalt; p.valuesFont = ValuesFont::Standard;
  p.meterHoldSeconds = 3.0; p.meterReleaseDbPerSec = 40.0;
  const auto c = content(p, 1);
  auto sel = [&](int key) { const int i = FairyTapes::Ui::findItem(c, key); return i < 0 ? -1 : c.items[static_cast<std::size_t>(i)].selected; };
  CHECK(sel(kKeyWear) == 2 && sel(kKeyNotes) == 0 && sel(kKeyTapeFont) == 1 && sel(kKeyValuesFont) == 0 && sel(kKeyHold) == 2 && sel(kKeyRelease) == 2);
  CHECK(sel(kKeyRecall) == 1);
  const auto rows = backRowValues(p);                                                            // used to refresh the rows after a change
  CHECK(rows.size() == 6);
  for (const auto& r : rows) CHECK(r.index == sel(r.key));
  const auto font = c.items[static_cast<std::size_t>(FairyTapes::Ui::findItem(c, kKeyTapeFont))];
  CHECK(font.kind == FairyTapes::Ui::BackItemKind::Stepper && font.tapeFontOptions && font.options.size() == 3);   // each name in its own font
}

void test_what_is_not_available_yet_is_dimmed() {
  const auto c = content(Preferences{});
  auto item = [&](int key) { return c.items[static_cast<std::size_t>(FairyTapes::Ui::findItem(c, key))]; };
  CHECK(!item(kKeyInference).enabled);                                                           // needs the GPU build
  for (int key : {kKeyWear, kKeyRecall, kKeyMarkNow, kKeyFolders, kKeyReroll}) CHECK(item(key).enabled);
}

void test_changing_a_row_changes_the_preferences_and_presses_call_the_editor() {
  Preferences p;
  int rerolls = 0, folders = 0, marks = 0, recallMode = -1, changes = 0;
  BackActions a;
  a.changePreference = [&](const std::function<void(Preferences&)>& mutate) { mutate(p); ++changes; };
  a.reroll = [&] { ++rerolls; };
  a.manageFolders = [&] { ++folders; };
  a.markNow = [&] { ++marks; };
  a.recallModeChanged = [&](int m) { recallMode = m; };
  const auto c = content(p, 0, a);
  auto item = [&](int key) { return c.items[static_cast<std::size_t>(FairyTapes::Ui::findItem(c, key))]; };
  item(kKeyWear).onChange(2);
  CHECK(p.wear == WearLevel::Heavy);
  item(kKeyNotes).onChange(0);
  CHECK(p.notes == NotesLevel::Hide);
  item(kKeyTapeFont).onChange(2);
  CHECK(p.tapeFont == TapeFont::HomemadeApple);
  item(kKeyValuesFont).onChange(0);
  CHECK(p.valuesFont == ValuesFont::Standard);
  item(kKeyHold).onChange(3);
  CHECK(p.meterHoldSeconds == kMaxMeterHoldSeconds);
  item(kKeyRelease).onChange(0);
  CHECK(p.meterReleaseDbPerSec == releaseForIndex(0));
  CHECK(changes == 6);
  item(kKeyReroll).onPress();
  item(kKeyFolders).onPress();
  item(kKeyMarkNow).onPress();
  item(kKeyRecall).onChange(1);
  CHECK(rerolls == 1 && folders == 1 && marks == 1 && recallMode == 1 && changes == 6);   // these are not preferences
  BackActions none;                                                                               // no callbacks at all: nothing crashes
  const auto quiet = content(Preferences{}, 0, none);
  quiet.items[static_cast<std::size_t>(FairyTapes::Ui::findItem(quiet, kKeyWear))].onChange(1);
  quiet.items[static_cast<std::size_t>(FairyTapes::Ui::findItem(quiet, kKeyRecall))].onChange(1);
}
} // namespace

int main() {
  test_credits_list_every_component_with_its_license();
  test_notices_and_license_text();
  test_help_covers_every_button_of_the_layout();
  test_help_titles_come_from_the_layout_labels();
  test_help_says_the_values_it_imposes();
  test_settings_options_map_to_preferences();
  test_meter_options();
  test_tabs();
  test_the_content_fits_the_common_template();
  test_rows_show_the_preferences_and_the_recall_mode();
  test_what_is_not_available_yet_is_dimmed();
  test_changing_a_row_changes_the_preferences_and_presses_call_the_editor();
  if (g_failed) { std::fprintf(stderr, "%d check(s) failed\n", g_failed); return 1; }
  std::puts("test_back_panel: ok");
  return 0;
}
