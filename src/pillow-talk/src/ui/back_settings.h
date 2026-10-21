/**
 * Fairy Tapes · Pillow Talk — the contents of the back panel for the shared template (fairy_ui/back_panel.h): the tabs,
 * the SETTINGS items exactly as in the preview (pillow_rack.html), the notes and the credits (ui/credits.h). Appearance
 * settings live in the user preferences (ui/prefs.h), behaviour settings in the instance state (state.h: Behaviour,
 * state v3). Pure, unit tested (tests/test_back_settings.cpp); the editor adds the callbacks and applies the changes.
 */
#pragma once

#include "state.h"
#include "ui/credits.h"
#include "ui/prefs.h"

#include "fairy_ui/back_panel.h"

#include <vector>

namespace FairyTapes::PillowTalk::Ui {

// Keys of the SETTINGS items (BackItem::key).
enum SettingKey : int {
  kSetWear = 1,        // APPEARANCE: OFF / LIGHT / HEAVY            -> Prefs::wear
  kSetNotes,           //             HIDE / SHOW                     -> Prefs::notes
  kSetTapeFont,        //             Zeyada / Rock Salt / Homemade Apple (stepper) -> Prefs::tapeFont
  kSetValuesFont,      //             STANDARD / TAPE FONT            -> Prefs::handwrittenValues
  kSetLook,            //             RE-ROLL (button): new seed, screws and tapes cleared
  kSetDisplayWindow,   // BEHAVIOUR:  3 S / 6 S / 12 S                -> Behaviour::displayWindow
  kSetDisplayPeaks,    //             OFF / 1.5 S / 3 S               -> Behaviour::displayPeaks
  kSetTempoFallback,   //             120 BPM                         -> Behaviour::tempoFallbackBpm
};

// TEMPO FALLBACK offers one value in the preview.
constexpr float kTempoFallbackOptions[] = {120.f};
constexpr int kTempoFallbackCount = static_cast<int>(sizeof(kTempoFallbackOptions) / sizeof(kTempoFallbackOptions[0]));

// The option shown as selected for `key`.
inline int selectedFor(int key, const Prefs& p, const Behaviour& b) {
  switch (key) {
  case kSetWear: return static_cast<int>(p.wear);
  case kSetNotes: return p.notes ? 1 : 0;
  case kSetTapeFont: return static_cast<int>(p.tapeFont);
  case kSetValuesFont: return p.handwrittenValues ? 1 : 0;
  case kSetDisplayWindow: return static_cast<int>(b.displayWindow < (uint32_t)kDisplayWindowCount ? b.displayWindow : Behaviour{}.displayWindow);
  case kSetDisplayPeaks: return static_cast<int>(b.displayPeaks < (uint32_t)kDisplayPeaksCount ? b.displayPeaks : Behaviour{}.displayPeaks);
  case kSetTempoFallback: {
    int best = 0;
    for (int i = 1; i < kTempoFallbackCount; ++i)
      if (std::fabs(kTempoFallbackOptions[i] - b.tempoFallbackBpm) < std::fabs(kTempoFallbackOptions[best] - b.tempoFallbackBpm)) best = i;
    return best;
  }
  default: return 0;
  }
}

enum class SettingTarget { None, Prefs, Behaviour };

// Applies option `index` of `key`; says which store changed (None: unknown key, a button or an index out of range).
inline SettingTarget applySetting(int key, int index, Prefs& p, Behaviour& b) {
  auto in = [index](int n) { return index >= 0 && index < n; };
  switch (key) {
  case kSetWear:
    if (!in(3)) return SettingTarget::None;
    p.wear = static_cast<WearLevel>(index);
    return SettingTarget::Prefs;
  case kSetNotes:
    if (!in(2)) return SettingTarget::None;
    p.notes = index == 1;
    return SettingTarget::Prefs;
  case kSetTapeFont:
    if (!in(3)) return SettingTarget::None;
    p.tapeFont = static_cast<TapeFont>(index);
    return SettingTarget::Prefs;
  case kSetValuesFont:
    if (!in(2)) return SettingTarget::None;
    p.handwrittenValues = index == 1;
    return SettingTarget::Prefs;
  case kSetDisplayWindow:
    if (!in(kDisplayWindowCount)) return SettingTarget::None;
    b.displayWindow = static_cast<uint32_t>(index);
    return SettingTarget::Behaviour;
  case kSetDisplayPeaks:
    if (!in(kDisplayPeaksCount)) return SettingTarget::None;
    b.displayPeaks = static_cast<uint32_t>(index);
    return SettingTarget::Behaviour;
  case kSetTempoFallback:
    if (!in(kTempoFallbackCount)) return SettingTarget::None;
    b.tempoFallbackBpm = kTempoFallbackOptions[index];
    return SettingTarget::Behaviour;
  default: return SettingTarget::None;
  }
}

// LOOK > RE-ROLL: the decor with a new seed (new wear, tapes and screws), the user's screw rotations back to 0 and the
// tapes blank; the behaviour settings are kept. A seed of 0 or equal to the current one is replaced by another one.
inline Decor rerolledDecor(Decor d, uint32_t newSeed) {
  if (newSeed == 0 || newSeed == d.seed) newSeed = (d.seed ^ 0x9E3779B9u) * 2654435761u + 0x7F4A7C15u;
  if (newSeed == 0 || newSeed == d.seed) newSeed = d.seed + 1u ? d.seed + 1u : 1u;
  d.seed = newSeed;
  for (float& a : d.screwAngles) a = 0.f;
  d.tapeTexts.clear();
  return d;
}

// The whole back for the given preferences and behaviour (no callbacks).
inline FairyTapes::Ui::BackContent backContent(const Prefs& p, const Behaviour& b) {
  using namespace FairyTapes::Ui;
  BackContent c;
  c.headerLine = "PILLOW TALK  \xC2\xB7  TAPE ECHO VST3";
  c.tabs = {{"SETTINGS", BackTabKind::Settings}, {"CREDITS", BackTabKind::Credits}};
  const auto A = BackColumn::Appearance, B = BackColumn::Behaviour;
  c.items.push_back(choiceItem(kSetWear, A, "WEAR", "", {"OFF", "LIGHT", "HEAVY"}, selectedFor(kSetWear, p, b)));
  c.items.push_back(toggleItem(kSetNotes, A, "NOTES LAYER", "tape and pen marks on the values", "HIDE", "SHOW", p.notes));
  c.items.push_back(stepperItem(kSetTapeFont, A, "TAPE FONT", "handwriting on the tape", {"Zeyada", "Rock Salt", "Homemade Apple"},
                                selectedFor(kSetTapeFont, p, b), true));
  c.items.push_back(toggleItem(kSetValuesFont, A, "VALUES FONT", "values on the panel", "STANDARD", "TAPE FONT", p.handwrittenValues, 118));
  c.items.push_back(buttonItem(kSetLook, A, "LOOK", "RE-ROLL", "new wear, dirt and screws"));
  c.items.push_back(choiceItem(kSetDisplayWindow, B, "DISPLAY WINDOW", "time shown by the ducking display", {"3 S", "6 S", "12 S"},
                               selectedFor(kSetDisplayWindow, p, b)));
  c.items.push_back(choiceItem(kSetDisplayPeaks, B, "DISPLAY PEAKS", "hold the highest peak", {"OFF", "1.5 S", "3 S"},
                               selectedFor(kSetDisplayPeaks, p, b)));
  c.items.push_back(choiceItem(kSetTempoFallback, B, "TEMPO FALLBACK", "when the host sends no tempo", {"120 BPM"},
                               selectedFor(kSetTempoFallback, p, b), 120));
  c.notes = {
      "Appearance settings belong to you: they are kept in your preferences and apply to every project and every instance.",
      "Behaviour settings change how the plugin works: they are saved in the project, together with the parameters.",
      "The wear layout (scratches, tape, screws) is generated once per instance from a saved seed and never changes by itself.",
      "Double-click on a knob returns it to its neutral default.",
  };
  c.credits = backCredits();
  return c;
}

} // namespace FairyTapes::PillowTalk::Ui
