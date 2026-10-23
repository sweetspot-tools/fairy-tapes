/**
 * Fairy Tapes · Pillow Talk — editor layout table, dimming rules, printed items.
 * Header-only and free of VSTGUI so it is unit tested (tests/test_layout.cpp). Coordinates are logical px of the
 * 1000 x 470 panel of src/pillow-talk/docs/mockups/pillow_rack.html (2026-10-21: DELAY and AUTO DUCK on one row; with
 * SYNC on, the TIME or HOLD knob gives its place to the four note values).
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
    {Box::Delay, {24, 282, 520, 150}, "DELAY"},
    {Box::AutoDuck, {550, 282, 426, 150}, "AUTO DUCK"},
};

constexpr const BoxSpec& boxOf(Box b) {
  for (const auto& s : kBoxes)
    if (s.box == b) return s;
  return kBoxes[0];
}

// ---- Controls -------------------------------------------------------------------------------------------------------
enum class Kind { Knob, Toggle, Choice };
// docs/DESIGN_SYSTEM.md (three button types, 2026-10-21): on/off buttons (Bypass, Sidechain, Sync) have a red LED; the
// note values are exclusive choices with the name on the face, which lights amber (no LED).
enum class Led { None, Red };

struct ControlSpec {
  ParamID id;
  Kind kind;
  Box box;
  Rect rect;              // Knob: the cell (cap centre 30 px below the top). Toggle: the face. Choice: the grid of faces.
  const char* name;       // Knob: printed under the cap. Toggle: printed above the face. Choice: unused.
  const char* lo;         // Knob: end values of the printed scale
  const char* hi;
  float capRadius;        // Knob
  Led led;
  int choiceCount;        // Choice: number of buttons, kNoteColumns per row, names on the faces
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
// The four note values in place of the TIME (or HOLD) knob centred at (cx, cy): a 2 x 2 grid of 34 px faces, centres
// 40 px apart across and 26 px down, the first at (cx - 20, cy - 16) (preview noteGrid).
constexpr int kNoteColumns = 2;
constexpr float kNoteFaceWidth = 34;
constexpr Rect notes(float cx, float cy) { return {cx - 40, cy - 29, 80, 52}; }

constexpr float kRowY = 352;   // DELAY and AUTO DUCK share the row

constexpr ControlSpec kControls[] = {
    // DUCKING: the scope (kScopeRect) and the sidechain switch for the detector
    {kSidechain, Kind::Toggle, Box::Ducking, face(472, 150), "SIDECHAIN", nullptr, nullptr, 0, Led::Red, 0, {}},
    // OUTPUT: Mix in the middle, the global Bypass at the top right
    {kMix, Kind::Knob, Box::Output, knob(891, 176), "MIX", "DRY", "WET", kMixRadius, Led::None, 0, {}},
    {kBypass, Kind::Toggle, Box::Output, face(940, 130), "BYPASS", nullptr, nullptr, 0, Led::Red, 0, {}},
    // DELAY
    {kTimeMs, Kind::Knob, Box::Delay, knob(84, kRowY), "TIME", "20", "1200", kKnobRadius, Led::None, 0, {}},
    {kFeedback, Kind::Knob, Box::Delay, knob(174, kRowY), "FEEDBACK", "0", "95", kKnobRadius, Led::None, 0, {}},
    {kTone, Kind::Knob, Box::Delay, knob(264, kRowY), "TONE", ".8k", "12k", kKnobRadius, Led::None, 0, {}},
    {kSaturation, Kind::Knob, Box::Delay, knob(354, kRowY), "SATURATION", "0", "10", kKnobRadius, Led::None, 0, {}},
    {kWowFlutter, Kind::Knob, Box::Delay, knob(446, kRowY), "WOW/FLUTTER", "0", "10", kKnobRadius, Led::None, 0, {}},
    {kTimeSync, Kind::Toggle, Box::Delay, face(515, 320), "SYNC", nullptr, nullptr, 0, Led::Red, 0, {}},   // top right corner
    {kTimeNote, Kind::Choice, Box::Delay, notes(84, kRowY), nullptr, nullptr, nullptr, 0, Led::None, 4,
     {"1/16", "1/8", "1/4", "1/2"}},   // in place of TIME while SYNC is on
    // AUTO DUCK
    {kThreshold, Kind::Knob, Box::AutoDuck, knob(606, kRowY), "THRESHOLD", "-60", "0", kKnobRadius, Led::None, 0, {}},
    {kDuckDepth, Kind::Knob, Box::AutoDuck, knob(696, kRowY), "DEPTH", "0", "mute", kKnobRadius, Led::None, 0, {}},
    {kHoldMs, Kind::Knob, Box::AutoDuck, knob(796, kRowY), "HOLD", "50", "1k", kKnobRadius, Led::None, 0, {}},
    {kRelease, Kind::Knob, Box::AutoDuck, knob(886, kRowY), "RELEASE", "50", "2k", kKnobRadius, Led::None, 0, {}},
    {kHoldSync, Kind::Toggle, Box::AutoDuck, face(947, 320), "SYNC", nullptr, nullptr, 0, Led::Red, 0, {}},
    {kHoldNote, Kind::Choice, Box::AutoDuck, notes(796, kRowY), nullptr, nullptr, nullptr, 0, Led::None, 4,
     {"1/16", "1/8", "1/4", "1/2"}},   // in place of HOLD while SYNC is on
};

inline const ControlSpec* controlOf(ParamID id) {
  for (const auto& c : kControls)
    if (c.id == id) return &c;
  return nullptr;
}

constexpr float knobCentreX(const ControlSpec& c) { return c.rect.x + c.rect.w / 2; }
constexpr float knobCentreY(const ControlSpec& c) { return c.rect.y + kKnobCentreOffsetY; }

// The rect of the control's view: the knob cell, the face plus the LED zone under it, or the grid of note faces.
constexpr Rect viewRect(const ControlSpec& c) {
  return c.kind == Kind::Toggle ? Rect{c.rect.x, c.rect.y, c.rect.w, c.rect.h + kLedZone} : c.rect;
}

// The two controls that take turns in one place: the millisecond knob and its note values (SYNC off / on).
constexpr bool sharesPlace(ParamID a, ParamID b) {
  return (a == kTimeMs && b == kTimeNote) || (a == kTimeNote && b == kTimeMs) || (a == kHoldMs && b == kHoldNote) ||
         (a == kHoldNote && b == kHoldMs);
}

// ---- Dimming --------------------------------------------------------------------------------------------------------
struct DimState {
  bool bypass = false;     // the global bypass dims everything but itself
  bool timeSync = false;   // Sync on: the note values take the place of the millisecond knob; off: the knob is back
  bool holdSync = false;
};

inline bool controlDimmed(ParamID id, const DimState& s) { return id != kBypass && s.bypass; }

inline bool controlHidden(ParamID id, const DimState& s) {
  switch (id) {
  case kTimeMs: return s.timeSync;
  case kTimeNote: return !s.timeSync;
  case kHoldMs: return s.holdSync;
  case kHoldNote: return !s.holdSync;
  default: return false;
  }
}

// The name under the TIME and HOLD place stays printed whichever control is shown (its own silkscreen key).
constexpr int kPlaceLabelKey = 1000;
constexpr int placeLabelKey(ParamID id) { return kPlaceLabelKey + static_cast<int>(id); }

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
};

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
      if (c.id == kTimeMs || c.id == kHoldMs) {   // the name stays when the note values take the place
        PrintedItem label;
        label.id = placeLabelKey(c.id);
        label.kind = PrintKind::Label;
        label.cx = it.cx;
        label.cy = FairyTapes::Ui::Rack::nameBaseline(it.cy, c.capRadius);
        label.name = c.name;
        out.push_back(std::move(label));
        it.name.clear();
      }
      break;
    case Kind::Toggle:
      it.kind = PrintKind::Button;
      it.cy = c.rect.y + c.rect.h / 2;
      it.name = c.name;
      break;
    case Kind::Choice: continue;   // the names are on the faces
    }
    out.push_back(std::move(it));
  }
  return out;
}

// ---- Decor: base positions of the paper tapes and the corner screws (ui/decor.h adds the seeded variation) ---------
// Base centre and how far the seed may move it (+-jx, +-jy). Chosen so that no tape (up to 96 x 26, turned up to 9 degrees,
// with its torn ends) ever covers a control, the scope, the LEVEL rows or the clip LEDs, nor another tape
// (tests/test_decor.cpp): in the header, in the free bands between the rows of boxes, and in the empty corners of
// DELAY and AUTO DUCK. A tape may lie over printed text and box borders, as real tape would. Re-placed for the 1000 x 470
// panel on 2026-10-23: in the header, in the band between the two rows, in the empty tops of DELAY and AUTO DUCK and in
// the band under them.
struct TapeSlot {
  float cx, cy, jx, jy;
};
constexpr TapeSlot kTapeSlots[] = {
    {430.f, 50.f, 50.f, 10.f},   // header, between the title and the tagline
    {130.f, 273.f, 40.f, 3.f},   // band between DUCKING and DELAY, left
    {600.f, 273.f, 30.f, 3.f},   // band between LEVEL and the DELAY / AUTO DUCK row
    {360.f, 296.f, 40.f, 2.f},   // DELAY, above the knobs
    {840.f, 296.f, 30.f, 2.f},   // AUTO DUCK, above the knobs, left of SYNC
    {500.f, 440.f, 60.f, 3.f},   // band under the boxes, where DELAY meets AUTO DUCK
};
constexpr int kNumTapeSlots = static_cast<int>(sizeof(kTapeSlots) / sizeof(kTapeSlots[0]));
constexpr float kScrewPos[4][2] = {{18.f, 18.f}, {982.f, 18.f}, {18.f, 452.f}, {982.f, 452.f}};

} // namespace FairyTapes::PillowTalk::Ui
