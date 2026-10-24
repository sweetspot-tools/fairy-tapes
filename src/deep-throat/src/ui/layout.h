/**
 * Fairy Tapes · Deep Throat — editor layout table, dimming rules, printed items.
 * Header-only and free of VSTGUI so it is unit tested (tests/test_ui.cpp). Coordinates are logical px of the
 * 1000 x 614 panel of src/deep-throat/docs/mockups/deep_throat_rack.html. The contents of a module box (modBox in the
 * preview) are drawn there in a group shifted by -12 px in y: the numbers below are the FINAL ones (preview y - 12).
 * INPUT, SCOPE, LEVEL, OUTPUT and MODE are not shifted.
 */

#pragma once

#include "params.h"
#include "ui/editor_size.h"

#include "fairy_ui/sanitize.h"
#include "fairy_ui/silkscreen.h"

#include <vector>

namespace FairyTapes::DeepThroat::Ui {

struct Rect {
  float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
};

// ---- Boxes ----------------------------------------------------------------------------------------------------------
enum class Box { Input, Scope, Level, Output, Mode, Rider, Breath, Eq, DeEss, Comp, Tone };

struct BoxSpec {
  Box box;
  Rect rect;
  const char* title;   // printed in a gap of the bottom border
};

constexpr BoxSpec kBoxes[11] = {
    {Box::Input, {24, 96, 110, 130}, "INPUT"},
    {Box::Scope, {140, 96, 324, 130}, "SCOPE"},
    {Box::Level, {470, 96, 330, 130}, "LEVEL"},
    {Box::Output, {806, 96, 170, 130}, "OUTPUT"},
    {Box::Mode, {24, 238, 130, 174}, "MODE"},
    {Box::Rider, {160, 238, 234, 174}, "RIDER"},
    {Box::Breath, {400, 238, 355, 174}, "BREATH"},
    {Box::Eq, {761, 238, 215, 174}, "EQ"},
    {Box::DeEss, {24, 424, 237, 166}, "DE-ESS"},
    {Box::Comp, {267, 424, 403, 166}, "COMP"},
    {Box::Tone, {676, 424, 300, 166}, "TONE"},
};

constexpr const BoxSpec& boxOf(Box b) {
  for (const auto& s : kBoxes)
    if (s.box == b) return s;
  return kBoxes[0];
}

// ---- Modules (the six boxes with an IN button) ----------------------------------------------------------------------
enum class Module { None, Rider, Breath, Eq, DeEss, Comp, Tone };

constexpr Module moduleOf(Box b) {
  switch (b) {
  case Box::Rider: return Module::Rider;
  case Box::Breath: return Module::Breath;
  case Box::Eq: return Module::Eq;
  case Box::DeEss: return Module::DeEss;
  case Box::Comp: return Module::Comp;
  case Box::Tone: return Module::Tone;
  default: return Module::None;
  }
}

// The module a parameter is attenuated with when that module's IN is off. The IN buttons and the two Listen buttons are
// not part of it (they stay usable to switch the module back on / to listen); they are still dimmed by the global bypass.
constexpr Module moduleOf(ParamID id) {
  switch (id) {
  case kRiderTarget:
  case kRiderRange: return Module::Rider;
  case kBreathAmount:
  case kBreathSens: return Module::Breath;
  case kEqAmount: return Module::Eq;
  case kDeEssAmount:
  case kDeEssFreq: return Module::DeEss;
  case kCompAmount:
  case kCompMix:
  case kCompRatio:
  case kCompDist:
  case kCompHpf: return Module::Comp;
  case kTone: return Module::Tone;
  default: return Module::None;
  }
}

constexpr ParamID inParamOf(Module m) {
  switch (m) {
  case Module::Rider: return kRiderIn;
  case Module::Breath: return kBreathIn;
  case Module::Eq: return kEqIn;
  case Module::DeEss: return kDeEssIn;
  case Module::Comp: return kCompIn;
  case Module::Tone: return kToneIn;
  default: return kNumParams;
  }
}

// ---- Controls -------------------------------------------------------------------------------------------------------
// Knob: cap with printed scale. Toggle: on/off button, red LED. Choice: exclusive faces with the name on them (Mode).
// Cycle: one button that cycles a value, yellow LED, value printed under the LED (docs/DESIGN_SYSTEM.md, three button types).
enum class Kind { Knob, Toggle, Choice, Cycle };
enum class Led { None, Red, Yellow };

struct ControlSpec {
  ParamID id;
  Kind kind;
  Box box;
  Rect rect;         // Knob: the cell (cap centre 30 px below the top). Toggle, Cycle: the face. Choice: the stack of faces.
  const char* name;  // Knob: printed under the cap. Toggle, Cycle: printed above the face. Choice: unused.
  const char* lo;    // Knob: end values of the printed scale
  const char* hi;
  float capRadius;   // Knob
  Led led;
  int choiceCount;   // Choice, Cycle: number of values
  const char* choices[8];
};

// Cell of a knob: AnalogKnob::kPreferredWidth x kPreferredHeight, cap centre kCenterOffsetY below the top.
constexpr float kKnobCellW = 64, kKnobCellH = 88, kKnobCentreOffsetY = 30;
constexpr Rect knob(float cx, float cy) { return {cx - kKnobCellW / 2, cy - kKnobCentreOffsetY, kKnobCellW, kKnobCellH}; }
// Button face centred on (cx, cy): 24 x 22 (Rack::kFaceWidth x kFaceHeight).
constexpr Rect face(float cx, float cy) { return {cx - 12, cy - 11, 24, 22}; }
// Room under a face for its LED; a Cycle button adds the printed value under the LED (baseline 39 px below the centre).
constexpr float kLedZone = 14;
constexpr float kCycleViewW = 60, kCycleViewH = 53;   // from the face top to 3 px under the value baseline

// MODE: three faces 56 x 20, centres (89, 302 + 26 i).
constexpr float kModeFaceW = 56, kModeFaceH = 20, kModeFacePitch = 26, kModeCentreX = 89, kModeFirstCentreY = 302;
constexpr Rect modeFaceRect(int i) {
  return {kModeCentreX - kModeFaceW / 2, kModeFirstCentreY + kModeFacePitch * static_cast<float>(i) - kModeFaceH / 2, kModeFaceW,
          kModeFaceH};
}
constexpr Rect modeStack() { return {modeFaceRect(0).x, modeFaceRect(0).y, kModeFaceW, modeFaceRect(2).y + kModeFaceH - modeFaceRect(0).y}; }

constexpr float kKnobRadius = 19, kLargeRadius = 22, kToneRadius = 30;

// IN button of a module box: centre (x + w - inset, y + 38). The preview passes a larger inset for BREATH and DE-ESS.
constexpr float kInDefaultInset = 24, kInBreathInset = 34, kInDeEssInset = 36, kInOffsetY = 38;
constexpr Rect inFace(Box b, float inset) {
  const Rect& r = boxOf(b).rect;
  return face(r.x + r.w - inset, r.y + kInOffsetY);
}

constexpr ControlSpec kControls[25] = {
    // OUTPUT: global bypass at the top right, Output gain
    {kBypass, Kind::Toggle, Box::Output, face(946, 130), "BYPASS", nullptr, nullptr, 0, Led::Red, 0, {}},
    // MODE
    {kMode, Kind::Choice, Box::Mode, modeStack(), nullptr, nullptr, nullptr, 0, Led::None, kModeCount, {"SPEAK", "SING", "PUKE"}},
    // INPUT
    {kInput, Kind::Knob, Box::Input, knob(79, 150), "GAIN", "-12", "+12", kLargeRadius, Led::None, 0, {}},
    // RIDER (module content shifted by -12)
    {kRiderTarget, Kind::Knob, Box::Rider, knob(222, 310), "TARGET", "-30", "-10", kKnobRadius, Led::None, 0, {}},
    {kRiderRange, Kind::Knob, Box::Rider, knob(310, 310), "RANGE", "0", "1", kKnobRadius, Led::None, 0, {}},
    // BREATH
    {kBreathAmount, Kind::Knob, Box::Breath, knob(466, 310), "AMOUNT", "0", "12", kKnobRadius, Led::None, 0, {}},
    // DE-ESS
    {kDeEssAmount, Kind::Knob, Box::DeEss, knob(74, 510), "AMOUNT", "0", "1", kKnobRadius, Led::None, 0, {}},
    {kDeEssListen, Kind::Toggle, Box::DeEss, face(225, 536), "LISTEN", nullptr, nullptr, 0, Led::Red, 0, {}},
    // COMP
    {kCompAmount, Kind::Knob, Box::Comp, knob(312, 510), "AMOUNT", "-6", "-30", kKnobRadius, Led::None, 0, {}},
    // TONE
    {kTone, Kind::Knob, Box::Tone, knob(750, 510), "TONE", "-100", "+100", kToneRadius, Led::None, 0, {}},
    // OUTPUT
    {kOutput, Kind::Knob, Box::Output, knob(868, 150), "GAIN", "-12", "+12", kLargeRadius, Led::None, 0, {}},
    // the six IN buttons
    {kRiderIn, Kind::Toggle, Box::Rider, inFace(Box::Rider, kInDefaultInset), "IN", nullptr, nullptr, 0, Led::Red, 0, {}},
    {kBreathIn, Kind::Toggle, Box::Breath, inFace(Box::Breath, kInBreathInset), "IN", nullptr, nullptr, 0, Led::Red, 0, {}},
    {kEqIn, Kind::Toggle, Box::Eq, inFace(Box::Eq, kInDefaultInset), "IN", nullptr, nullptr, 0, Led::Red, 0, {}},
    {kDeEssIn, Kind::Toggle, Box::DeEss, inFace(Box::DeEss, kInDeEssInset), "IN", nullptr, nullptr, 0, Led::Red, 0, {}},
    {kCompIn, Kind::Toggle, Box::Comp, inFace(Box::Comp, kInDefaultInset), "IN", nullptr, nullptr, 0, Led::Red, 0, {}},
    {kToneIn, Kind::Toggle, Box::Tone, inFace(Box::Tone, kInDefaultInset), "IN", nullptr, nullptr, 0, Led::Red, 0, {}},
    // BREATH
    {kBreathSens, Kind::Knob, Box::Breath, knob(550, 310), "SENSITIVITY", "0", "1", kKnobRadius, Led::None, 0, {}},
    {kBreathListen, Kind::Toggle, Box::Breath, face(718, 352), "LISTEN", nullptr, nullptr, 0, Led::Red, 0, {}},
    // DE-ESS
    {kDeEssFreq, Kind::Knob, Box::DeEss, knob(156, 510), "FREQ", "4", "10", kKnobRadius, Led::None, 0, {}},
    // COMP
    {kCompMix, Kind::Knob, Box::Comp, knob(398, 510), "MIX", "0", "100", kKnobRadius, Led::None, 0, {}},
    {kCompRatio, Kind::Cycle, Box::Comp, face(488, 532), "RATIO", nullptr, nullptr, 0, Led::Yellow, kCompRatioCount,
     {"AUTO", "2:1", "3:1", "4:1", "6:1", "8:1", "10:1", "20:1"}},
    {kCompDist, Kind::Cycle, Box::Comp, face(552, 532), "DIST", nullptr, nullptr, 0, Led::Yellow, kCompDistCount,
     {"AUTO", "OFF", "DIST 2", "DIST 3"}},
    {kCompHpf, Kind::Cycle, Box::Comp, face(616, 532), "SC HPF", nullptr, nullptr, 0, Led::Yellow, kCompHpfCount,
     {"OFF", "80 Hz", "150 Hz"}},
    // EQ
    {kEqAmount, Kind::Knob, Box::Eq, knob(812, 310), "AMOUNT", "0", "100", kKnobRadius, Led::None, 0, {}},
};

inline const ControlSpec* controlOf(ParamID id) {
  for (const auto& c : kControls)
    if (c.id == id) return &c;
  return nullptr;
}

constexpr float knobCentreX(const ControlSpec& c) { return c.rect.x + c.rect.w / 2; }
constexpr float knobCentreY(const ControlSpec& c) { return c.rect.y + kKnobCentreOffsetY; }

// The rect of the control's view: the knob cell, the face plus the LED zone under it (a Cycle also the value text), or
// the stack of Mode faces.
constexpr Rect viewRect(const ControlSpec& c) {
  switch (c.kind) {
  case Kind::Toggle: return {c.rect.x, c.rect.y, c.rect.w, c.rect.h + kLedZone};
  case Kind::Cycle: return {c.rect.x + c.rect.w / 2 - kCycleViewW / 2, c.rect.y, kCycleViewW, kCycleViewH};
  default: return c.rect;
  }
}

// ---- Dimming --------------------------------------------------------------------------------------------------------
struct DimState {
  bool bypass = false;                                    // the global bypass dims everything but itself
  bool in[6] = {true, true, true, true, true, true};      // IN of Rider, Breath, Eq, DeEss, Comp, Tone (Module order - 1)
};

constexpr bool isModuleIn(ParamID id) {
  return id == kRiderIn || id == kBreathIn || id == kEqIn || id == kDeEssIn || id == kCompIn || id == kToneIn;
}

constexpr bool moduleOut(Module m, const DimState& s) { return m != Module::None && !s.in[static_cast<int>(m) - 1]; }

// Bypass dims all but itself (the IN buttons included); a module whose IN is off dims its own controls.
constexpr bool controlDimmed(ParamID id, const DimState& s) {
  if (id == kBypass) return false;
  return s.bypass || moduleOut(moduleOf(id), s);
}

// ---- SCOPE ----------------------------------------------------------------------------------------------------------
constexpr Rect kScopeRect{156, 106, 292, 84};

// ---- LEVEL: LED rows and clip LEDs ----------------------------------------------------------------------------------
constexpr int kMeterLeds = 15;
constexpr float kMeterFirstX = 528, kMeterPitch = 18, kMeterBarHeight = 8;   // centre of the first LED, LED pitch

struct MeterRow {
  Steinberg::Vst::ParamID id;   // display parameter (meter norm over -60..+6 dB; kDispGr: gain reduction over 0..-18 dB)
  float cy;
  bool gainReduction;   // the GR row: amber then red
  bool alwaysLit;       // input and output stay lit under the global bypass
};
constexpr MeterRow kMeterRows[5] = {
    {kDispInL, 110, false, true}, {kDispInR, 122, false, true}, {kDispOutL, 148, false, true},
    {kDispOutR, 160, false, true}, {kDispGr, 184, true, false},
};
constexpr int kMeterRowCount = 5;

constexpr Rect meterBarRect(const MeterRow& r) {
  return {kMeterFirstX - kMeterPitch / 2, r.cy - kMeterBarHeight / 2, kMeterPitch * kMeterLeds, kMeterBarHeight};
}

struct ClipSpec {
  Steinberg::Vst::ParamID id;
  const char* label;
  float cx;   // LED centre
};
constexpr int kClipCount = 2;
constexpr ClipSpec kClips[kClipCount] = {{kDispClipIn, "IN", 532}, {kDispClipOut, "OUT", 576}};
constexpr float kClipRowY = 210;
// ClipLed draws its LED (radius 3.4) 1 px from the left edge and the label after it.
constexpr Rect clipRect(const ClipSpec& c) { return {c.cx - 4.4f, kClipRowY - 8, 40, 16}; }

// ---- BREATH detector lamp ---------------------------------------------------------------------------------------------
struct Lamp {
  float cx, cy, r;
};
constexpr Lamp kBreathLamp{622, 338, 5};

// ---- Printed on the panel (panel layer, below the silkscreen) -------------------------------------------------------
enum class TextStyle {
  Title,          // 38 px bold italic, white
  Brand,          // 11 px bold, dim white
  Tagline,        // 13 px bold, white
  Section,        // 11 px bold, yellow (IN / OUT / GR / CLIP of the LEVEL box)
  RowTag,         // 11 px bold, white (L / R)
  Scale,          // 9 px bold, dim white (dB scale)
  Label12,        // 12 px bold, white (TONE band names)
  Label11,        // 11 px bold, white (HPF, MUD CUT)
  Label10,        // 10 px bold, white (BREATH lamp)
  Small,          // 9 px bold, dim white (band frequencies, "at 300 Hz")
  LegendVoice,    // 9 px bold, white (scope legend)
  LegendRider,    // 9 px bold, amber
  LegendBreath,   // 9 px bold, red
};
using FairyTapes::Ui::TextAlign;

struct PanelText {
  const char* text;
  float x, y;   // y is the baseline
  TextStyle style;
  TextAlign align;
  Module module;   // dimmed with this module's IN (None: only by the global bypass)
};

// Exact index of the LED under a scale mark, as the preview: round((d + 60) / (66 / 14)), d in dB.
constexpr int levelLed(int db) { return ((db + 60) * 14 * 2 + 66) / (2 * 66); }
constexpr float levelScaleX(int db) { return kMeterFirstX + kMeterPitch * static_cast<float>(levelLed(db)); }
constexpr float grScaleX(int db) { return kMeterFirstX + kMeterPitch * static_cast<float>(db * 2 / 3); }   // round(d / 1.5)
constexpr float kRowTagX = kMeterFirstX - 12, kSectionTagX = kMeterFirstX - 34;

constexpr PanelText kPanelTexts[] = {
    {"DEEP THROAT", 34, 58, TextStyle::Title, TextAlign::Left, Module::None},
    {"FAIRY TAPES", 36, 78, TextStyle::Brand, TextAlign::Left, Module::None},
    {"VOCAL CHANNEL STRIP  \xC2\xB7  RIDER  \xC2\xB7  BREATH  \xC2\xB7  EQ  \xC2\xB7  DE-ESS  \xC2\xB7  COMP  \xC2\xB7  TONE", 966, 46,
     TextStyle::Tagline, TextAlign::Right, Module::None},
    // SCOPE legend
    {"VOICE", 156, 206, TextStyle::LegendVoice, TextAlign::Left, Module::None},
    {"RIDER GAIN", 200, 206, TextStyle::LegendRider, TextAlign::Left, Module::None},
    {"BREATH", 274, 206, TextStyle::LegendBreath, TextAlign::Left, Module::None},
    // LEVEL
    {"IN", kSectionTagX, 120, TextStyle::Section, TextAlign::Centre, Module::None},
    {"OUT", kSectionTagX, 158, TextStyle::Section, TextAlign::Centre, Module::None},
    {"GR", kSectionTagX, 188, TextStyle::Section, TextAlign::Centre, Module::None},
    {"CLIP", kSectionTagX, 214, TextStyle::Section, TextAlign::Centre, Module::None},
    {"L", kRowTagX, 114, TextStyle::RowTag, TextAlign::Right, Module::None},
    {"R", kRowTagX, 126, TextStyle::RowTag, TextAlign::Right, Module::None},
    {"L", kRowTagX, 152, TextStyle::RowTag, TextAlign::Right, Module::None},
    {"R", kRowTagX, 164, TextStyle::RowTag, TextAlign::Right, Module::None},
    {"-60", levelScaleX(-60), 138, TextStyle::Scale, TextAlign::Centre, Module::None},
    {"-48", levelScaleX(-48), 138, TextStyle::Scale, TextAlign::Centre, Module::None},
    {"-36", levelScaleX(-36), 138, TextStyle::Scale, TextAlign::Centre, Module::None},
    {"-24", levelScaleX(-24), 138, TextStyle::Scale, TextAlign::Centre, Module::None},
    {"-18", levelScaleX(-18), 138, TextStyle::Scale, TextAlign::Centre, Module::None},
    {"-12", levelScaleX(-12), 138, TextStyle::Scale, TextAlign::Centre, Module::None},
    {"-6", levelScaleX(-6), 138, TextStyle::Scale, TextAlign::Centre, Module::None},
    {"0", levelScaleX(0), 138, TextStyle::Scale, TextAlign::Centre, Module::None},
    {"0", grScaleX(0), 174, TextStyle::Scale, TextAlign::Centre, Module::None},
    {"-6", grScaleX(6), 174, TextStyle::Scale, TextAlign::Centre, Module::None},
    {"-12", grScaleX(12), 174, TextStyle::Scale, TextAlign::Centre, Module::None},
    {"-18", grScaleX(18), 174, TextStyle::Scale, TextAlign::Centre, Module::None},
    // BREATH
    {"BREATH", 632, 342, TextStyle::Label10, TextAlign::Left, Module::Breath},
    // EQ
    {"HPF", 904, 298, TextStyle::Label11, TextAlign::Centre, Module::Eq},
    {"MUD CUT", 904, 338, TextStyle::Label11, TextAlign::Centre, Module::Eq},
    {"at 300 Hz", 904, 370, TextStyle::Small, TextAlign::Centre, Module::Eq},
    // TONE
    {"MUD", 822, 500, TextStyle::Label12, TextAlign::Left, Module::Tone},
    {"250 Hz", 822, 512, TextStyle::Small, TextAlign::Left, Module::Tone},
    {"PRESENCE", 822, 532, TextStyle::Label12, TextAlign::Left, Module::Tone},
    {"3.5 kHz", 822, 544, TextStyle::Small, TextAlign::Left, Module::Tone},
    {"AIR", 822, 564, TextStyle::Label12, TextAlign::Left, Module::Tone},
    {"12 kHz", 822, 576, TextStyle::Small, TextAlign::Left, Module::Tone},
};

// Read-only values printed on the panel (filled from the mode table and the parameters, Task 5). The Comp cycle values
// are printed by the Cycle items of the silkscreen.
enum class ReadoutId { RiderInfo, EqHpf, EqMud, ToneMud, TonePresence, ToneAir };
struct Readout {
  ReadoutId id;
  float x, y;   // y is the baseline
  float size;
  bool amber;   // amber, else dim white
  TextAlign align;
  Module module;
};
constexpr Readout kReadouts[6] = {
    {ReadoutId::RiderInfo, 266, 390, 10, false, TextAlign::Centre, Module::Rider},
    {ReadoutId::EqHpf, 904, 314, 10, true, TextAlign::Centre, Module::Eq},
    {ReadoutId::EqMud, 904, 354, 10, true, TextAlign::Centre, Module::Eq},
    {ReadoutId::ToneMud, 938, 506, 10, true, TextAlign::Right, Module::Tone},
    {ReadoutId::TonePresence, 938, 538, 10, true, TextAlign::Right, Module::Tone},
    {ReadoutId::ToneAir, 938, 570, 10, true, TextAlign::Right, Module::Tone},
};

// Faint block tags (7 px, 10% opacity) at the top left of a box: the keys of SilkscreenModel::setTag.
enum TagKey { kTagModeName = 1, kTagModeInfo, kTagBreath, kTagEq, kTagDeEss, kTagComp };
struct TagSpec {
  FairyTapes::Ui::BlockTag tag;
  Module module;
};
constexpr TagSpec kTags[6] = {
    {{kTagModeName, 40, 260}, Module::None}, {{kTagModeInfo, 40, 270}, Module::None}, {{kTagBreath, 416, 260}, Module::Breath},
    {{kTagEq, 777, 260}, Module::Eq},        {{kTagDeEss, 40, 446}, Module::DeEss},   {{kTagComp, 271, 446}, Module::Comp},
};

// ---- Silkscreen: the printed parts of every control (fairy_ui/silkscreen.h), ids are ParamIDs ---------------------
inline std::vector<FairyTapes::Ui::PrintedItem> silkscreenItems() {
  using FairyTapes::Ui::PrintedItem;
  using FairyTapes::Ui::PrintKind;
  std::vector<PrintedItem> out;
  for (const ControlSpec& c : kControls) {
    if (c.kind == Kind::Choice) continue;   // the names are on the faces
    PrintedItem it;
    it.id = static_cast<int>(c.id);
    it.cx = c.rect.x + c.rect.w / 2;
    if (c.kind == Kind::Knob) {
      it.kind = PrintKind::Knob;
      it.cy = knobCentreY(c);
      it.capRadius = c.capRadius;
      it.lo = c.lo;
      it.hi = c.hi;
    } else {
      it.kind = c.kind == Kind::Cycle ? PrintKind::Cycle : PrintKind::Button;
      it.cy = c.rect.y + c.rect.h / 2;
    }
    it.name = c.name;
    out.push_back(std::move(it));
  }
  return out;
}

// ---- Decor: base positions of the paper tapes and the corner screws (ui/decor.h adds the seeded variation) ---------
// Base centre and how far the seed may move it (+-jx, +-jy). Chosen so that no tape (up to 96 x 26, turned up to 9 degrees,
// with its torn ends: at most 50 x 22 around its centre) ever covers a control, the scope, the LEVEL rows or the clip LEDs,
// nor another tape: in the header, in the 12 px bands between the rows of boxes (the controls stop 28 px from them) and in
// the empty top of COMP. A tape may lie over printed text and box borders, as real tape would.
struct TapeSlot {
  float cx, cy, jx, jy;
};
constexpr TapeSlot kTapeSlots[6] = {
    {330.f, 48.f, 40.f, 10.f},   // header, between the title and the tagline
    {100.f, 234.f, 25.f, 2.f},   // band between INPUT and MODE
    {300.f, 234.f, 40.f, 2.f},   // band under SCOPE
    {890.f, 234.f, 40.f, 2.f},   // band under OUTPUT, above EQ
    {400.f, 418.f, 60.f, 2.f},   // band between BREATH / RIDER and COMP
    {540.f, 482.f, 30.f, 2.f},   // COMP, above the cycle buttons
};
constexpr int kNumTapeSlots = 6;
constexpr float kScrewPos[4][2] = {{18.f, 18.f}, {982.f, 18.f}, {18.f, 596.f}, {982.f, 596.f}};

} // namespace FairyTapes::DeepThroat::Ui
