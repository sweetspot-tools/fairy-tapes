/**
 * Fairy Tapes · Pillow Talk — the contents of the back panel (ui/back_settings.h): SETTINGS exactly as the preview
 * (pillow_rack.html: APPEARANCE wear, notes layer, tape font, values font, look; BEHAVIOUR display window, display peaks,
 * tempo fallback), the current values from the preferences and the instance behaviour, what a choice changes, and that
 * the content fits the shared template. Build target: pillow_talk_back_settings_tests.
 */
#include "ui/back_settings.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace FairyTapes::PillowTalk;
using namespace FairyTapes::PillowTalk::Ui;
using FairyTapes::Ui::BackColumn;
using FairyTapes::Ui::BackItemKind;
using FairyTapes::Ui::WearLevel;

namespace {
int g_failed = 0, g_passed = 0;

#define ASSERT_TRUE(expr) do { \
  if (!(expr)) { \
    std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); \
    ++g_failed; return; \
  } \
} while(0)
#define RUN_TEST(fn) do { const int before = g_failed; fn(); if (g_failed == before) ++g_passed; } while(0)

const FairyTapes::Ui::BackItem* item(const FairyTapes::Ui::BackContent& c, int key) {
  const int i = FairyTapes::Ui::findItem(c, key);
  return i < 0 ? nullptr : &c.items[static_cast<std::size_t>(i)];
}

void test_settings_are_the_preview_ones() {
  const auto c = backContent(Prefs{}, Behaviour{});
  ASSERT_TRUE(FairyTapes::Ui::backContentProblems(c, FairyTapes::Ui::Back::kShort).empty());   // the 1000 x 470 back
  ASSERT_TRUE(c.headerLine == "PILLOW TALK  \xC2\xB7  TAPE ECHO VST3");
  ASSERT_TRUE(c.tabs.size() == 3 && c.tabs[0].label == "SETTINGS" && c.tabs[1].label == "ROUTING" && c.tabs[2].label == "CREDITS");
  ASSERT_TRUE(c.tabs[1].kind == FairyTapes::Ui::BackTabKind::Routing && !c.routing.flow.empty());   // the preview's diagram
  struct Row { int key; BackColumn col; BackItemKind kind; const char* label; std::vector<std::string> options; };
  const std::vector<Row> rows = {
      {kSetWear, BackColumn::Appearance, BackItemKind::Choice, "WEAR", {"OFF", "LIGHT", "HEAVY"}},
      {kSetNotes, BackColumn::Appearance, BackItemKind::Choice, "NOTES LAYER", {"HIDE", "SHOW"}},
      {kSetTapeFont, BackColumn::Appearance, BackItemKind::Stepper, "TAPE FONT", {"Zeyada", "Rock Salt", "Homemade Apple"}},
      {kSetValuesFont, BackColumn::Appearance, BackItemKind::Choice, "VALUES FONT", {"STANDARD", "TAPE FONT"}},
      {kSetLook, BackColumn::Appearance, BackItemKind::Button, "LOOK", {"RE-ROLL"}},
      {kSetDisplayWindow, BackColumn::Behaviour, BackItemKind::Choice, "DISPLAY WINDOW", {"3 S", "6 S", "12 S"}},
      {kSetDisplayPeaks, BackColumn::Behaviour, BackItemKind::Choice, "DISPLAY PEAKS", {"OFF", "1.5 S", "3 S"}},
      {kSetTempoFallback, BackColumn::Behaviour, BackItemKind::Choice, "TEMPO FALLBACK", {"120 BPM"}},
  };
  ASSERT_TRUE(c.items.size() == rows.size());
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const auto& it = c.items[i];   // in the preview's order
    ASSERT_TRUE(it.key == rows[i].key && it.column == rows[i].col && it.kind == rows[i].kind);
    ASSERT_TRUE(it.label == rows[i].label && it.options == rows[i].options);
  }
  ASSERT_TRUE(item(c, kSetTapeFont)->tapeFontOptions);
  ASSERT_TRUE(item(c, kSetValuesFont)->cellWidth == 118 && item(c, kSetTempoFallback)->cellWidth == 120);
  ASSERT_TRUE(item(c, kSetLook)->note == "new wear, dirt and screws");
  ASSERT_TRUE(item(c, kSetDisplayWindow)->hint == "time shown by the ducking display");
  ASSERT_TRUE(c.notes.size() == 4);
  ASSERT_TRUE(c.credits.title == "PILLOW TALK");
}

void test_current_values_are_selected() {
  Prefs p;
  p.wear = WearLevel::Heavy;
  p.notes = false;
  p.tapeFont = TapeFont::HomemadeApple;
  p.handwrittenValues = false;
  Behaviour b;
  b.displayWindow = 2;
  b.displayPeaks = 0;
  const auto c = backContent(p, b);
  ASSERT_TRUE(item(c, kSetWear)->selected == 2 && item(c, kSetNotes)->selected == 0);
  ASSERT_TRUE(item(c, kSetTapeFont)->selected == 2 && item(c, kSetValuesFont)->selected == 0);
  ASSERT_TRUE(item(c, kSetDisplayWindow)->selected == 2 && item(c, kSetDisplayPeaks)->selected == 0);
  ASSERT_TRUE(item(c, kSetTempoFallback)->selected == 0);
  const auto d = backContent(Prefs{}, Behaviour{});   // defaults: Light, show, Zeyada, tape font, 6 s, 1.5 s
  ASSERT_TRUE(item(d, kSetWear)->selected == 1 && item(d, kSetNotes)->selected == 1 && item(d, kSetValuesFont)->selected == 1);
  ASSERT_TRUE(item(d, kSetDisplayWindow)->selected == 1 && item(d, kSetDisplayPeaks)->selected == 1);
  for (int key : {kSetWear, kSetNotes, kSetTapeFont, kSetValuesFont, kSetDisplayWindow, kSetDisplayPeaks, kSetTempoFallback})
    ASSERT_TRUE(selectedFor(key, p, b) == item(c, key)->selected);
}

void test_applying_a_choice() {
  Prefs p;
  Behaviour b;
  ASSERT_TRUE(applySetting(kSetWear, 0, p, b) == SettingTarget::Prefs && p.wear == WearLevel::Off);
  ASSERT_TRUE(applySetting(kSetNotes, 0, p, b) == SettingTarget::Prefs && !p.notes);
  ASSERT_TRUE(applySetting(kSetTapeFont, 1, p, b) == SettingTarget::Prefs && p.tapeFont == TapeFont::RockSalt);
  ASSERT_TRUE(applySetting(kSetValuesFont, 0, p, b) == SettingTarget::Prefs && !p.handwrittenValues);
  ASSERT_TRUE(applySetting(kSetDisplayWindow, 0, p, b) == SettingTarget::Behaviour && b.displayWindow == 0);
  ASSERT_TRUE(applySetting(kSetDisplayPeaks, 2, p, b) == SettingTarget::Behaviour && b.displayPeaks == 2);
  ASSERT_TRUE(applySetting(kSetTempoFallback, 0, p, b) == SettingTarget::Behaviour && b.tempoFallbackBpm == 120.f);
  // out of range or unknown: nothing changes
  const Prefs before = p;
  const Behaviour beforeB = b;
  ASSERT_TRUE(applySetting(kSetWear, 3, p, b) == SettingTarget::None && p.wear == before.wear);
  ASSERT_TRUE(applySetting(kSetDisplayWindow, -1, p, b) == SettingTarget::None && b.displayWindow == beforeB.displayWindow);
  ASSERT_TRUE(applySetting(kSetLook, 0, p, b) == SettingTarget::None);   // a button, not a choice
  ASSERT_TRUE(applySetting(12345, 0, p, b) == SettingTarget::None);
}
void test_reroll_gives_a_new_look_and_keeps_the_behaviour() {
  Decor d;
  d.seed = 1234;
  d.screwAngles[0] = 45.f;
  d.screwAngles[3] = 300.f;
  d.tapeTexts = {"VOX", "", "take 3"};
  d.behaviour.displayWindow = 2;
  d.behaviour.tempoFallbackBpm = 90.f;
  const Decor r = rerolledDecor(d, 777);
  ASSERT_TRUE(r.seed == 777);
  for (float a : r.screwAngles) ASSERT_TRUE(a == 0.f);        // the screws as the new seed makes them
  ASSERT_TRUE(r.tapeTexts.empty());                           // blank tapes
  ASSERT_TRUE(r.behaviour.displayWindow == 2 && r.behaviour.tempoFallbackBpm == 90.f);   // not part of the look
  ASSERT_TRUE(rerolledDecor(d, 1234).seed != 1234 && rerolledDecor(d, 1234).seed != 0);   // always another seed
  ASSERT_TRUE(rerolledDecor(d, 0).seed != 0 && rerolledDecor(d, 0).seed != 1234);
}
} // namespace

int main() {
  RUN_TEST(test_settings_are_the_preview_ones);
  RUN_TEST(test_current_values_are_selected);
  RUN_TEST(test_applying_a_choice);
  RUN_TEST(test_reroll_gives_a_new_look_and_keeps_the_behaviour);
  std::printf("%d passed, %d failed\n", g_passed, g_failed);
  return g_failed ? 1 : 0;
}
