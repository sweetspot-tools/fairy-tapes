/**
 * Fairy Tapes · Happy Ending — editor layout table, dimming / locking / visibility rules, printed items.
 * Header-only and free of VSTGUI so it is unit tested (tests/test_ui.cpp). Coordinates are logical px of the
 * 1280 x 890 panel of src/happy-ending/docs/mockups/happy_ending_rack.html, read from the rendered preview (headless
 * browser, the element's literals plus the translations of its groups). The preview moves rows with nested groups:
 * STAGES +202 in y; inside it VG2 -216 (VIBE EQ, GLUE) and SMALL -484 (CLIP, LIMIT, FINAL, METALHEAD). The numbers below
 * are the FINAL ones: service row as written; FINE EQ and the screen = written + 202; VIBE EQ and GLUE = written - 14;
 * the bottom row = written - 282. Parameter scales, steps and defaults come from params.h, never from the preview.
 */

#pragma once

#include "params.h"
#include "ui/editor_size.h"

#include "fairy_ui/rack_geometry.h"
#include "fairy_ui/screws.h"
#include "fairy_ui/silkscreen.h"

#include <vector>

namespace FairyTapes::HappyEnding::Ui {

using VstParamID = Steinberg::Vst::ParamID;   // a stored ParamID, a HiddenID (LISTEN, SET) or a DisplayID

struct Rect {
  float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
};
struct Point {
  float x = 0.f, y = 0.f;
};

// ---- Boxes ----------------------------------------------------------------------------------------------------------
// Four rows: service (INPUT, LEVEL, LOUDNESS, OUTPUT); FINE EQ and the screen; VIBE EQ and GLUE; CLIP, LIMIT, FINAL,
// METALHEAD. The screen is a box without a title.
enum class Box { Input, Level, Loudness, Output, FineEq, Screen, VibeEq, Glue, Clip, Limit, Final, Metal };

struct BoxSpec {
  Box box;
  Rect rect;
  const char* title;   // printed in a gap of the bottom border (nullptr: no title)
};

constexpr BoxSpec kBoxes[12] = {
    {Box::Input, {24, 92, 130, 190}, "INPUT"},
    {Box::Level, {160, 92, 652, 190}, "LEVEL"},
    {Box::Loudness, {818, 92, 212, 190}, "LOUDNESS"},
    {Box::Output, {1036, 92, 220, 190}, "OUTPUT"},
    {Box::FineEq, {24, 302, 752, 264}, "FINE EQ"},
    {Box::Screen, {782, 302, 474, 264}, nullptr},
    {Box::VibeEq, {24, 586, 624, 152}, "VIBE EQ"},
    {Box::Glue, {654, 586, 602, 152}, "GLUE"},
    {Box::Clip, {24, 758, 220, 112}, "CLIP"},
    {Box::Limit, {250, 758, 385, 112}, "LIMIT"},
    {Box::Final, {641, 758, 340, 112}, "FINAL"},
    {Box::Metal, {987, 758, 269, 112}, "METALHEAD"},
};

constexpr const BoxSpec& boxOf(Box b) {
  for (const auto& s : kBoxes)
    if (s.box == b) return s;
  return kBoxes[0];
}

// Box titles: 14 px bold, centred on the bottom border, baseline 5 px below it (the border is broken for the text).
constexpr float kBoxTitleSize = 14, kBoxTitleBaselineBelow = 5;
constexpr Point titleAnchor(const BoxSpec& b) {
  return {b.rect.x + b.rect.w / 2, b.rect.y + b.rect.h + kBoxTitleBaselineBelow};
}

// ---- Stages ---------------------------------------------------------------------------------------------------------
// The seven stages of the chain, in signal order (also the order of UiState::stageOn and of the stage clip LEDs).
enum class Stage { None = -1, Fine, Vibe, Glue, Clip, Limit, Final, Metal };
constexpr int kNumStages = 7;

constexpr Stage stageOf(Box b) {
  switch (b) {
  case Box::FineEq: return Stage::Fine;
  case Box::VibeEq: return Stage::Vibe;
  case Box::Glue: return Stage::Glue;
  case Box::Clip: return Stage::Clip;
  case Box::Limit: return Stage::Limit;
  case Box::Final: return Stage::Final;
  case Box::Metal: return Stage::Metal;
  default: return Stage::None;
  }
}

// The IN of a stage (kNumParams for Stage::None).
constexpr VstParamID stageInParam(Stage s) {
  switch (s) {
  case Stage::Fine: return kFineOn;
  case Stage::Vibe: return kVibeOn;
  case Stage::Glue: return kGlueOn;
  case Stage::Clip: return kClipOn;
  case Stage::Limit: return kLimitOn;
  case Stage::Final: return kFinalOn;
  case Stage::Metal: return kMetalOn;
  default: return kNumParams;
  }
}

// Faint block tags (7 px, 10 % opacity) at the top left of a box, (x + 16, y + 22): the external engine in the box.
// LIMIT and METALHEAD are our own engines and have none; neither have the service boxes and the screen.
constexpr float kTagInsetX = 16, kTagBaselineBelowTop = 22, kTagSize = 7, kTagOpacity = 0.10f;
constexpr Point tagPosition(Box b) {
  const Rect& r = boxOf(b).rect;
  return {r.x + kTagInsetX, r.y + kTagBaselineBelowTop};
}
// finalMode: 0 INFLATE, 1 CLIP (kFinalMode).
constexpr const char* engineTag(Box b, int finalMode) {
  switch (b) {
  case Box::FineEq: return "MULTI-Q";
  case Box::VibeEq: return "TUBE-1A";
  case Box::Glue: return "4K-G";
  case Box::Clip: return "C-SOFTLY";
  case Box::Final: return finalMode == 1 ? "C-ONLY2" : "INFL-Z";
  default: return nullptr;
  }
}

// ---- Controls -------------------------------------------------------------------------------------------------------
// Knob: cap with printed scale. Toggle: on/off button, red LED (IN, BYPASS, LISTEN, SET, WARM). LitChoice: exclusive
// faces with the name on them, the active one lit amber (LIMIT styles, FINAL mode, the screen views). Cycle: one button
// that steps a value, yellow LED, value printed under the LED (M/S, RATIO, SC HPF, TARGET, CEIL). Action: a momentary
// button without a parameter, red LED (MATCH). HeadToggle: the small DYN buttons on the VIBE EQ group headers, name on
// the face, lit amber when on, no LED (docs/DESIGN_SYSTEM.md, three button types and the header exception).
enum class Kind { Knob, Toggle, LitChoice, Cycle, Action, HeadToggle };

struct ControlSpec {
  VstParamID id;      // a stored ParamID, or kListenBand / kSetActive
  int choiceIndex;    // LISTEN: the bell of this button (0 LO MID, 1 MID, 2 HI MID); -1 otherwise
  Kind kind;
  Box box;
  Rect rect;          // Knob: the cell (cap centre kKnobCentreOffsetY below the top). Toggle, Cycle, HeadToggle: the
                      // face. LitChoice: the stack of faces.
  const char* name;   // Knob: printed under the cap. Toggle, Cycle: printed above the face. HeadToggle: on the face.
                      // LitChoice: unused (names in kLitChoices).
  const char* lo;     // Knob: end values of the printed scale
  const char* hi;
  float capRadius;    // Knob
};

// Cell of a knob: AnalogKnob::kPreferredWidth x kPreferredHeight, cap centre kCenterOffsetY below the top.
constexpr float kKnobCellW = 64, kKnobCellH = 88, kKnobCentreOffsetY = 30;
// FINE EQ stacks its knobs on two tiers 81 px apart, columns 34-36 px apart: the upper tier gets a shorter cell so the
// two never overlap (the value under an upper knob is printed by the silkscreen, outside the cell).
constexpr float kUpperTierCellH = 80;
constexpr Rect knob(float cx, float cy) { return {cx - kKnobCellW / 2, cy - kKnobCentreOffsetY, kKnobCellW, kKnobCellH}; }
constexpr Rect knobUpper(float cx, float cy) {
  return {cx - kKnobCellW / 2, cy - kKnobCentreOffsetY, kKnobCellW, kUpperTierCellH};
}
// Button face centred on (cx, cy): Rack::kFaceWidth x kFaceHeight (24 x 22).
constexpr float kFaceW = static_cast<float>(FairyTapes::Ui::Rack::kFaceWidth);
constexpr float kFaceH = static_cast<float>(FairyTapes::Ui::Rack::kFaceHeight);
constexpr Rect face(float cx, float cy) { return {cx - kFaceW / 2, cy - kFaceH / 2, kFaceW, kFaceH}; }
// DYN header buttons: 26 x 18; screen view buttons: 34 x 18 (preview headBtn, viewBtn).
constexpr float kHeadFaceW = 26, kHeadFaceH = 18, kViewFaceW = 34, kViewFaceH = 18;
constexpr Rect headFace(float cx, float cy) { return {cx - kHeadFaceW / 2, cy - kHeadFaceH / 2, kHeadFaceW, kHeadFaceH}; }
constexpr Rect viewFace(float cx, float cy) { return {cx - kViewFaceW / 2, cy - kViewFaceH / 2, kViewFaceW, kViewFaceH}; }
// Room under a face for its LED (centre Rack::kLedOffsetY below the face centre, radius 3.4).
constexpr float kLedZone = 14;
// A Cycle view: the face centred at the top, its LED and the value printed under it (Rack::kCycleValueOffsetY below the
// face centre). 32 px wide: the FINE EQ buttons are 34 px apart.
constexpr float kCycleViewW = 32;
constexpr float kCycleViewH = kFaceH / 2 + static_cast<float>(FairyTapes::Ui::Rack::kCycleValueOffsetY) + 3;

// The value under a knob: its baseline this far under the cap centre plus the radius, as the preview (knob(): R + 36).
// The shared silkscreen prints it at R + 39.5 (fairy_ui/rack_geometry.h valueBaseline), where the values of the lower
// FINE EQ knobs run into the labels of the band buttons; the editor draws the knob values here instead.
constexpr float kKnobValueBaselineBelowCap = 36;
// Cap radii (preview: knob() default 17; the service knobs, CLIP DRIVE and PUSH 22; LOUDNESS, TILT and MIX 24).
constexpr float kKnobRadius = 17, kMidRadius = 22, kLargeRadius = 24;

// ---- Face-lit exclusive choices (one view each: RackLitChoice, one column) -----------------------------------------
constexpr float kLitFaceH = static_cast<float>(FairyTapes::Ui::Rack::kLitFaceHeight);
struct LitChoiceSpec {
  VstParamID id;
  float cx, firstCy;   // centre of the first face
  float faceW, pitch;  // the faces are stacked `pitch` apart
  int count;
  const char* names[3];
};
constexpr LitChoiceSpec kLitChoices[2] = {
    {kLimitStyle, 532, 788, 44, 26, 3, {"MODERN", "PUNCH", "DYN"}},
    {kFinalMode, 878, 801, 44, 26, 2, {"INFLATE", "CLIP", nullptr}},
};
constexpr const LitChoiceSpec* litChoiceOf(VstParamID id) {
  for (const auto& l : kLitChoices)
    if (l.id == id) return &l;
  return nullptr;
}
// The faces of a stack, from the top of the first to the bottom of the last.
constexpr Rect litStack(const LitChoiceSpec& l) {
  return {l.cx - l.faceW / 2, l.firstCy - kLitFaceH / 2, l.faceW, l.pitch * static_cast<float>(l.count - 1) + kLitFaceH};
}

namespace detail {
constexpr ControlSpec knobSpec(VstParamID id, Box b, Rect r, const char* name, const char* lo, const char* hi, float radius) {
  return {id, -1, Kind::Knob, b, r, name, lo, hi, radius};
}
constexpr ControlSpec buttonSpec(VstParamID id, Kind k, Box b, Rect r, const char* name, int choice = -1) {
  return {id, choice, k, b, r, name, nullptr, nullptr, 0};
}
constexpr ControlSpec litSpec(const LitChoiceSpec& l, Box b) {
  return {l.id, -1, Kind::LitChoice, b, litStack(l), nullptr, nullptr, nullptr, 0};
}
// FINE EQ button row: IN, M/S, (LISTEN), 34 px apart; the first at the band centre - 17 (one knob or a shelf) or - 34
// (a bell).
constexpr float kBandButtonY = 517, kBandButtonPitch = 34;
} // namespace detail

// Every stored parameter but kOversampling (on the back, BEHAVIOUR) once, in ParamID order; then LISTEN (three buttons,
// one parameter) and SET. The comments name the box of the entries that follow.
constexpr ControlSpec kControls[] = {
    // ---- service row
    detail::buttonSpec(kBypass, Kind::Toggle, Box::Output, face(1227, 130), "BYPASS"),
    detail::knobSpec(kInputGain, Box::Input, knob(89, 188), "GAIN", "-12", "+12", kMidRadius),
    detail::knobSpec(kOutputGain, Box::Output, knob(1110, 188), "GAIN", "-12", "+12", kMidRadius),
    detail::buttonSpec(kCeiling, Kind::Cycle, Box::Output, face(1227, 230), "CEIL"),
    detail::knobSpec(kLoudness, Box::Loudness, knob(890, 188), "LOUDNESS", "0", "10", kLargeRadius),
    detail::buttonSpec(kTarget, Kind::Cycle, Box::Loudness, face(1001, 230), "TARGET"),
    // ---- FINE EQ: stage IN at the top right
    detail::buttonSpec(kFineOn, Kind::Toggle, Box::FineEq, face(747, 340), "IN"),
    // HI PASS (band centre 78): one knob on the upper tier. End labels of the normal scale (SIDE: kHpSideScaleHi).
    detail::buttonSpec(kHpOn, Kind::Toggle, Box::FineEq, face(61, detail::kBandButtonY), "IN"),
    detail::buttonSpec(kHpMs, Kind::Cycle, Box::FineEq, face(95, detail::kBandButtonY), "M/S"),
    detail::knobSpec(kHpFreq, Box::FineEq, knobUpper(78, 358), "FREQ", "Off", "40", kKnobRadius),
    // LOW SHELF (174): GAIN high, FREQ low
    detail::buttonSpec(kLsOn, Kind::Toggle, Box::FineEq, face(157, detail::kBandButtonY), "IN"),
    detail::buttonSpec(kLsMs, Kind::Cycle, Box::FineEq, face(191, detail::kBandButtonY), "M/S"),
    detail::knobSpec(kLsFreq, Box::FineEq, knob(152, 439), "FREQ", "30", "300", kKnobRadius),
    detail::knobSpec(kLsGain, Box::FineEq, knobUpper(188, 358), "GAIN", "-4", "+4", kKnobRadius),
    // LO MID (292): Q high, FREQ and GAIN low
    detail::buttonSpec(kLmOn, Kind::Toggle, Box::FineEq, face(258, detail::kBandButtonY), "IN"),
    detail::buttonSpec(kLmMs, Kind::Cycle, Box::FineEq, face(292, detail::kBandButtonY), "M/S"),
    detail::knobSpec(kLmFreq, Box::FineEq, knob(258, 439), "FREQ", "100", "800", kKnobRadius),
    detail::knobSpec(kLmGain, Box::FineEq, knob(326, 439), "GAIN", "-4", "+4", kKnobRadius),
    detail::knobSpec(kLmQ, Box::FineEq, knobUpper(292, 358), "Q", "W", "S", kKnobRadius),
    // MID (420): FREQ and GAIN high, Q low
    detail::buttonSpec(kMidOn, Kind::Toggle, Box::FineEq, face(386, detail::kBandButtonY), "IN"),
    detail::buttonSpec(kMidMs, Kind::Cycle, Box::FineEq, face(420, detail::kBandButtonY), "M/S"),
    detail::knobSpec(kMidFreq, Box::FineEq, knobUpper(386, 358), "FREQ", "400", "3.15k", kKnobRadius),
    detail::knobSpec(kMidGain, Box::FineEq, knobUpper(454, 358), "GAIN", "-4", "+4", kKnobRadius),
    detail::knobSpec(kMidQ, Box::FineEq, knob(420, 439), "Q", "W", "S", kKnobRadius),
    // HI MID (548): Q high, FREQ and GAIN low
    detail::buttonSpec(kHmOn, Kind::Toggle, Box::FineEq, face(514, detail::kBandButtonY), "IN"),
    detail::buttonSpec(kHmMs, Kind::Cycle, Box::FineEq, face(548, detail::kBandButtonY), "M/S"),
    detail::knobSpec(kHmFreq, Box::FineEq, knob(514, 439), "FREQ", "1.6k", "12.5k", kKnobRadius),
    detail::knobSpec(kHmGain, Box::FineEq, knob(582, 439), "GAIN", "-4", "+4", kKnobRadius),
    detail::knobSpec(kHmQ, Box::FineEq, knobUpper(548, 358), "Q", "W", "S", kKnobRadius),
    // HI SHELF (666): FREQ high, GAIN low
    detail::buttonSpec(kHsOn, Kind::Toggle, Box::FineEq, face(649, detail::kBandButtonY), "IN"),
    detail::buttonSpec(kHsMs, Kind::Cycle, Box::FineEq, face(683, detail::kBandButtonY), "M/S"),
    detail::knobSpec(kHsFreq, Box::FineEq, knobUpper(644, 358), "FREQ", "3k", "20k", kKnobRadius),
    detail::knobSpec(kHsGain, Box::FineEq, knob(680, 439), "GAIN", "-4", "+4", kKnobRadius),
    // ---- VIBE EQ: groups LOW and AIR (FREQ high, amount low), TILT, TUBE; DYN on the group headers
    detail::buttonSpec(kVibeOn, Kind::Toggle, Box::VibeEq, face(619, 624), "IN"),
    detail::knobSpec(kLowFreq, Box::VibeEq, knob(80, 656), "FREQ", "20", "100", kKnobRadius),
    detail::knobSpec(kLow, Box::VibeEq, knob(154, 670), "LOW", "0", "10", kKnobRadius),
    detail::knobSpec(kAirFreq, Box::VibeEq, knob(262, 656), "FREQ", "5k", "16k", kKnobRadius),
    detail::knobSpec(kAir, Box::VibeEq, knob(336, 670), "AIR", "0", "10", kKnobRadius),
    detail::knobSpec(kTilt, Box::VibeEq, knob(470, 640), "TILT", "DARK", "BRIGHT", kLargeRadius),
    detail::knobSpec(kTube, Box::VibeEq, knob(574, 670), "TUBE", "0", "10", kKnobRadius),
    detail::buttonSpec(kDynLow, Kind::HeadToggle, Box::VibeEq, headFace(175, 630), "DYN"),
    detail::buttonSpec(kDynAir, Kind::HeadToggle, Box::VibeEq, headFace(357, 630), "DYN"),
    // ---- GLUE: knobs on the left, MIX big; IN alone at the top right; WARM, RATIO, SC HPF in a row at the bottom right
    detail::buttonSpec(kGlueOn, Kind::Toggle, Box::Glue, face(1227, 624), "IN"),
    detail::knobSpec(kGlueThresh, Box::Glue, knob(714, 642), "THRESH", "-20", "0", kKnobRadius),
    detail::knobSpec(kGlueAttack, Box::Glue, knob(784, 666), "ATTACK", ".1", "30", kKnobRadius),
    detail::knobSpec(kGlueRelease, Box::Glue, knob(854, 642), "RELEASE", ".1", "A", kKnobRadius),
    detail::buttonSpec(kGlueRatio, Kind::Cycle, Box::Glue, face(1179, 686), "RATIO"),
    detail::knobSpec(kGlueMakeup, Box::Glue, knob(924, 666), "MAKEUP", "0", "+6", kKnobRadius),
    detail::knobSpec(kGlueMix, Box::Glue, knob(1004, 636), "MIX", "DRY", "WET", kLargeRadius),
    detail::buttonSpec(kGlueScHpf, Kind::Cycle, Box::Glue, face(1227, 686), "SC HPF"),
    detail::buttonSpec(kGlueWarm, Kind::Toggle, Box::Glue, face(1131, 686), "WARM"),
    // ---- CLIP ("level matched" under the IN: kPanelTexts)
    detail::buttonSpec(kClipOn, Kind::Toggle, Box::Clip, face(215, 796), "IN"),
    detail::knobSpec(kClipDrive, Box::Clip, knob(104, 798), "DRIVE", "0", "+6", kMidRadius),
    // ---- LIMIT: THRESH and RELEASE on the left, SET up right of RELEASE, the styles between SET and IN
    detail::buttonSpec(kLimitOn, Kind::Toggle, Box::Limit, face(606, 796), "IN"),
    detail::knobSpec(kLimitThresh, Box::Limit, knob(304, 804), "THRESH", "0", "-4", kKnobRadius),
    detail::litSpec(kLitChoices[0], Box::Limit),
    detail::knobSpec(kLimitRelease, Box::Limit, knob(390, 804), "RELEASE", "FAST", "SLOW", kKnobRadius),
    // ---- FINAL: EFFECT and CURVE (INFLATE) or DRIVE (CLIP) in the same place; INFLATE / CLIP between them and IN
    detail::buttonSpec(kFinalOn, Kind::Toggle, Box::Final, face(952, 796), "IN"),
    detail::litSpec(kLitChoices[1], Box::Final),
    detail::knobSpec(kFinalEffect, Box::Final, knob(707, 804), "EFFECT", "0", "100", kKnobRadius),
    detail::knobSpec(kFinalCurve, Box::Final, knob(793, 804), "CURVE", "0", "100", kKnobRadius),
    detail::knobSpec(kFinalDrive, Box::Final, knob(750, 804), "DRIVE", "0", "+2", kKnobRadius),
    // ---- METALHEAD ("SHAME ON YOU" at the tag place: kPanelTexts)
    detail::buttonSpec(kMetalOn, Kind::Toggle, Box::Metal, face(1227, 796), "IN"),
    detail::knobSpec(kPush, Box::Metal, knob(1101, 798), "PUSH", "0", "+6", kMidRadius),
    // ---- session parameters (hidden, never stored): LISTEN on the three bells, SET in LIMIT
    detail::buttonSpec(kListenBand, Kind::Toggle, Box::FineEq, face(326, detail::kBandButtonY), "LISTEN", 0),
    detail::buttonSpec(kListenBand, Kind::Toggle, Box::FineEq, face(454, detail::kBandButtonY), "LISTEN", 1),
    detail::buttonSpec(kListenBand, Kind::Toggle, Box::FineEq, face(582, detail::kBandButtonY), "LISTEN", 2),
    detail::buttonSpec(kSetActive, Kind::Toggle, Box::Limit, face(458, 796), "SET"),
};
constexpr int kNumControls = static_cast<int>(sizeof(kControls) / sizeof(kControls[0]));

// The control of a parameter; for LISTEN pass the bell (0..2). nullptr when the parameter has no front control.
constexpr const ControlSpec* controlOf(VstParamID id, int choiceIndex = -1) {
  for (const auto& c : kControls)
    if (c.id == id && c.choiceIndex == choiceIndex) return &c;
  return nullptr;
}

constexpr Point knobCentre(const ControlSpec& c) { return {c.rect.x + c.rect.w / 2, c.rect.y + kKnobCentreOffsetY}; }
constexpr Point faceCentre(const ControlSpec& c) { return {c.rect.x + c.rect.w / 2, c.rect.y + c.rect.h / 2}; }

// The rect of the control's view: the knob cell; the face plus the LED zone under it (a Cycle also the value text); for
// a LitChoice one equal cell of `pitch` per face (RackLitChoice centres each face in its cell); a HeadToggle is its face.
constexpr Rect viewRect(const ControlSpec& c) {
  switch (c.kind) {
  case Kind::Toggle:
  case Kind::Action: return {c.rect.x, c.rect.y, c.rect.w, c.rect.h + kLedZone};
  case Kind::Cycle: return {c.rect.x + c.rect.w / 2 - kCycleViewW / 2, c.rect.y, kCycleViewW, kCycleViewH};
  case Kind::LitChoice: {
    const LitChoiceSpec* l = litChoiceOf(c.id);
    if (!l) return c.rect;
    return {c.rect.x, c.rect.y + kLitFaceH / 2 - l->pitch / 2, c.rect.w, l->pitch * static_cast<float>(l->count)};
  }
  default: return c.rect;
  }
}

// ---- Actions without a parameter ------------------------------------------------------------------------------------
// MATCH (LOUDNESS, top right: Controller::doMatch) and the five screen views (a column right of the screen, face-lit,
// exclusive; the view is editor state, not a parameter).
enum class Action { Match, ViewEq, ViewGlue, ViewClip, ViewLimit, ViewFinal };

struct ActionSpec {
  Action action;
  Kind kind;          // Action (MATCH: face and red LED, name above) or LitChoice (a view: name on the face)
  Box box;
  Rect rect;          // the face
  const char* name;
};
constexpr float kViewFirstCy = 342, kViewPitch = 44, kViewCx = 1221;
constexpr ActionSpec kActions[6] = {
    {Action::Match, Kind::Action, Box::Loudness, face(1001, 130), "MATCH"},
    {Action::ViewEq, Kind::LitChoice, Box::Screen, viewFace(kViewCx, kViewFirstCy), "EQ"},
    {Action::ViewGlue, Kind::LitChoice, Box::Screen, viewFace(kViewCx, kViewFirstCy + kViewPitch), "GLUE"},
    {Action::ViewClip, Kind::LitChoice, Box::Screen, viewFace(kViewCx, kViewFirstCy + 2 * kViewPitch), "CLIP"},
    {Action::ViewLimit, Kind::LitChoice, Box::Screen, viewFace(kViewCx, kViewFirstCy + 3 * kViewPitch), "LIMIT"},
    {Action::ViewFinal, Kind::LitChoice, Box::Screen, viewFace(kViewCx, kViewFirstCy + 4 * kViewPitch), "FINAL"},
};
constexpr const ActionSpec& actionOf(Action a) {
  for (const auto& s : kActions)
    if (s.action == a) return s;
  return kActions[0];
}
constexpr Rect viewRect(const ActionSpec& a) {
  return a.kind == Kind::Action ? Rect{a.rect.x, a.rect.y, a.rect.w, a.rect.h + kLedZone} : a.rect;
}
// Key of an action's printed label in the silkscreen (outside every parameter and display ID).
constexpr int kActionPrintIdBase = 2000;
constexpr int actionPrintId(Action a) { return kActionPrintIdBase + static_cast<int>(a); }
// Keys of printed items that are not a parameter's own: each LISTEN button has its own (the three share one parameter,
// but each bell dims on its own), and HI PASS FREQ has a second print with the SIDE scale (mono bass, up to 200 Hz): the
// editor shows the one of the current HI PASS M/S.
constexpr int kListenPrintIdBase = 2100;
constexpr int kHpSidePrintId = 2200;
constexpr const char* kHpSideScaleHi = "200";
constexpr int printId(const ControlSpec& c) {
  return c.id == kListenBand ? kListenPrintIdBase + c.choiceIndex : static_cast<int>(c.id);
}

// ---- FINE EQ bands --------------------------------------------------------------------------------------------------
// 0 HI PASS, 1 LOW SHELF, 2 LO MID, 3 MID, 4 HI MID, 5 HI SHELF (UiState::bandOn order). LISTEN bell k is band k + 2.
constexpr int kNumBands = 6;
constexpr VstParamID bandInParam(int band) {
  constexpr VstParamID ins[kNumBands] = {kHpOn, kLsOn, kLmOn, kMidOn, kHmOn, kHsOn};
  return band >= 0 && band < kNumBands ? ins[band] : kNumParams;
}
constexpr int bandOf(VstParamID id, int choiceIndex) {
  if (id == kListenBand) return choiceIndex >= 0 && choiceIndex <= 2 ? choiceIndex + 2 : -1;
  if (id >= kHpOn && id <= kHpFreq) return 0;
  if (id >= kLsOn && id <= kLsGain) return 1;
  if (id >= kLmOn && id <= kLmQ) return 2;
  if (id >= kMidOn && id <= kMidQ) return 3;
  if (id >= kHmOn && id <= kHmQ) return 4;
  if (id >= kHsOn && id <= kHsGain) return 5;
  return -1;
}
constexpr int bandOf(const ControlSpec& c) { return bandOf(c.id, c.choiceIndex); }

// ---- Dimming, locking, visibility (spec §3, §5) ---------------------------------------------------------------------
// Dimmed = drawn at 35 %; locked = takes no clicks. The defaults are "everything live" (a neutral start for tests);
// uiStateFrom gives the state of a set of parameters.
struct UiState {
  bool bypass = false;
  bool stageOn[kNumStages] = {true, true, true, true, true, true, true};   // FINE VIBE GLUE CLIP LIMIT FINAL METAL
  bool bandOn[kNumBands] = {true, true, true, true, true, true};           // HI PASS ... HI SHELF
  int listen = -1;     // LISTEN: -1 off, else the bell 0..2 (LO MID, MID, HI MID)
  bool set = false;    // SET of LIMIT
  int finalMode = 0;   // 0 INFLATE, 1 CLIP
};

constexpr bool stageOn(const UiState& s, Stage st) { return st == Stage::None || s.stageOn[static_cast<int>(st)]; }

// The FINE EQ band being listened to (2..4), or -1. As in the engine (mapParams), LISTEN counts as off while its bell
// or the stage is off.
constexpr int listenedBand(const UiState& s) {
  if (s.listen < 0 || s.listen > 2 || !stageOn(s, Stage::Fine)) return -1;
  const int b = s.listen + 2;
  return s.bandOn[b] ? b : -1;
}
// SET acts on the panel only while LIMIT is in: with LIMIT out its IN stays free (no dead end) and SET dims with LIMIT.
constexpr bool setEngaged(const UiState& s) { return s.set && stageOn(s, Stage::Limit); }

// A FINE EQ band's part of the rules: while another band is listened to the whole band dims, its IN too; a band that
// is off dims all but its IN.
constexpr bool bandDimmed(int band, bool isBandIn, const UiState& s) {
  if (band < 0 || band >= kNumBands) return false;
  const int heard = listenedBand(s);
  if (heard >= 0 && heard != band) return true;
  return !isBandIn && !s.bandOn[band];
}

// BYPASS dims everything but itself; a stage whose IN is off dims all of its controls but that IN (METALHEAD, off by
// default, included); FINE EQ: bands as above; SET dims LIMIT but RELEASE and SET.
constexpr bool controlDimmed(const ControlSpec& c, const UiState& s) {
  if (c.id == kBypass) return false;
  if (s.bypass) return true;
  const Stage st = stageOf(c.box);
  if (st == Stage::None) return false;
  if (c.id != stageInParam(st) && !stageOn(s, st)) return true;
  if (st == Stage::Fine) {
    const int b = bandOf(c);
    if (bandDimmed(b, c.id == bandInParam(b), s)) return true;
  }
  if (st == Stage::Limit && setEngaged(s) && c.id != kLimitRelease && c.id != kSetActive) return true;
  return false;
}

// FINAL shows only the controls of the selected mode: EFFECT and CURVE for INFLATE, DRIVE for CLIP.
constexpr bool controlVisible(const ControlSpec& c, const UiState& s) {
  if (c.id == kFinalEffect || c.id == kFinalCurve) return s.finalMode != 1;
  if (c.id == kFinalDrive) return s.finalMode == 1;
  return true;
}

// A dimmed control is locked; so is a hidden one.
constexpr bool controlLocked(const ControlSpec& c, const UiState& s) { return controlDimmed(c, s) || !controlVisible(c, s); }

// MATCH and the screen views dim and lock with BYPASS only.
constexpr bool actionDimmed(const ActionSpec&, const UiState& s) { return s.bypass; }

// The UI state of a set of normalized parameters (kNumParams values in ParamID order), read with the plug-in's index
// conversion; listen (-1..2) and set are the session parameters. A LISTEN out of range is off.
inline UiState uiStateFrom(const double* norm, int listen, bool set) {
  UiState s;
  auto on = [norm](ParamID id) { return indexFromNorm(id, norm[id]) != 0; };
  s.bypass = on(kBypass);
  for (int i = 0; i < kNumStages; ++i) s.stageOn[i] = on(static_cast<ParamID>(stageInParam(static_cast<Stage>(i))));
  for (int b = 0; b < kNumBands; ++b) s.bandOn[b] = on(static_cast<ParamID>(bandInParam(b)));
  s.listen = listen >= -1 && listen <= 2 ? listen : -1;
  s.set = set;
  s.finalMode = indexFromNorm(kFinalMode, norm[kFinalMode]);
  return s;
}

// ---- Screen ---------------------------------------------------------------------------------------------------------
constexpr Rect kScreenRect{798, 318, 388, 232};   // the drawing area inside the screen box

// ---- LEVEL: peak meters, GR rows, clip LEDs -------------------------------------------------------------------------
// Peak rows IN L/R and OUT L/R and the GR block (G C L F M): 15 LEDs each, 24 px apart from x 242. Clip LEDs in signal
// order under them. The peak rows, the IN / OUT clip LEDs and the LUFS screen never dim; the GR rows and the stage clip
// LEDs dim with BYPASS and with their stage's IN.
constexpr int kMeterLeds = 15;
constexpr float kMeterFirstX = 242, kMeterPitch = 24, kMeterLedRadius = 3.4f, kGrLedRadius = 3.2f, kMeterBarHeight = 8;

struct MeterRow {
  VstParamID id;   // display parameter (peak over -60..0 dB)
  float cy;
};
constexpr MeterRow kMeterRows[4] = {{kDispInL, 114}, {kDispInR, 128}, {kDispOutL, 156}, {kDispOutR, 170}};

struct GrRow {
  VstParamID id;   // display parameter (gain reduction over 0..12 dB)
  const char* label;
  float cy;
  Stage stage;
};
constexpr GrRow kGrRows[5] = {
    {kDispGr0, "G", 194, Stage::Glue},  {kDispGr1, "C", 207, Stage::Clip},  {kDispGr2, "L", 220, Stage::Limit},
    {kDispGr3, "F", 233, Stage::Final}, {kDispGr4, "M", 246, Stage::Metal},
};

constexpr Rect ledBarRect(float cy) {
  return {kMeterFirstX - kMeterPitch / 2, cy - kMeterBarHeight / 2, kMeterPitch * kMeterLeds, kMeterBarHeight};
}
constexpr Rect meterBarRect(const MeterRow& r) { return ledBarRect(r.cy); }
constexpr Rect grBarRect(const GrRow& r) { return ledBarRect(r.cy); }
// The row tag (L / R, G C L F M) is right-aligned 12 px left of the first LED, baseline 4 px under the row centre.
constexpr float kRowTagX = kMeterFirstX - 12, kRowTagBaselineBelow = 4;

constexpr bool grRowDimmed(int row, const UiState& s) {
  return row >= 0 && row < 5 && (s.bypass || !stageOn(s, kGrRows[row].stage));
}

struct ClipLed {
  VstParamID id;
  const char* label;   // printed right of the LED (x + 7, 9 px bold)
  float cx;
  Stage stage;         // None: IN and OUT, never dimmed
};
constexpr float kClipRowY = 264, kClipLedRadius = 3.4f;
constexpr ClipLed kClipLeds[9] = {
    {kDispClip0, "IN", 246, Stage::None},    {kDispClip1, "FEQ", 290, Stage::Fine},  {kDispClip2, "VEQ", 334, Stage::Vibe},
    {kDispClip3, "GLU", 378, Stage::Glue},   {kDispClip4, "CLP", 422, Stage::Clip},  {kDispClip5, "LIM", 466, Stage::Limit},
    {kDispClip6, "FIN", 510, Stage::Final},  {kDispClip7, "MTL", 554, Stage::Metal}, {kDispClip8, "OUT", 598, Stage::None},
};
// LED and label; a click on any of them clears all (kClearClips).
constexpr Rect clipLedRect(const ClipLed& c) { return {c.cx - 4.4f, kClipRowY - 8, 40, 16}; }
constexpr bool clipLedDimmed(int index, const UiState& s) {
  if (index < 0 || index >= 9 || kClipLeds[index].stage == Stage::None) return false;
  return s.bypass || !stageOn(s, kClipLeds[index].stage);
}

// ---- LUFS screen (in LEVEL, right of the LEDs) ----------------------------------------------------------------------
constexpr Rect kLufsScreenRect{640, 108, 156, 128};
constexpr Rect kLufsResetRect{642, 160, 152, 32};   // a click here resets the integrated loudness (kResetIntegrated)
struct Led {
  float cx, cy, r;
};
constexpr Led kTargetLed{658, 222, 3.4f};   // green within 0.5 LU of the target
// The three values, right-aligned at x 786 (baseline, size).
struct LufsValue {
  float x, y, size;
};
constexpr LufsValue kLufsShortValue{786, 150, 22};      // amber
constexpr LufsValue kLufsIntegratedValue{786, 186, 15}; // amber
constexpr LufsValue kLufsToTargetValue{786, 226, 13};   // white

// ---- VIBE EQ: the ACT LEDs left of the DYN buttons, lit as much as the boost is pulled back ------------------------
constexpr Led kActLeds[2] = {{150, 630, 4}, {332, 630, 4}};   // LOW, AIR
constexpr VstParamID kActLedIds[2] = {kDispDynLow, kDispDynAir};
constexpr bool actLedDimmed(const UiState& s) { return s.bypass || !stageOn(s, Stage::Vibe); }

// ---- Printed on the panel (panel layer, below the silkscreen) -------------------------------------------------------
enum class TextStyle {
  Title,       // 38 px bold italic, white
  Brand,       // 11 px bold, dim white
  Tagline,     // 13 px bold, white
  Section,     // 11 px bold, yellow (IN / OUT / GR / CLIP of LEVEL)
  RowTag,      // 11 px bold, white (L / R)
  Scale,       // 9 px bold, dim white (dB scale of the peak rows)
  LufsTitle,   // 10 px bold, yellow
  LufsLabel,   // 8 px bold, dim white
  LufsHint,    // 7 px bold, dim white
  Header,      // 10 px bold, dim white (FINE EQ band names, VIBE EQ group names)
  Small,       // 9 px bold, dim white ("level matched")
  Tag,         // 7 px bold, dim white at 10 % (as the block tags: "SHAME ON YOU")
};
using FairyTapes::Ui::TextAlign;

struct PanelText {
  const char* text;
  float x, y;   // y is the baseline
  TextStyle style;
  TextAlign align;
  Stage stage;           // dimmed with this stage's IN (None: no stage)
  int band;              // FINE EQ band it belongs to (-1: none)
  bool dimsWithBypass;   // the stage texts; the header, LEVEL and LUFS texts never dim
};

// Index of the LED under a scale mark, as the preview: round((d + 60) / (66 / 14)), d in dB.
constexpr int levelLed(int db) { return ((db + 60) * 14 * 2 + 66) / (2 * 66); }
constexpr float levelScaleX(int db) { return kMeterFirstX + kMeterPitch * static_cast<float>(levelLed(db)); }
constexpr float kSectionTagX = kMeterFirstX - 34;

constexpr PanelText kPanelTexts[] = {
    // header
    {"HAPPY ENDING", 34, 58, TextStyle::Title, TextAlign::Left, Stage::None, -1, false},
    {"FAIRY TAPES", 36, 78, TextStyle::Brand, TextAlign::Left, Stage::None, -1, false},
    {"MASTER BUS  \xC2\xB7  FINE EQ  \xC2\xB7  VIBE EQ  \xC2\xB7  GLUE  \xC2\xB7  CLIP  \xC2\xB7  LIMIT  \xC2\xB7  FINAL  "
     "\xC2\xB7  METALHEAD",
     1246, 46, TextStyle::Tagline, TextAlign::Right, Stage::None, -1, false},
    // LEVEL (the GR row tags and the clip labels are drawn with their rows: kGrRows, kClipLeds)
    {"IN", kSectionTagX, 124, TextStyle::Section, TextAlign::Centre, Stage::None, -1, false},
    {"OUT", kSectionTagX, 166, TextStyle::Section, TextAlign::Centre, Stage::None, -1, false},
    {"GR", kSectionTagX, 224, TextStyle::Section, TextAlign::Centre, Stage::None, -1, false},
    {"CLIP", kSectionTagX, 268, TextStyle::Section, TextAlign::Centre, Stage::None, -1, false},
    {"L", kRowTagX, 118, TextStyle::RowTag, TextAlign::Right, Stage::None, -1, false},
    {"R", kRowTagX, 132, TextStyle::RowTag, TextAlign::Right, Stage::None, -1, false},
    {"L", kRowTagX, 160, TextStyle::RowTag, TextAlign::Right, Stage::None, -1, false},
    {"R", kRowTagX, 174, TextStyle::RowTag, TextAlign::Right, Stage::None, -1, false},
    {"-60", levelScaleX(-60), 146, TextStyle::Scale, TextAlign::Centre, Stage::None, -1, false},
    {"-48", levelScaleX(-48), 146, TextStyle::Scale, TextAlign::Centre, Stage::None, -1, false},
    {"-36", levelScaleX(-36), 146, TextStyle::Scale, TextAlign::Centre, Stage::None, -1, false},
    {"-24", levelScaleX(-24), 146, TextStyle::Scale, TextAlign::Centre, Stage::None, -1, false},
    {"-18", levelScaleX(-18), 146, TextStyle::Scale, TextAlign::Centre, Stage::None, -1, false},
    {"-12", levelScaleX(-12), 146, TextStyle::Scale, TextAlign::Centre, Stage::None, -1, false},
    {"-6", levelScaleX(-6), 146, TextStyle::Scale, TextAlign::Centre, Stage::None, -1, false},
    {"0", levelScaleX(0), 146, TextStyle::Scale, TextAlign::Centre, Stage::None, -1, false},
    // LUFS screen
    {"LUFS", 652, 124, TextStyle::LufsTitle, TextAlign::Left, Stage::None, -1, false},
    {"SHORT", 652, 146, TextStyle::LufsLabel, TextAlign::Left, Stage::None, -1, false},
    {"INTEGRATED", 652, 172, TextStyle::LufsLabel, TextAlign::Left, Stage::None, -1, false},
    {"click to reset", 652, 184, TextStyle::LufsHint, TextAlign::Left, Stage::None, -1, false},
    {"TO TARGET", 652, 210, TextStyle::LufsLabel, TextAlign::Left, Stage::None, -1, false},
    // FINE EQ band names over their header lines (kHeaderLines)
    {"HI PASS", 78, 318, TextStyle::Header, TextAlign::Centre, Stage::Fine, 0, true},
    {"LOW SHELF", 174, 318, TextStyle::Header, TextAlign::Centre, Stage::Fine, 1, true},
    {"LO MID", 292, 318, TextStyle::Header, TextAlign::Centre, Stage::Fine, 2, true},
    {"MID", 420, 318, TextStyle::Header, TextAlign::Centre, Stage::Fine, 3, true},
    {"HI MID", 548, 318, TextStyle::Header, TextAlign::Centre, Stage::Fine, 4, true},
    {"HI SHELF", 666, 318, TextStyle::Header, TextAlign::Centre, Stage::Fine, 5, true},
    // VIBE EQ group names
    {"LOW", 93, 624, TextStyle::Header, TextAlign::Centre, Stage::Vibe, -1, true},
    {"AIR", 275, 624, TextStyle::Header, TextAlign::Centre, Stage::Vibe, -1, true},
    // CLIP: under the IN, on two lines
    {"level", 215, 842, TextStyle::Small, TextAlign::Centre, Stage::Clip, -1, true},
    {"matched", 215, 854, TextStyle::Small, TextAlign::Centre, Stage::Clip, -1, true},
    // METALHEAD: in the corner, with the lettering of the block tags
    {"SHAME ON YOU", 1003, 780, TextStyle::Tag, TextAlign::Left, Stage::Metal, -1, true},
};

// Thin header lines (1 px, dim white at 50 %) under the FINE EQ band names and the VIBE EQ group names.
struct HeaderLine {
  float x0, x1, y;
  Stage stage;
  int band;
};
constexpr HeaderLine kHeaderLines[8] = {
    {44, 112, 324, Stage::Fine, 0},  {128, 220, 324, Stage::Fine, 1}, {236, 348, 324, Stage::Fine, 2},
    {364, 476, 324, Stage::Fine, 3}, {492, 604, 324, Stage::Fine, 4}, {620, 712, 324, Stage::Fine, 5},
    {44, 142, 630, Stage::Vibe, -1}, {226, 324, 630, Stage::Vibe, -1},
};

// A printed part of a stage dims with BYPASS (if it does), with its stage's IN and, in FINE EQ, with its band.
constexpr bool printedDimmed(Stage st, int band, bool dimsWithBypass, const UiState& s) {
  return (dimsWithBypass && s.bypass) || !stageOn(s, st) || bandDimmed(band, false, s);
}
constexpr bool panelTextDimmed(const PanelText& t, const UiState& s) { return printedDimmed(t.stage, t.band, t.dimsWithBypass, s); }
constexpr bool headerLineDimmed(const HeaderLine& l, const UiState& s) { return printedDimmed(l.stage, l.band, true, s); }
constexpr bool tagDimmed(Box b, const UiState& s) { return printedDimmed(stageOf(b), -1, true, s); }

// ---- Silkscreen: the printed parts of every control (fairy_ui/silkscreen.h); ids are ParamIDs ----------------------
// Knobs, on/off buttons, cycling selectors and MATCH, keyed by printId (each LISTEN button its own key); HI PASS FREQ once
// more with the SIDE scale (kHpSidePrintId). The face-lit choices and the DYN buttons carry their names on the faces.
inline std::vector<FairyTapes::Ui::PrintedItem> silkscreenItems() {
  using FairyTapes::Ui::PrintedItem;
  using FairyTapes::Ui::PrintKind;
  std::vector<PrintedItem> out;
  for (const ControlSpec& c : kControls) {
    if (c.kind == Kind::LitChoice || c.kind == Kind::HeadToggle) continue;
    PrintedItem it;
    it.id = printId(c);
    if (c.kind == Kind::Knob) {
      const Point p = knobCentre(c);
      it.kind = PrintKind::Knob;
      it.cx = p.x;
      it.cy = p.y;
      it.capRadius = c.capRadius;
      it.lo = c.lo;
      it.hi = c.hi;
    } else {
      const Point p = faceCentre(c);
      it.kind = c.kind == Kind::Cycle ? PrintKind::Cycle : PrintKind::Button;
      it.cx = p.x;
      it.cy = p.y;
    }
    it.name = c.name;
    if (c.id == kHpFreq) {
      PrintedItem side = it;
      side.id = kHpSidePrintId;
      side.hi = kHpSideScaleHi;
      out.push_back(std::move(it));
      out.push_back(std::move(side));
      continue;
    }
    out.push_back(std::move(it));
  }
  const ActionSpec& m = actionOf(Action::Match);
  PrintedItem it;
  it.id = actionPrintId(Action::Match);
  it.kind = PrintKind::Button;
  it.cx = m.rect.x + m.rect.w / 2;
  it.cy = m.rect.y + m.rect.h / 2;
  it.name = m.name;
  out.push_back(std::move(it));
  return out;
}

// ---- Decor: base positions of the paper tapes and the corner screws (the seeded variation comes later) -------------
// Base centre and how far the seed may move it (+-jx, +-jy). A tape is up to 96 x 26 and turned up to 9 degrees; with its
// torn ends and shadow it reaches 54 px either side of its centre in x and 25 px in y. The slots sit where that fits
// clear of every control view, the screens, the meters and LEDs and the screws (tests/test_ui.cpp checks it), and of
// the printed names and values as far as the panel allows; a tape may lie over box borders, as real tape would:
//   0, 1  the header band under the tagline, right of the title, above the service row
//   2     the empty top of INPUT, above its knob
//   3     the empty top left of LOUDNESS, above its knob and left of MATCH
//   4     the empty top left of OUTPUT, above its knob and left of BYPASS
//   5     the empty top of GLUE between MIX and its IN
struct TapeSlot {
  float cx, cy, jx, jy;
};
constexpr TapeSlot kTapeSlots[6] = {
    {480.f, 77.f, 60.f, 2.f},    // header, under the tagline
    {860.f, 77.f, 90.f, 2.f},    // header, under the tagline, right
    {89.f, 124.f, 4.f, 2.f},     // top of INPUT
    {878.f, 124.f, 8.f, 2.f},    // top left of LOUDNESS
    {1110.f, 124.f, 8.f, 2.f},   // top left of OUTPUT
    {1120.f, 620.f, 10.f, 3.f},  // top of GLUE, between MIX and IN
};
constexpr int kNumTapeSlots = 6;

inline constexpr FairyTapes::Ui::ScrewPositions kScrewPositions = FairyTapes::Ui::screwPositions(kEditorWidth, kEditorHeight);
constexpr float kScrewPos[4][2] = {
    {static_cast<float>(kScrewPositions[0].x), static_cast<float>(kScrewPositions[0].y)},
    {static_cast<float>(kScrewPositions[1].x), static_cast<float>(kScrewPositions[1].y)},
    {static_cast<float>(kScrewPositions[2].x), static_cast<float>(kScrewPositions[2].y)},
    {static_cast<float>(kScrewPositions[3].x), static_cast<float>(kScrewPositions[3].y)},
};

} // namespace FairyTapes::HappyEnding::Ui
