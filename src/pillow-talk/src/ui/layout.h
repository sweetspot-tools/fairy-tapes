/**
 * Fairy Tapes · Pillow Talk — editor layout table, dimming rules, printed items.
 * Header-only and free of VSTGUI so it is unit tested (tests/test_layout.cpp). Coordinates are logical px of the
 * 1000 x 628 panel of src/pillow-talk/docs/mockups/pillow_rack.html (row C of the preview is drawn 32 px higher
 * than its source numbers: the AUTO DUCK box sits at y 452).
 */

#pragma once

#include "params.h"
#include "ui/editor_size.h"

#include "fairy_ui/sanitize.h"
#include "fairy_ui/silkscreen.h"

#include <vector>

namespace FairyTapes::PillowTalk::Ui {

using Steinberg::Vst::ParamID;

struct Rect {
  float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
};

// ---- Boxes ----------------------------------------------------------------------------------------------------------
enum class Box { Ducking, Level, Output, Delay, AutoDuck };

struct BoxSpec {
  Box box;
  Rect rect;
  const char* title;   // printed in a gap of the bottom border
};

constexpr BoxSpec kBoxes[] = {
    {Box::Ducking, {24, 92, 500, 170}, "DUCKING"},
    {Box::Level, {530, 92, 270, 170}, "LEVEL"},
    {Box::Output, {806, 92, 170, 170}, "OUTPUT"},
    {Box::Delay, {24, 282, 952, 150}, "DELAY"},
    {Box::AutoDuck, {24, 452, 952, 146}, "AUTO DUCK"},
};

constexpr const BoxSpec& boxOf(Box b) {
  for (const auto& s : kBoxes)
    if (s.box == b) return s;
  return kBoxes[0];
}

// ---- Controls -------------------------------------------------------------------------------------------------------
enum class Kind { Knob, Toggle, Choice };
// docs/DESIGN_SYSTEM.md: on/off buttons (Bypass, Sidechain, Sync) red, value selectors (note buttons) yellow.
enum class Led { None, Red, Yellow };

struct ControlSpec {
  ParamID id;
  Kind kind;
  Box box;
  Rect rect;              // Knob: the cell (cap centre 30 px below the top). Toggle / Choice: the button face(s).
  const char* name;       // Knob: printed under the cap. Toggle: printed above the face. Choice: unused.
  const char* lo;         // Knob: end values of the printed scale
  const char* hi;
  float capRadius;        // Knob
  Led led;
  int choiceCount;        // Choice: number of buttons (equal slots over rect.w), labels printed above them
  const char* choices[4];
};

// Cell of a knob: AnalogKnob::kPreferredWidth x kPreferredHeight, cap centre kCenterOffsetY below the top.
constexpr float kKnobCellW = 64, kKnobCellH = 88, kKnobCentreOffsetY = 30;
constexpr Rect knob(float cx, float cy) { return {cx - kKnobCellW / 2, cy - kKnobCentreOffsetY, kKnobCellW, kKnobCellH}; }
// Button face(s) centred on (cx, cy): 24 x 22 per button (Rack::kFaceWidth x kFaceHeight).
constexpr Rect face(float cx, float cy, float w = 24) { return {cx - w / 2, cy - 11, w, 22}; }
// Room under a face for its LED: the view of a button is taller than its face.
constexpr float kLedZone = 14;

constexpr float kKnobRadius = 19, kMixRadius = 22;
constexpr float kNotePitch = 64;   // one note button every 64 px, the first centred at x 700
constexpr Rect notes(float cy) { return face(700 + 1.5f * kNotePitch, cy, 4 * kNotePitch); }

constexpr float kDelayRowY = 352, kDuckRowY = 520;

constexpr ControlSpec kControls[] = {
    // DUCKING: the scope (kScopeRect) and the sidechain switch for the detector
    {kSidechain, Kind::Toggle, Box::Ducking, face(472, 150), "SIDECHAIN", nullptr, nullptr, 0, Led::Red, 0, {}},
    // OUTPUT: Mix in the middle, the global Bypass at the top right
    {kMix, Kind::Knob, Box::Output, knob(891, 176), "MIX", "DRY", "WET", kMixRadius, Led::None, 0, {}},
    {kBypass, Kind::Toggle, Box::Output, face(940, 130), "BYPASS", nullptr, nullptr, 0, Led::Red, 0, {}},
    // DELAY
    {kTimeMs, Kind::Knob, Box::Delay, knob(116, kDelayRowY), "TIME", "20", "1200", kKnobRadius, Led::None, 0, {}},
    {kFeedback, Kind::Knob, Box::Delay, knob(208, kDelayRowY), "FEEDBACK", "0", "95", kKnobRadius, Led::None, 0, {}},
    {kTone, Kind::Knob, Box::Delay, knob(300, kDelayRowY), "TONE", ".8k", "12k", kKnobRadius, Led::None, 0, {}},
    {kSaturation, Kind::Knob, Box::Delay, knob(392, kDelayRowY), "SATURATION", "0", "10", kKnobRadius, Led::None, 0, {}},
    {kWowFlutter, Kind::Knob, Box::Delay, knob(484, kDelayRowY), "WOW/FLUTTER", "0", "10", kKnobRadius, Led::None, 0, {}},
    {kTimeSync, Kind::Toggle, Box::Delay, face(604, kDelayRowY), "SYNC", nullptr, nullptr, 0, Led::Red, 0, {}},
    {kTimeNote, Kind::Choice, Box::Delay, notes(kDelayRowY), nullptr, nullptr, nullptr, 0, Led::Yellow, 4,
     {"1/16", "1/8", "1/4", "1/2"}},
    // AUTO DUCK
    {kThreshold, Kind::Knob, Box::AutoDuck, knob(116, kDuckRowY), "THRESHOLD", "-60", "0", kKnobRadius, Led::None, 0, {}},
    {kDuckDepth, Kind::Knob, Box::AutoDuck, knob(208, kDuckRowY), "DEPTH", "0", "mute", kKnobRadius, Led::None, 0, {}},
    {kHoldMs, Kind::Knob, Box::AutoDuck, knob(300, kDuckRowY), "HOLD", "50", "1k", kKnobRadius, Led::None, 0, {}},
    {kRelease, Kind::Knob, Box::AutoDuck, knob(392, kDuckRowY), "RELEASE", "50", "2k", kKnobRadius, Led::None, 0, {}},
    {kHoldSync, Kind::Toggle, Box::AutoDuck, face(604, kDuckRowY), "SYNC", nullptr, nullptr, 0, Led::Red, 0, {}},
    {kHoldNote, Kind::Choice, Box::AutoDuck, notes(kDuckRowY), nullptr, nullptr, nullptr, 0, Led::Yellow, 4,
     {"1/16", "1/8", "1/4", "1/2"}},
};

inline const ControlSpec* controlOf(ParamID id) {
  for (const auto& c : kControls)
    if (c.id == id) return &c;
  return nullptr;
}

constexpr float knobCentreX(const ControlSpec& c) { return c.rect.x + c.rect.w / 2; }
constexpr float knobCentreY(const ControlSpec& c) { return c.rect.y + kKnobCentreOffsetY; }

// The rect of the control's view: the knob cell, or the face(s) plus the LED zone under them.
constexpr Rect viewRect(const ControlSpec& c) {
  return c.kind == Kind::Knob ? c.rect : Rect{c.rect.x, c.rect.y, c.rect.w, c.rect.h + kLedZone};
}

// ---- Dimming --------------------------------------------------------------------------------------------------------
struct DimState {
  bool bypass = false;     // the global bypass dims everything but itself
  bool timeSync = false;   // Sync on: the millisecond knob is dimmed; off: the note buttons are
  bool holdSync = false;
};

inline bool controlDimmed(ParamID id, const DimState& s) {
  if (id == kBypass) return false;
  if (s.bypass) return true;
  switch (id) {
  case kTimeMs: return s.timeSync;
  case kTimeNote: return !s.timeSync;
  case kHoldMs: return s.holdSync;
  case kHoldNote: return !s.holdSync;
  default: return false;
  }
}

// ---- DUCKING: the scope ---------------------------------------------------------------------------------------------
constexpr Rect kScopeRect{44, 108, 376, 130};

// ---- LEVEL: LED rows and clip LEDs ----------------------------------------------------------------------------------
constexpr int kMeterLeds = 12;
constexpr float kMeterFirstX = 598, kMeterPitch = 16, kMeterBarHeight = 8;   // centre of the first LED, LED pitch

struct MeterRow {
  ParamID id;           // display parameter (meter norm over -60..0 dB; kDispSend: linear send gain)
  float cy;
  bool gainReduction;   // the DUCK row: send level reduction (1 - send gain), amber
  bool alwaysLit;       // input and output stay lit under the global bypass (as in The Stripper)
};
constexpr MeterRow kMeterRows[] = {
    {kDispInL, 114, false, true}, {kDispInR, 128, false, true}, {kDispOutL, 156, false, true},
    {kDispOutR, 170, false, true}, {kDispSend, 198, true, false},
};
constexpr int kMeterRowCount = static_cast<int>(sizeof(kMeterRows) / sizeof(kMeterRows[0]));

constexpr Rect meterBarRect(const MeterRow& r) {
  return {kMeterFirstX - kMeterPitch / 2, r.cy - kMeterBarHeight / 2, kMeterPitch * kMeterLeds, kMeterBarHeight};
}

// The DUCK row shows how much the send is reduced: 1 - send gain (NaN: nothing reduced).
inline double duckMeterNorm(double sendNorm) { return 1.0 - FairyTapes::Ui::sanitizeNorm(sendNorm, 1.0); }

struct ClipSpec {
  ParamID id;
  const char* label;
  float cx;   // LED centre
};
constexpr int kClipCount = 2;
constexpr ClipSpec kClips[kClipCount] = {{kDispClipIn, "IN", 602}, {kDispClipOut, "OUT", 666}};
constexpr float kClipRowY = 238;
// ClipLed draws its LED (radius 3.4) 1 px from the left edge and the label after it.
constexpr Rect clipRect(const ClipSpec& c) { return {c.cx - 4.4f, kClipRowY - 8, 40, 16}; }

// ---- Printed on the panel (panel layer, below the silkscreen) -------------------------------------------------------
enum class TextStyle {
  Title,      // 38 px bold italic, white
  Brand,      // 11 px bold, dim white
  Tagline,    // 13 px bold, white
  Section,    // 11 px bold, yellow (IN / OUT / DUCK / CLIP of the LEVEL box)
  RowTag,     // 11 px bold, white (L / R)
  Scale,      // 9 px bold, dim white (dB scale)
  Note,       // 9 px bold, dim white (small explanations)
  SyncTitle,  // 10 px bold, dim white, over a rule
};
using FairyTapes::Ui::TextAlign;

struct PanelText {
  const char* text;
  float x, y;   // y is the baseline
  TextStyle style;
  TextAlign align;
};

constexpr float kRowTagX = kMeterFirstX - 12, kSectionTagX = kMeterFirstX - 36;
constexpr float scaleX(int db) {   // LED of the scale label (nearest of the 12 over -60..0 dB)
  return kMeterFirstX + kMeterPitch * static_cast<int>((db + 60) / (60.0f / (kMeterLeds - 1)) + 0.5f);
}

constexpr PanelText kPanelTexts[] = {
    {"PILLOW TALK", 34, 58, TextStyle::Title, TextAlign::Left},
    {"FAIRY TAPES", 36, 78, TextStyle::Brand, TextAlign::Left},
    {"TAPE ECHO  \xC2\xB7  AUTO DUCK  \xC2\xB7  SIDECHAIN", 966, 46, TextStyle::Tagline, TextAlign::Right},
    // DUCKING
    {"detector input", 472, 196, TextStyle::Note, TextAlign::Centre},
    // LEVEL
    {"IN", kSectionTagX, 124, TextStyle::Section, TextAlign::Centre},
    {"OUT", kSectionTagX, 166, TextStyle::Section, TextAlign::Centre},
    {"DUCK", kSectionTagX, 202, TextStyle::Section, TextAlign::Centre},
    {"CLIP", kSectionTagX, 242, TextStyle::Section, TextAlign::Centre},
    {"L", kRowTagX, 118, TextStyle::RowTag, TextAlign::Right},
    {"R", kRowTagX, 132, TextStyle::RowTag, TextAlign::Right},
    {"L", kRowTagX, 160, TextStyle::RowTag, TextAlign::Right},
    {"R", kRowTagX, 174, TextStyle::RowTag, TextAlign::Right},
    {"-60", scaleX(-60), 146, TextStyle::Scale, TextAlign::Centre},
    {"-48", scaleX(-48), 146, TextStyle::Scale, TextAlign::Centre},
    {"-36", scaleX(-36), 146, TextStyle::Scale, TextAlign::Centre},
    {"-24", scaleX(-24), 146, TextStyle::Scale, TextAlign::Centre},
    {"-12", scaleX(-12), 146, TextStyle::Scale, TextAlign::Centre},
    {"0", scaleX(0), 146, TextStyle::Scale, TextAlign::Centre},
    {"send level reduction", kMeterFirstX + 5 * kMeterPitch + 8, 216, TextStyle::Note, TextAlign::Centre},
    // DELAY / AUTO DUCK: titles of the Sync groups
    {"TIME SYNC", 750, 300, TextStyle::SyncTitle, TextAlign::Centre},
    {"HOLD SYNC", 750, 468, TextStyle::SyncTitle, TextAlign::Centre},
};

// Thin dim rules over the Sync groups.
struct PanelRule {
  float x0, x1, y;
};
constexpr PanelRule kPanelRules[] = {{560, 940, 306}, {560, 940, 474}};

// ---- Silkscreen: the printed parts of every control (fairy_ui/silkscreen.h), ids are ParamIDs ---------------------
inline std::vector<FairyTapes::Ui::PrintedItem> silkscreenItems() {
  using FairyTapes::Ui::PrintedItem;
  using FairyTapes::Ui::PrintKind;
  std::vector<PrintedItem> out;
  for (const ControlSpec& c : kControls) {
    PrintedItem it;
    it.id = static_cast<int>(c.id);
    it.cx = c.rect.x + c.rect.w / 2;
    switch (c.kind) {
    case Kind::Knob:
      it.kind = PrintKind::Knob;
      it.cy = knobCentreY(c);
      it.capRadius = c.capRadius;
      it.name = c.name;
      it.lo = c.lo;
      it.hi = c.hi;
      break;
    case Kind::Toggle:
      it.kind = PrintKind::Button;
      it.cy = c.rect.y + c.rect.h / 2;
      it.name = c.name;
      break;
    case Kind::Choice:
      it.kind = PrintKind::Choice;
      it.cy = c.rect.y + c.rect.h / 2;
      it.width = c.rect.w;
      for (int k = 0; k < c.choiceCount && k < 4; ++k) it.labels.push_back(c.choices[k]);
      break;
    }
    out.push_back(std::move(it));
  }
  return out;
}

// ---- Decor: base positions of the paper tapes and the corner screws (ui/decor.h adds the seeded variation) ---------
struct TapeSlot {
  float cx, cy;
};
constexpr TapeSlot kTapeSlots[] = {
    {150.f, 150.f}, {500.f, 135.f}, {850.f, 150.f}, {200.f, 540.f}, {550.f, 560.f}, {880.f, 535.f},
};
constexpr int kNumTapeSlots = static_cast<int>(sizeof(kTapeSlots) / sizeof(kTapeSlots[0]));
constexpr float kScrewPos[4][2] = {{18.f, 18.f}, {982.f, 18.f}, {18.f, 610.f}, {982.f, 610.f}};

} // namespace FairyTapes::PillowTalk::Ui
