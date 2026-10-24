/**
 * Fairy Tapes · Hotline — editor layout table, IN-button polarity, dimming rules, printed items.
 * Header-only and free of VSTGUI so it is unit tested (tests/test_hotline.cpp). Every coordinate is a logical px of the
 * 1000 x 470 panel of src/hotline/docs/mockups/hotline_rack.html, front side (rows A and B, as tuned on 2026-10-21:
 * narrower Device buttons, CHARACTER knobs closer, IN a bit lower). The preview wins over any number in the plan.
 * The back of the panel, wear, screws and paper tapes are not here (Task 8).
 */

#pragma once

#include "params.h"
#include "ui/editor_size.h"

#include "fairy_ui/sanitize.h"
#include "fairy_ui/silkscreen.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace FairyTapes::Hotline::Ui {

using Steinberg::Vst::ParamID;

struct Rect {
  float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
};

// ---- Modules and boxes ----------------------------------------------------------------------------------------------
// None: not in a box (never used by the control table). Level has no controls, only the meters.
enum class Module { None, Device, Level, Output, Morph, Character };

struct BoxSpec {
  Module module;
  Rect rect;
  const char* title;   // printed in a gap of the bottom border
};

constexpr BoxSpec kBoxes[] = {
    {Module::Device, {24, 92, 306, 156}, "DEVICE"},
    {Module::Level, {336, 92, 448, 156}, "LEVEL"},
    {Module::Output, {790, 92, 186, 156}, "OUTPUT"},
    {Module::Morph, {24, 268, 240, 172}, "MORPH"},
    {Module::Character, {270, 268, 706, 172}, "CHARACTER"},
};

constexpr const BoxSpec& boxOf(Module m) {
  for (const auto& b : kBoxes)
    if (b.module == m) return b;
  return kBoxes[0];
}

// ---- Controls -------------------------------------------------------------------------------------------------------
enum class Kind { Knob, Toggle, Choice };
// docs/DESIGN_SYSTEM.md: on/off buttons have an LED (red; the preview gives MONO a yellow one); the device choice has the
// name on the face and lights it amber (no LED).
enum class Led { None, Red, Yellow };

constexpr int kMaxChoices = kNumDevices;

struct ControlSpec {
  ParamID id;
  Kind kind;
  Module module;          // the box the control stands in (and the module whose IN it follows)
  Rect rect;              // Knob: the cell (cap centre knobCentreOffsetY below its top). Toggle: the face. Choice: the grid.
  const char* name;       // Knob: printed under the cap. Toggle: printed above the face. Choice: unused.
  const char* lo;         // Knob: end values of the printed scale
  const char* hi;
  float capRadius;        // Knob
  Led led;
  bool isIn;              // the IN button of its module: lit = module in, follows the global bypass only
  int choiceCount;        // Choice: number of buttons, `columns` per row, names on the faces
  int columns;
  float faceWidth;        // Choice: width of one face
  const char* choices[kMaxChoices];
};

// A knob cell is AnalogKnob::kPreferredWidth x kPreferredHeight with the cap centre 30 px below the top. The Morph cap
// (radius 34, skirt 37) does not fit, so the cell of a big knob is wider and 8 px taller above: the editor draws the cap
// that much lower in it (BigRackKnob).
constexpr float kKnobCellW = 64, kKnobCellH = 88, kKnobCentreOffsetY = 30;
constexpr float kBigKnobMinRadius = 28, kBigKnobExtra = 8, kBigKnobCellW = 76;
constexpr bool isBigKnob(float r) { return r >= kBigKnobMinRadius; }
constexpr float knobCentreOffsetY(float r) { return kKnobCentreOffsetY + (isBigKnob(r) ? kBigKnobExtra : 0.f); }
constexpr Rect knob(float cx, float cy, float r) {
  const float w = isBigKnob(r) ? kBigKnobCellW : kKnobCellW;
  const float off = knobCentreOffsetY(r);
  return {cx - w / 2, cy - off, w, kKnobCellH + off - kKnobCentreOffsetY};
}
// Button face centred on (cx, cy): 24 x 22 (Rack::kFaceWidth x kFaceHeight).
constexpr Rect face(float cx, float cy) { return {cx - 12, cy - 11, 24, 22}; }
// Room under a face for its LED: the view of a toggle is taller than its face.
constexpr float kLedZone = 14;

constexpr float kStdRadius = 22, kMorphRadius = 34;

constexpr int kDeviceColumns = 3;
constexpr float kDeviceFaceWidth = 64;
// The six device buttons: cells 90 x 62 around the centres (87 | 177 | 267, 141 | 203) of the preview.
constexpr Rect kDeviceGrid{42, 110, 270, 124};

constexpr float kCharacterRowY = 349;   // cap centre of the CHARACTER knobs

constexpr ControlSpec kControls[] = {
    // DEVICE
    {kDevice, Kind::Choice, Module::Device, kDeviceGrid, nullptr, nullptr, nullptr, 0, Led::None, false, kNumDevices,
     kDeviceColumns, kDeviceFaceWidth, {"MEGA", "PHONE", "WALKIE", "AM RADIO", "PA", "CHEAP"}},
    // OUTPUT: GAIN in the middle left, the global BYPASS at the top right and MONO below it
    {kGain, Kind::Knob, Module::Output, knob(842, 164, kStdRadius), "GAIN", "-20", "+20", kStdRadius, Led::None, false, 0, 0, 0, {}},
    {kBypass, Kind::Toggle, Module::Output, face(940, 134), "BYPASS", nullptr, nullptr, 0, Led::Red, false, 0, 0, 0, {}},
    {kMono, Kind::Toggle, Module::Output, face(940, 206), "MONO", nullptr, nullptr, 0, Led::Yellow, false, 0, 0, 0, {}},
    // MORPH
    {kMorph, Kind::Knob, Module::Morph, knob(144, 337, kMorphRadius), "MORPH", "CLEAN", "FULL", kMorphRadius, Led::None, false, 0, 0, 0, {}},
    // CHARACTER: five knobs and the IN button at the top right corner
    {kDrive, Kind::Knob, Module::Character, knob(354, kCharacterRowY, kStdRadius), "DRIVE", "0", "10", kStdRadius, Led::None, false, 0, 0, 0, {}},
    {kTone, Kind::Knob, Module::Character, knob(482, kCharacterRowY, kStdRadius), "TONE", "DARK", "BRIGHT", kStdRadius, Led::None, false, 0, 0, 0, {}},
    {kNoise, Kind::Knob, Module::Character, knob(610, kCharacterRowY, kStdRadius), "NOISE", "0", "10", kStdRadius, Led::None, false, 0, 0, 0, {}},
    {kRoom, Kind::Knob, Module::Character, knob(738, kCharacterRowY, kStdRadius), "ROOM", "0", "10", kStdRadius, Led::None, false, 0, 0, 0, {}},
    {kMix, Kind::Knob, Module::Character, knob(866, kCharacterRowY, kStdRadius), "MIX", "DRY", "WET", kStdRadius, Led::None, false, 0, 0, 0, {}},
    {kCharacterIn, Kind::Toggle, Module::Character, face(948, 304), "IN", nullptr, nullptr, 0, Led::Red, true, 0, 0, 0, {}},
};

constexpr int kControlCount = static_cast<int>(sizeof(kControls) / sizeof(kControls[0]));

inline const ControlSpec* controlOf(ParamID id) {
  for (const auto& c : kControls)
    if (c.id == id) return &c;
  return nullptr;
}

constexpr float knobCentreX(const ControlSpec& c) { return c.rect.x + c.rect.w / 2; }
constexpr float knobCentreY(const ControlSpec& c) { return c.rect.y + knobCentreOffsetY(c.capRadius); }

// The rect of the control's view: the knob cell, the face plus the LED zone under it, or the grid of device faces.
constexpr Rect viewRect(const ControlSpec& c) {
  return c.kind == Kind::Toggle ? Rect{c.rect.x, c.rect.y, c.rect.w, c.rect.h + kLedZone} : c.rect;
}

// ---- Dimming --------------------------------------------------------------------------------------------------------
// The IN button of CHARACTER is lit = in (ParamID kCharacterIn: 1 = in, same polarity as the UI: no inversion). The other
// modules have no IN. The global bypass dims everything but the BYPASS button itself.
struct ModuleState {
  bool characterIn = true;
  bool bypass = false;
};

inline bool moduleDimmed(Module m, const ModuleState& s) { return s.bypass || (m == Module::Character && !s.characterIn); }

// The IN button follows the global bypass only (it must stay usable to switch the module back in).
inline bool controlDimmed(const ControlSpec& c, const ModuleState& s) {
  if (c.id == kBypass) return false;
  return c.isIn ? s.bypass : moduleDimmed(c.module, s);
}

// ---- LEVEL: LED rows and clip LEDs ----------------------------------------------------------------------------------
constexpr int kMeterLeds = 24;
constexpr float kMeterFirstX = 398, kMeterPitch = 14.5f, kMeterBarHeight = 8;   // centre of the first LED, LED pitch

struct MeterRow {
  ParamID id;        // display parameter (norm over kMeterMinDb..kMeterMaxDb)
  float cy;
  bool alwaysLit;    // stays lit under the global bypass (all four, as in the preview)
};
constexpr MeterRow kMeterRows[] = {
    {kMeterInL, 124, true}, {kMeterInR, 138, true}, {kMeterOutL, 170, true}, {kMeterOutR, 184, true},
};
constexpr int kMeterRowCount = static_cast<int>(sizeof(kMeterRows) / sizeof(kMeterRows[0]));

constexpr Rect meterBarRect(const MeterRow& r) {
  return {kMeterFirstX - kMeterPitch / 2, r.cy - kMeterBarHeight / 2, kMeterPitch * kMeterLeds, kMeterBarHeight};
}

inline bool meterDimmed(const MeterRow& r, const ModuleState& s) { return s.bypass && !r.alwaysLit; }

struct ClipSpec {
  ParamID id;
  const char* label;
  float cx;           // LED centre
  bool alwaysLit;     // DRV follows the global bypass (the drive stage is out of the path), IN and OUT do not
};
constexpr int kClipCount = 3;
constexpr ClipSpec kClips[kClipCount] = {
    {kClipIn, "IN", kMeterFirstX + 4, true}, {kClipDrive, "DRV", kMeterFirstX + 74, false}, {kClipOut, "OUT", kMeterFirstX + 144, true}};
constexpr float kClipRowY = 218;
// ClipLed draws its LED (radius 3.4) 1 px from the left edge and the label after it.
constexpr Rect clipRect(const ClipSpec& c) { return {c.cx - 4.4f, kClipRowY - 8, 40, 16}; }

inline bool clipDimmed(const ClipSpec& c, const ModuleState& s) { return s.bypass && !c.alwaysLit; }

// ---- Panel readouts (the yellow value under a knob, as the preview prints it) -------------------------------------------
// Drive, Noise and Room: 0..10 with one decimal. Tone: -5.0..+5.0. Gain: "+x.x dB". Morph and Mix: percent. Others: none.
inline std::string panelValueText(ParamID id, double norm) {
  const double n = FairyTapes::Ui::sanitizeNorm(norm, 0.0);
  char buf[32] = {0};
  const auto signedOneDecimal = [&](double v) {
    const double r = std::round(v * 10.0) / 10.0;
    if (r == 0.0) return std::string("0.0");
    std::snprintf(buf, sizeof buf, "%+.1f", r);
    return std::string(buf);
  };
  switch (id) {
  case kDrive:
  case kNoise:
  case kRoom: std::snprintf(buf, sizeof buf, "%.1f", n * 10.0); return buf;
  case kTone: return signedOneDecimal((n - 0.5) * 10.0);
  case kGain: return signedOneDecimal(gainDbFromNorm(n)) + " dB";
  case kMorph:
  case kMix: std::snprintf(buf, sizeof buf, "%.0f %%", n * 100.0); return buf;
  default: return {};
  }
}

// ---- Printed on the panel (panel layer, below the silkscreen) -------------------------------------------------------
enum class TextStyle {
  Title,      // 38 px bold italic, white
  Brand,      // 11 px bold, dim white
  Tagline,    // 13 px bold, white
  Section,    // 11 px bold, yellow (IN / OUT / CLIP of the LEVEL box)
  RowTag,     // 11 px bold, white (L / R)
  Scale,      // 9 px bold, dim white (dB scale)
};
using FairyTapes::Ui::TextAlign;

struct PanelText {
  const char* text;
  float x, y;   // y is the baseline
  TextStyle style;
  TextAlign align;
};

constexpr float kRowTagX = kMeterFirstX - 12, kSectionTagX = kMeterFirstX - 34;
// LED under a dB scale label: the 24 LEDs span kMeterMinDb..kMeterMaxDb (66 dB in 23 steps).
constexpr float scaleX(int db) {
  return kMeterFirstX + kMeterPitch * static_cast<int>((db + 60) / (66.0f / (kMeterLeds - 1)) + 0.5f);
}
constexpr float kScaleY = 157;

constexpr PanelText kPanelTexts[] = {
    {"HOTLINE", 34, 58, TextStyle::Title, TextAlign::Left},
    {"FAIRY TAPES", 36, 78, TextStyle::Brand, TextAlign::Left},
    {"MEGAPHONE  \xC2\xB7  PHONE  \xC2\xB7  WALKIE-TALKIE  \xC2\xB7  AM RADIO  \xC2\xB7  PA  \xC2\xB7  CHEAP SPEAKER", 966, 46,
     TextStyle::Tagline, TextAlign::Right},
    // LEVEL
    {"IN", kSectionTagX, 134, TextStyle::Section, TextAlign::Centre},
    {"OUT", kSectionTagX, 180, TextStyle::Section, TextAlign::Centre},
    {"CLIP", kSectionTagX, 222, TextStyle::Section, TextAlign::Centre},
    {"L", kRowTagX, 128, TextStyle::RowTag, TextAlign::Right},
    {"R", kRowTagX, 142, TextStyle::RowTag, TextAlign::Right},
    {"L", kRowTagX, 174, TextStyle::RowTag, TextAlign::Right},
    {"R", kRowTagX, 188, TextStyle::RowTag, TextAlign::Right},
    {"-60", scaleX(-60), kScaleY, TextStyle::Scale, TextAlign::Centre},
    {"-48", scaleX(-48), kScaleY, TextStyle::Scale, TextAlign::Centre},
    {"-36", scaleX(-36), kScaleY, TextStyle::Scale, TextAlign::Centre},
    {"-24", scaleX(-24), kScaleY, TextStyle::Scale, TextAlign::Centre},
    {"-18", scaleX(-18), kScaleY, TextStyle::Scale, TextAlign::Centre},
    {"-12", scaleX(-12), kScaleY, TextStyle::Scale, TextAlign::Centre},
    {"-9", scaleX(-9), kScaleY, TextStyle::Scale, TextAlign::Centre},
    {"-6", scaleX(-6), kScaleY, TextStyle::Scale, TextAlign::Centre},
    {"-3", scaleX(-3), kScaleY, TextStyle::Scale, TextAlign::Centre},
    {"0", scaleX(0), kScaleY, TextStyle::Scale, TextAlign::Centre},
    {"+3", scaleX(3), kScaleY, TextStyle::Scale, TextAlign::Centre},
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

} // namespace FairyTapes::Hotline::Ui
