/**
 * Fairy Tapes · The Stripper — what the back of the panel says and does, as the common BackContent (shared/ui, fairy_ui/back_panel.h):
 * the tabs, the settings rows with their current values and callbacks, the notes and the credits. Pure (no VSTGUI), so a test
 * can check the content and call its callbacks. The BUTTONS tab is drawn by the editor (buttons_tab.h). Appearance settings are
 * user preferences: each change goes through `changePreference`, which saves and applies it and refreshes the rows.
 */

#pragma once

#include "ui/back_panel.h"
#include "ui/routing_diagram.h"

#include "fairy_ui/back_panel.h"

#include <functional>
#include <string>

namespace FairyTapes::TheStripper::Ui {

// The keys of the rows (BackItem::key), used to refresh a row from outside.
enum BackKey : int {
  kKeyWear = 1, kKeyNotes, kKeyTapeFont, kKeyValuesFont, kKeyHold, kKeyRelease, kKeyReroll,   // APPEARANCE
  kKeyInference, kKeyRecall, kKeyMarkNow, kKeyFolders, kKeyTone3000,                          // BEHAVIOUR
};

// What the editor wires to the rows.
struct BackActions {
  std::function<void(const std::function<void(Preferences&)>& mutate)> changePreference;   // saves, applies, refreshes the rows
  std::function<void()> reroll, manageFolders, openTone3000, markNow;
  std::function<void(int mode)> recallModeChanged;                                         // RECALL MARKS: 0 = Auto, 1 = Manual
};

inline FairyTapes::Ui::BackContent makeBackContent(const Preferences& prefs, int recallMode, const BackActions& act) {
  using namespace FairyTapes::Ui;
  BackContent c;
  c.headerLine = "THE STRIPPER  \xC2\xB7  CHANNEL STRIP VST3";
  c.tabs = {{"SETTINGS", BackTabKind::Settings}, {"BUTTONS", BackTabKind::Custom}, {"ROUTING", BackTabKind::Routing},
            {"CREDITS", BackTabKind::Credits}};
  c.routing = stripperRouting();

  auto prefChange = [act](void (*set)(Preferences&, int)) {
    return [act, set](int i) {
      if (act.changePreference) act.changePreference([set, i](Preferences& p) { set(p, i); });
    };
  };

  // APPEARANCE (kept for the user, valid in every project)
  BackItem wear = choiceItem(kKeyWear, BackColumn::Appearance, "WEAR", "", {"OFF", "LIGHT", "HEAVY"}, wearIndex(prefs));
  wear.onChange = prefChange(setWearIndex);
  BackItem notes = choiceItem(kKeyNotes, BackColumn::Appearance, "NOTES LAYER", "tape and pen marks on the values", {"HIDE", "SHOW"}, notesIndex(prefs));
  notes.onChange = prefChange(setNotesIndex);
  BackItem tape = stepperItem(kKeyTapeFont, BackColumn::Appearance, "TAPE FONT", "handwriting on the tape", {"Zeyada", "Rock Salt", "Homemade Apple"},
                              tapeFontIndex(prefs), /*tapeFontOptions*/ true);
  tape.onChange = prefChange(setTapeFontIndex);
  BackItem values = choiceItem(kKeyValuesFont, BackColumn::Appearance, "VALUES FONT", "values under the knobs", {"STANDARD", "TAPE FONT"},
                               valuesFontIndex(prefs), 118);
  values.onChange = prefChange(setValuesFontIndex);
  BackItem hold = choiceItem(kKeyHold, BackColumn::Appearance, "METER HOLD", "peak marker of the level meters", {"OFF", "1.5 S", "3 S", "INF"},
                             holdIndex(prefs), 63);
  hold.onChange = prefChange(setHoldIndex);
  BackItem release = choiceItem(kKeyRelease, BackColumn::Appearance, "METER RELEASE", "fall of the peak marker", {"SLOW", "NORMAL", "FAST"},
                                releaseIndex(prefs));
  release.onChange = prefChange(setReleaseIndex);
  BackItem look = buttonItem(kKeyReroll, BackColumn::Appearance, "LOOK", "RE-ROLL", "new wear, dirt and writing");
  look.onPress = act.reroll;

  // BEHAVIOUR (saved with the project): NAM INFERENCE and the rest of what is not available yet are dimmed
  BackItem inference = choiceItem(kKeyInference, BackColumn::Behaviour, "NAM INFERENCE", "needs the GPU build", {"AUTO", "GPU", "CPU"}, 0);
  inference.enabled = false;
  BackItem recall = choiceItem(kKeyRecall, BackColumn::Behaviour, "RECALL MARKS", "auto: set at every load", {"AUTO", "MANUAL"}, recallMode == 1 ? 1 : 0);
  recall.onChange = [act](int mode) { if (act.recallModeChanged) act.recallModeChanged(mode); };
  BackItem mark = buttonItem(kKeyMarkNow, BackColumn::Behaviour, "MARK NOW", "MARK NOW", "");
  mark.hint = "set the marks to the current values";
  mark.onPress = act.markNow;
  BackItem folders = buttonItem(kKeyFolders, BackColumn::Behaviour, "MODEL FOLDERS", "MANAGE...", "");
  folders.hint = "where the picker looks for models";
  folders.onPress = act.manageFolders;
  BackItem tone = buttonItem(kKeyTone3000, BackColumn::Behaviour, "TONE3000", "OPEN...", "");
  tone.hint = "sign in and download models";
  tone.onPress = act.openTone3000;

  c.items = {wear, notes, tape, values, hold, release, look, inference, recall, mark, folders, tone};
  c.notes = {"Appearance settings belong to you: they are kept in your preferences and apply to every project and every instance.",
             "Behaviour settings change how the plugin works: they are saved in the project, together with the parameters.",
             "The wear (scratches, tape, screws) is made once per instance from a saved seed and never changes by itself.",
             "Double-click on a knob returns it to its default; Alt + double-click returns it to the recalled value (the blue mark)."};

  c.credits.title = "THE STRIPPER";
  c.credits.versionLine = std::string("Version ") + kPluginVersion + "  \xC2\xB7  Fairy Tapes  \xC2\xB7  github.com/fairy-tapes";
  c.credits.licenseLines = {kLicenseLine, kLicenseLine2, kLicenseLine3};
  for (const auto& e : kCredits) c.credits.entries.push_back({e.component, e.author, e.license});
  for (const char* n : kNotices) c.credits.notices.push_back(n);
  return c;
}

// The values of the preference-backed rows, to refresh them after a change (key -> index).
struct BackRowValue {
  int key, index;
};
inline std::vector<BackRowValue> backRowValues(const Preferences& p) {
  return {{kKeyWear, wearIndex(p)},       {kKeyNotes, notesIndex(p)}, {kKeyTapeFont, tapeFontIndex(p)},
          {kKeyValuesFont, valuesFontIndex(p)}, {kKeyHold, holdIndex(p)},   {kKeyRelease, releaseIndex(p)}};
}

} // namespace FairyTapes::TheStripper::Ui
