/**
 * Fairy Tapes · Happy Ending — the contents of the back panel for the shared template (fairy_ui/back_panel.h): the tabs in
 * the order of the spec (SETTINGS, BUTTONS, ROUTING, CREDITS), the SETTINGS items and the notes. Model: Deep Throat's
 * ui/back_settings.h. Pure (no VSTGUI), unit tested (tests/test_ui.cpp); the editor adds the callbacks and applies the
 * changes.
 *
 * SETTINGS: APPEARANCE is saved for the user (ui/prefs.h): WEAR (Off / Light / Heavy), TAPE FONT (Zeyada / Rock Salt /
 * Homemade Apple), VALUES FONT (Standard / Tape font), LOOK > RE-ROLL (a new seed for the instance) and SIZE (75 / 90 / 100 /
 * 125 %, the editor of this plug-in only). The preview's NOTES LAYER, METER HOLD and METER RELEASE rows change nothing there:
 * they are not offered (spec §8: Wear, Tape font, Values font). BEHAVIOUR (saved with the project) has OVERSAMPLING: OFF /
 * 2x / 4x, which is the stored parameter kOversampling (the controller messages the processor and asks the host to read the
 * latency again). BUTTONS is a custom tab (ui/buttons_text.h, drawn by the editor); ROUTING (ui/routing_diagram.h) and
 * CREDITS (ui/credits.h) are filled here.
 */
#pragma once

#include "params.h"
#include "ui/credits.h"
#include "ui/prefs.h"
#include "ui/routing_diagram.h"

#include "fairy_ui/back_panel.h"

#include <string>
#include <vector>

namespace FairyTapes::HappyEnding::Ui {

// Keys of the SETTINGS items (BackItem::key).
enum SettingKey : int {
  kSetWear = 1,      // APPEARANCE: OFF / LIGHT / HEAVY                          -> Prefs::wear
  kSetTapeFont,      //             Zeyada / Rock Salt / Homemade Apple (stepper) -> Prefs::tapeFont
  kSetValuesFont,    //             STANDARD / TAPE FONT                       -> Prefs::handwrittenValues
  kSetLook,          //             RE-ROLL (button): new seed, screws and tapes cleared (instance decor, not Prefs)
  kSetSize,          //             75 % / 90 % / 100 % / 125 %                -> Prefs::size
  kSetOversampling,  // BEHAVIOUR:  OFF / 2x / 4x                              -> the stored parameter kOversampling
};

// Indices of the tabs in backContent().tabs.
enum BackTabIndex : int { kTabSettings = 0, kTabButtons, kTabRouting, kTabCredits };

namespace detail {
constexpr EditorSize kSizes[] = {EditorSize::S75, EditorSize::S90, EditorSize::S100, EditorSize::S125};
constexpr int kNumSizes = static_cast<int>(sizeof(kSizes) / sizeof(kSizes[0]));
inline int sizeIndex(EditorSize s) {
  for (int i = 0; i < kNumSizes; ++i)
    if (kSizes[i] == s) return i;
  return 2;
}
inline int oversamplingSteps() { return kParams[kOversampling].steps; }
inline int limitedStep(int step) { return step < 0 ? 0 : (step >= oversamplingSteps() ? oversamplingSteps() - 1 : step); }
} // namespace detail

// The option shown as selected for `key` (0 for a button or an unknown key). `oversamplingIndex` is the step of kOversampling.
inline int selectedFor(int key, const Prefs& p, int oversamplingIndex) {
  switch (key) {
  case kSetWear: return static_cast<int>(p.wear);
  case kSetTapeFont: return static_cast<int>(p.tapeFont);
  case kSetValuesFont: return p.handwrittenValues ? 1 : 0;
  case kSetSize: return detail::sizeIndex(p.size);
  case kSetOversampling: return detail::limitedStep(oversamplingIndex);
  default: return 0;
  }
}

// Applies option `index` of `key` to the preferences; false (nothing changed) for an unknown key, the RE-ROLL button,
// OVERSAMPLING (a project setting, not a preference) or an index out of range.
inline bool applySetting(int key, int index, Prefs& p) {
  auto in = [index](int n) { return index >= 0 && index < n; };
  switch (key) {
  case kSetWear:
    if (!in(3)) return false;
    p.wear = static_cast<WearLevel>(index);
    return true;
  case kSetTapeFont:
    if (!in(3)) return false;
    p.tapeFont = static_cast<TapeFont>(index);
    return true;
  case kSetValuesFont:
    if (!in(2)) return false;
    p.handwrittenValues = index == 1;
    return true;
  case kSetSize:
    if (!in(detail::kNumSizes)) return false;
    p.size = detail::kSizes[index];
    return true;
  default: return false;
  }
}

// The step of kOversampling for option `index` of the OVERSAMPLING row; false (`step` unchanged) for any other key or an
// index the parameter has no step for. The editor writes it as one edit of the parameter.
inline bool oversamplingStepFor(int key, int index, int& step) {
  if (key != kSetOversampling || index < 0 || index >= detail::oversamplingSteps()) return false;
  step = index;
  return true;
}

// The SETTINGS items for the given preferences and OVERSAMPLING step, in row order (no callbacks). Labels, hints, options and
// widths from the preview (happy_ending_rack.html).
inline std::vector<FairyTapes::Ui::BackItem> settingsItems(const Prefs& p, int oversamplingIndex) {
  using namespace FairyTapes::Ui;
  const auto A = BackColumn::Appearance, B = BackColumn::Behaviour;
  std::vector<BackItem> items;
  items.push_back(choiceItem(kSetWear, A, "WEAR", "", {"OFF", "LIGHT", "HEAVY"}, selectedFor(kSetWear, p, 0), 84));
  items.push_back(stepperItem(kSetTapeFont, A, "TAPE FONT", "handwriting on the tape", {"Zeyada", "Rock Salt", "Homemade Apple"},
                              selectedFor(kSetTapeFont, p, 0), true));
  items.push_back(toggleItem(kSetValuesFont, A, "VALUES FONT", "values on the knobs", "STANDARD", "TAPE FONT", p.handwrittenValues, 118));
  items.push_back(buttonItem(kSetLook, A, "LOOK", "RE-ROLL", "new wear, dirt and screws"));
  std::vector<std::string> sizes;
  for (EditorSize s : detail::kSizes) sizes.emplace_back(sizeLabel(s));
  items.push_back(choiceItem(kSetSize, A, "SIZE", "editor size of this plugin", std::move(sizes), selectedFor(kSetSize, p, 0), 63));
  items.push_back(choiceItem(kSetOversampling, B, "OVERSAMPLING", "CLIP, TUBE, FINAL, METALHEAD", {"OFF", "2\xC3\x97", "4\xC3\x97"},
                             selectedFor(kSetOversampling, p, oversamplingIndex), 63));
  return items;
}

// The line under the OVERSAMPLING row (the preview's, drawn by the editor in the BEHAVIOUR box at kOversamplingNoteBaseline).
inline const char* oversamplingNote() {
  return "linear phase; the latency is reported to the host and changes only with this setting";
}
constexpr double kOversamplingNoteBaseline = 196;   // the preview's y

// The NOTES box of SETTINGS (the preview's notes, one line each).
inline std::vector<std::string> settingsNotes() {
  return {
      "Appearance settings belong to you: they are kept in your preferences and apply to every project and every instance.",
      "Behaviour settings change how the plugin works: they are saved in the project, together with the parameters.",
      "The wear layout (scratches, tape, screws) is generated once per instance from a saved seed and never changes by itself.",
      "Double-click on a knob returns it to its neutral default.",
  };
}

// The whole back for the given preferences and OVERSAMPLING step (no callbacks). BUTTONS is a custom tab the editor draws.
inline FairyTapes::Ui::BackContent backContent(const Prefs& p, int oversamplingIndex) {
  using namespace FairyTapes::Ui;
  BackContent c;
  c.headerLine = "HAPPY ENDING  \xC2\xB7  MASTERING VST3";
  c.tabs = {{"SETTINGS", BackTabKind::Settings},
            {"BUTTONS", BackTabKind::Custom},
            {"ROUTING", BackTabKind::Routing},
            {"CREDITS", BackTabKind::Credits}};
  c.items = settingsItems(p, oversamplingIndex);
  c.notes = settingsNotes();
  c.credits = backCredits();
  c.routing.flow = happyEndingRouting();
  c.routing.legend = happyEndingLegend();
  return c;
}

} // namespace FairyTapes::HappyEnding::Ui
