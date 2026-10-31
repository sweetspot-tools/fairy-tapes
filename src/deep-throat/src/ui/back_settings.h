/**
 * Fairy Tapes · Deep Throat — the contents of the back panel for the shared template (fairy_ui/back_panel.h): the tabs
 * in the order of the spec (SETTINGS, MODES, ROUTING, CREDITS), the SETTINGS items and the notes. Model: Pillow Talk's
 * ui/back_settings.h. Pure (no VSTGUI), unit tested (tests/test_ui.cpp); the editor adds the callbacks and applies the
 * changes.
 *
 * SETTINGS: APPEARANCE is saved for the user (ui/prefs.h): WEAR (Off / Light / Heavy), TAPE FONT (Zeyada / Rock Salt /
 * Homemade Apple), VALUES FONT (Standard / Tape font) and LOOK > RE-ROLL (a new seed for the instance). The preview's
 * NOTES LAYER, METER HOLD and METER RELEASE rows are placeholders that change nothing there: they are not offered (spec
 * §8: Wear, Tape font, Values font). BEHAVIOUR (saved with the project) has no item yet, so its box stays empty. The
 * preview's notes go in the template's NOTES box (at most four lines).
 * ROUTING (ui/routing_diagram.h) and CREDITS (ui/credits.h) are filled here; MODES is a custom tab the editor draws from
 * ui/back_modes.h.
 */
#pragma once

#include "ui/credits.h"
#include "ui/prefs.h"
#include "ui/routing_diagram.h"

#include "fairy_ui/back_panel.h"

#include <string>
#include <vector>

namespace FairyTapes::DeepThroat::Ui {

// Keys of the SETTINGS items (BackItem::key).
enum SettingKey : int {
  kSetWear = 1,    // APPEARANCE: OFF / LIGHT / HEAVY                          -> Prefs::wear
  kSetTapeFont,    //             Zeyada / Rock Salt / Homemade Apple (stepper) -> Prefs::tapeFont
  kSetValuesFont,  //             STANDARD / TAPE FONT                       -> Prefs::handwrittenValues
  kSetLook,        //             RE-ROLL (button): new seed, screws and tapes cleared (instance decor, not Prefs)
};

// Indices of the tabs in backContent().tabs.
enum BackTabIndex : int { kTabSettings = 0, kTabModes, kTabRouting, kTabCredits };

// The option shown as selected for `key` (0 for a button or an unknown key).
inline int selectedFor(int key, const Prefs& p) {
  switch (key) {
  case kSetWear: return static_cast<int>(p.wear);
  case kSetTapeFont: return static_cast<int>(p.tapeFont);
  case kSetValuesFont: return p.handwrittenValues ? 1 : 0;
  default: return 0;
  }
}

// Applies option `index` of `key` to the preferences; false (nothing changed) for an unknown key, the RE-ROLL button or
// an index out of range.
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
  default: return false;
  }
}

// The SETTINGS items for the given preferences, in row order (no callbacks). Labels, hints, options and widths from the
// preview (deep_throat_rack.html).
inline std::vector<FairyTapes::Ui::BackItem> settingsItems(const Prefs& p) {
  using namespace FairyTapes::Ui;
  const auto A = BackColumn::Appearance;
  std::vector<BackItem> items;
  items.push_back(choiceItem(kSetWear, A, "WEAR", "", {"OFF", "LIGHT", "HEAVY"}, selectedFor(kSetWear, p), 84));
  items.push_back(stepperItem(kSetTapeFont, A, "TAPE FONT", "handwriting on the tape", {"Zeyada", "Rock Salt", "Homemade Apple"},
                              selectedFor(kSetTapeFont, p), true));
  items.push_back(toggleItem(kSetValuesFont, A, "VALUES FONT", "values on the knobs", "STANDARD", "TAPE FONT", p.handwrittenValues, 118));
  items.push_back(buttonItem(kSetLook, A, "LOOK", "RE-ROLL", "new wear, dirt and screws"));
  return items;
}

// The NOTES box of SETTINGS (the preview's notes, one line each).
inline std::vector<std::string> settingsNotes() {
  return {
      "Appearance settings belong to you: they are kept in your preferences and apply to every project and every instance.",
      "The wear layout (scratches, tape, screws) is generated once per instance from a saved seed: RE-ROLL gives a new one.",
      "Double-click on a knob returns it to its default value. Click a clip LED to clear it.",
      "The three mode buttons (SPEAK, SING, PUKE) change values inside the chain: see the MODES tab.",
  };
}

// The whole back for the given preferences (no callbacks).
inline FairyTapes::Ui::BackContent backContent(const Prefs& p) {
  using namespace FairyTapes::Ui;
  BackContent c;
  c.headerLine = "DEEP THROAT  \xC2\xB7  VOCAL CHANNEL STRIP VST3";
  c.tabs = {{"SETTINGS", BackTabKind::Settings},
            {"MODES", BackTabKind::Custom},
            {"ROUTING", BackTabKind::Routing},
            {"CREDITS", BackTabKind::Credits}};
  c.items = settingsItems(p);
  c.notes = settingsNotes();
  c.credits = backCredits();
  c.routing = deepThroatRouting();
  return c;
}

} // namespace FairyTapes::DeepThroat::Ui
