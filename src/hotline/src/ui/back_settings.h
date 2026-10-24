/**
 * Fairy Tapes · Hotline — the contents of the back panel for the shared template (fairy_ui/back_panel.h): the tabs in the order
 * of the spec (SETTINGS, DEVICES, ROUTING, CREDITS), the SETTINGS items, the notes, the routing (ui/routing_diagram.h) and the
 * credits (ui/credits.h). The DEVICES tab is a custom tab drawn from the device table (ui/devices_tab.h).
 *
 * SETTINGS: APPEARANCE is saved for the user (ui/prefs.h): WEAR (Off / Light / Heavy, live) and TAPE FONT (live). NOTES LAYER and
 * VALUES FONT of the other plug-ins are not offered: Hotline has no notes layer and no handwritten values, and a control that
 * changes nothing would be a lie. There is no RE-ROLL either (not requested for this plug-in). BEHAVIOUR is saved with the
 * project and Hotline has no behaviour setting yet, so the box stays empty and a note says so (it fills in when one is added).
 * Pure, unit tested (tests/test_back.cpp); the editor adds the callbacks and applies the changes.
 */
#pragma once

#include "ui/credits.h"
#include "ui/prefs.h"
#include "ui/routing_diagram.h"

#include "fairy_ui/back_panel.h"

namespace FairyTapes::Hotline::Ui {

// Keys of the SETTINGS items (BackItem::key).
enum SettingKey : int {
  kSetWear = 1,   // APPEARANCE: OFF / LIGHT / HEAVY              -> Prefs::wear
  kSetTapeFont,   //             Zeyada / Rock Salt / Homemade Apple (stepper) -> Prefs::tapeFont
};

// Index of the DEVICES tab in backContent().tabs (the custom drawer is registered for it).
constexpr int kDevicesTab = 1;

// The option shown as selected for `key`.
inline int selectedFor(int key, const Prefs& p) {
  switch (key) {
  case kSetWear: return static_cast<int>(p.wear);
  case kSetTapeFont: return static_cast<int>(p.tapeFont);
  default: return 0;
  }
}

// Applies option `index` of `key` to the preferences; false (nothing changed) for an unknown key or an index out of range.
inline bool applySetting(int key, int index, Prefs& p) {
  switch (key) {
  case kSetWear:
    if (index < 0 || index > 2) return false;
    p.wear = static_cast<WearLevel>(index);
    return true;
  case kSetTapeFont:
    if (index < 0 || index > 2) return false;
    p.tapeFont = static_cast<TapeFont>(index);
    return true;
  default: return false;
  }
}

// The whole back for the given preferences (no callbacks).
inline FairyTapes::Ui::BackContent backContent(const Prefs& p) {
  using namespace FairyTapes::Ui;
  BackContent c;
  c.headerLine = "HOTLINE  \xC2\xB7  LINE DEVICES VST3";
  c.tabs = {{"SETTINGS", BackTabKind::Settings},
            {"DEVICES", BackTabKind::Custom},
            {"ROUTING", BackTabKind::Routing},
            {"CREDITS", BackTabKind::Credits}};
  c.routing = hotlineRouting();
  const auto A = BackColumn::Appearance;
  c.items.push_back(choiceItem(kSetWear, A, "WEAR", "", {"OFF", "LIGHT", "HEAVY"}, selectedFor(kSetWear, p), 84));
  c.items.push_back(stepperItem(kSetTapeFont, A, "TAPE FONT", "handwriting on the tape", {"Zeyada", "Rock Salt", "Homemade Apple"},
                                selectedFor(kSetTapeFont, p), true));
  c.notes = {
      "Appearance settings belong to you: they are kept in your preferences and apply to every project and every instance.",
      "Hotline has no behaviour settings yet; the ones added later are saved in the project, together with the parameters.",
      "The wear layout (scratches, tape, screws) is generated once per instance from a saved seed and never changes by itself.",
      "Double-click on a knob returns it to its neutral default.",
  };
  c.credits = backCredits();
  return c;
}

} // namespace FairyTapes::Hotline::Ui
