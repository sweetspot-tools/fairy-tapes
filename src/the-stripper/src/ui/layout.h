/**
 * Fairy Tapes · The Stripper — editor layout table, IN-button polarity, dimming rules.
 * Header-only and free of VSTGUI so it is unit tested. Coordinates are logical px of the 1000 x 628 panel
 * (layout of src/the-stripper/docs/mockups/stripper_rack.html).
 */

#pragma once

#include "params.h"

#include <cmath>
#include <cstddef>

namespace FairyTapes::TheStripper::Ui {

constexpr int kEditorWidth = 1000;
constexpr int kEditorHeight = 628;

struct Rect {
  double x, y, w, h;
};

enum class Kind { Knob, Toggle, Choice };

// A module is a box of the panel; its controls dim together.
enum class Module { None, Input, Filters, Level, Output, Eq, Comp, Drive, Limiter, Gate };
constexpr Module kModules[] = {Module::Input, Module::Filters, Module::Output, Module::Eq,
                               Module::Comp,  Module::Drive,    Module::Limiter, Module::Gate};
constexpr int kModuleCount = 10;   // size of ModuleState::in (indexed by Module)

struct BoxSpec {
  Rect rect;
  const char* title;
  bool titleOnTop;   // the EQ box carries its title on the top border, the others on the bottom one
  Module module;     // module held by this box
};

constexpr BoxSpec kBoxes[] = {
    {{24, 92, 150, 170}, "INPUT", false, Module::Input},
    {{180, 92, 230, 170}, "FILTERS", false, Module::Filters},
    {{416, 92, 368, 170}, "LEVEL", false, Module::Level},
    {{790, 92, 186, 170}, "OUTPUT", false, Module::Output},
    {{24, 282, 952, 150}, "EQUALISER", true, Module::Eq},
    {{24, 452, 342, 146}, "COMPRESSION", false, Module::Comp},
    {{372, 452, 300, 146}, "DRIVE", false, Module::Drive},
    {{678, 452, 162, 146}, "LIMITER", false, Module::Limiter},
    {{846, 452, 130, 146}, "GATE", false, Module::Gate},
};

inline const BoxSpec* boxOf(Module m) {
  for (const auto& b : kBoxes)
    if (b.module == m) return &b;
  return nullptr;
}

struct ControlSpec {
  Steinberg::Vst::ParamID id;
  Kind kind;
  Module module;
  Rect rect;
  const char* label;        // knob/toggle text
  bool isIn;                // the module's IN button
  bool inverted;            // the parameter is a bypass: the control shows the opposite
  const char* choices[3];   // Choice only
  bool sharesView = false;  // no view of its own: another entry's view shows this parameter at times
};

// Cell of a knob: AnalogKnob::kPreferredWidth x kPreferredHeight, centre kCenterOffsetY below the top.
constexpr Rect knob(double cx, double cy) { return {cx - 32, cy - 30, 64, 88}; }
constexpr Rect button(double cx, double cy, double w = 24, double h = 22) { return {cx - w / 2, cy - h / 2, w, h}; }

constexpr ControlSpec kControls[] = {
    // Input / Filters / Output
    {kInputGain, Kind::Knob, Module::Input, knob(99, 168), "GAIN", false, false, {}},
    {kEqHPFFreq, Kind::Knob, Module::Filters, knob(233, 168), "HIGH PASS", false, false, {}},
    {kEqLPFFreq, Kind::Knob, Module::Filters, knob(337, 168), "LOW PASS", false, false, {}},
    {kBypassLPHP, Kind::Toggle, Module::Filters, button(382, 130), "IN", true, true, {}},   // one IN for both filters
    {kOutputGain, Kind::Knob, Module::Output, knob(854, 168), "GAIN", false, false, {}},
    {kBypass, Kind::Toggle, Module::None, button(940, 130), "BYPASS", false, false, {}},
    {kStripMode, Kind::Choice, Module::Output, {923, 185, 34, 52}, nullptr, false, false, {"CHAN", "BUS"}},   // stacked
    // Equaliser: ten band knobs, then SATURATE (the 4K console saturation is the last step of the EQ, back from the Drive)
    {kEqLFGain, Kind::Knob, Module::Eq, knob(78, 352), "GAIN", false, false, {}},
    {kEqLFFreq, Kind::Knob, Module::Eq, knob(160, 352), "FREQ", false, false, {}},
    {kEqLFBell, Kind::Toggle, Module::Eq, button(181, 306, 26, 18), nullptr, false, false, {}},
    {kEqLMGain, Kind::Knob, Module::Eq, knob(240, 352), "GAIN", false, false, {}},
    {kEqLMFreq, Kind::Knob, Module::Eq, knob(322, 352), "FREQ", false, false, {}},
    {kEqLMQ, Kind::Knob, Module::Eq, knob(404, 352), "Q", false, false, {}},
    {kEqHMGain, Kind::Knob, Module::Eq, knob(484, 352), "GAIN", false, false, {}},
    {kEqHMFreq, Kind::Knob, Module::Eq, knob(566, 352), "FREQ", false, false, {}},
    {kEqHMQ, Kind::Knob, Module::Eq, knob(648, 352), "Q", false, false, {}},
    {kEqHFGain, Kind::Knob, Module::Eq, knob(728, 352), "GAIN", false, false, {}},
    {kEqHFFreq, Kind::Knob, Module::Eq, knob(810, 352), "FREQ", false, false, {}},
    {kEqHFBell, Kind::Toggle, Module::Eq, button(831, 306, 26, 18), nullptr, false, false, {}},
    {kEqSaturation, Kind::Knob, Module::Eq, knob(891, 352), "SATURATE", false, false, {}},
    {kBypassEQ, Kind::Toggle, Module::Eq, button(940, 320), "IN", true, true, {}},
    // Compression
    {kCompressAmount, Kind::Knob, Module::Comp, knob(80, 520), "AMOUNT", false, false, {}},
    {kCompressMix, Kind::Knob, Module::Comp, knob(170, 520), "MIX", false, false, {}},
    {kCompressorMode, Kind::Choice, Module::Comp, {240, 485, 44, 78}, nullptr, false, false,
     {"VOICE", "DRUMS", "CLASSIC"}},   // stacked beside the knobs, away from the IN
    {kBypassCompression, Kind::Toggle, Module::Comp, button(330, 490), "IN", true, true, {}},
    // Drive, Limiter, Gate
    // Two engines in series, each with its knob and its on/off switch (left of the module IN): NAM, then TAPE.
    {kDriveAmount, Kind::Knob, Module::Drive, knob(416, 520), "DRIVE", false, false, {}},
    {kDriveTapeAmount, Kind::Knob, Module::Drive, knob(498, 520), "TAPE", false, false, {}},
    {kDriveNamOn, Kind::Toggle, Module::Drive, button(566, 490), "NAM", false, false, {}},
    {kDriveTapeOn, Kind::Toggle, Module::Drive, button(601, 490), "TAPE", false, false, {}},
    {kBypassDrive, Kind::Toggle, Module::Drive, button(636, 490), "IN", true, true, {}},
    {kLimiterPound, Kind::Knob, Module::Limiter, knob(752, 520), "POUND", false, false, {}},
    {kBypassLimiter, Kind::Toggle, Module::Limiter, button(811, 490), "IN", true, true, {}},
    {kGateThreshold, Kind::Knob, Module::Gate, knob(903, 520), "THRESH", false, false, {}},
    {kBypassGate, Kind::Toggle, Module::Gate, button(947, 490), "IN", true, true, {}},
};

// Printed look of the knobs (silkscreen layer): end values of the scale and cap radius (preview stripper_rack.html).
struct KnobLook {
  Steinberg::Vst::ParamID id;
  const char* lo;
  const char* hi;
  double radius;
};
constexpr KnobLook kKnobLooks[] = {
    {kInputGain, "-20", "+20", 22},      {kEqHPFFreq, "20", "500", 19},       {kEqLPFFreq, "3k", "20k", 19},
    {kOutputGain, "-20", "+20", 22},     {kEqLFGain, "-20", "+20", 19},       {kEqLFFreq, "30", "480", 19},
    {kEqLMGain, "-20", "+20", 19},       {kEqLMFreq, ".2k", "2.5k", 19},      {kEqLMQ, ".4", "4", 19},
    {kEqHMGain, "-20", "+20", 19},       {kEqHMFreq, ".6k", "7k", 19},        {kEqHMQ, ".4", "4", 19},
    {kEqHFGain, "-20", "+20", 19},       {kEqHFFreq, "1.5k", "16k", 19},      {kCompressAmount, "0", "10", 19},
    {kCompressMix, "DRY", "WET", 19},    {kDriveAmount, "0", "10", 21},       {kEqSaturation, "0", "10", 22},
    {kDriveTapeAmount, "0", "10", 19},
    {kLimiterPound, "0", "10", 19},      {kGateThreshold, "-\xE2\x88\x9E", "0", 19},
};
inline const KnobLook* knobLookOf(Steinberg::Vst::ParamID id) {
  for (const auto& k : kKnobLooks)
    if (k.id == id) return &k;
  return nullptr;
}
// Exclusive choices (Chan/Bus, Voice/Drums/Classic) carry the name on the face, which lights amber on the
// active one (design system, 2026-10-21): the control rect is the whole group, a grid of `columns` columns.
struct ChoiceLook {
  Steinberg::Vst::ParamID id;
  int columns;
  double faceWidth;
};
constexpr ChoiceLook kChoiceLooks[] = {{kStripMode, 1, 34}, {kCompressorMode, 1, 44}};
inline const ChoiceLook* choiceLookOf(Steinberg::Vst::ParamID id) {
  for (const auto& c : kChoiceLooks)
    if (c.id == id) return &c;
  return nullptr;
}
// The two Bell buttons show a bell icon on an amber face instead of a label and an LED.
inline bool isBellButton(Steinberg::Vst::ParamID id) { return id == kEqLFBell || id == kEqHFBell; }
// Height added under a button face for its LED (the view is taller than the face; the layout rect is the face).
constexpr double kLedZone = 14;

// Header of an EQ band: a rule from `left` to `right` with the band name above it (the Bell button, if any, sits at its right end).
struct EqBand {
  const char* name;
  double left, right;
};
constexpr double kEqBandRuleY = 306;
constexpr EqBand kEqBands[] = {{"LF", 42, 162}, {"LM", 204, 440}, {"HM", 448, 684}, {"HF", 692, 812}};

// Level box: LED rows (peak meters, then gain reduction) and the clip LEDs (spec 4.1).
constexpr int kMeterLeds = 15;
constexpr double kMeterLeft = 489, kMeterPitch = 18, kMeterBarHeight = 8;
constexpr double kMeterLabelX = 486;   // row tags (L/R/C), right-aligned just left of the bars
struct MeterRow {
  Steinberg::Vst::ParamID id;
  double cy;
  const char* tag;
  bool gainReduction;
  bool alwaysLit;   // stays at full light under the global bypass
};
constexpr MeterRow kMeterRows[] = {
    {kMeterInL, 114, "L", false, true},    {kMeterInR, 128, "R", false, true},
    {kMeterOutL, 156, "L", false, true},   {kMeterOutR, 170, "R", false, true},
    {kMeterGrComp, 192, "C", true, false}, {kMeterGrLimiter, 205, "L", true, false}, {kMeterGrGate, 218, "G", true, false},
};
constexpr int kMeterRowCount = 7;
static_assert(sizeof(kMeterRows) / sizeof(kMeterRows[0]) == kMeterRowCount, "kMeterRowCount must match kMeterRows");
constexpr Rect meterBarRect(const MeterRow& row) {
  return {kMeterLeft, row.cy - kMeterBarHeight / 2, kMeterPitch * kMeterLeds, kMeterBarHeight};
}

struct ClipSpec {
  Steinberg::Vst::ParamID id;
  const char* label;
  bool alwaysLit;
};
constexpr int kClipCount = 6;
constexpr ClipSpec kClips[kClipCount] = {{kClipInput, "IN", true}, {kClipEq, "EQ", false}, {kClipComp, "CMP", false},
                                         {kClipDrive, "DRV", false}, {kClipLimiter, "LIM", false},
                                         {kClipOutput, "OUT", true}};
constexpr double kClipRowY = 240, kClipFirstX = 502, kClipStep = 44;   // LED centre of the first, pitch
constexpr Rect clipRect(int index) { return {kClipFirstX + index * kClipStep - 4.4, kClipRowY - 8, 40, 16}; }

// Section tags printed left of the rows (amber) and the dB scale under the output rows (dim white).
constexpr double kMeterScaleY = 146;
constexpr int kMeterScaleDb[] = {-60, -48, -36, -24, -18, -12, -6, 0};   // one label per LED at most (no overlap)

// Bar position (0..1) of a gain-reduction parameter value (SSL encoding) on a scale of `maxDb` dB.
inline double grBarNorm(double value, double maxDb) {
  if (!(value > 0.0) || !(maxDb > 0.0)) return 0.0;
  const double db = -20.0 * std::log10(1.0 - (value < 1.0 - 1e-9 ? value : 1.0 - 1e-9));
  return db >= maxDb ? 1.0 : db / maxDb;
}
constexpr double kGrCompMaxDb = 20.0, kGrLimiterMaxDb = 12.0, kGrGateMaxDb = 40.0;

// An engine's knob in the Drive dims when its switch is off; the NAM's also without a model (it passes the signal through).
inline bool engineKnobDimmed(Steinberg::Vst::ParamID id, bool namOn, bool tapeOn, bool modelLoaded) {
  if (id == kDriveAmount) return !namOn || !modelLoaded;
  if (id == kDriveTapeAmount) return !tapeOn;
  return false;
}
// The paper strip with the model name dims with the NAM switch (it stays clickable: a model can be chosen first).
inline bool stripDimmed(bool namOn) { return !namOn; }

// The small tag printed at the top left of a block: what is mounted on it right now (spec 11, "Serigrafia dei blocchi").
// `compressorMode` is the parameter value 0 / 0.5 / 1 (Voice / Drums / Classic). Blocks without a tag return "".
inline const char* blockTag(Module m, bool channelMode, double compressorMode, bool namOn, bool tapeOn) {
  const char* series = channelMode ? "4K-G" : "4K-E";
  switch (m) {
  case Module::Filters:
  case Module::Eq: return series;
  case Module::Comp:
    if (!channelMode) return "4K-G";   // the Bus compressor
    return compressorMode < 0.25 ? "OPTO-2A" : compressorMode < 0.75 ? "FET-76" : "VCA-160";
  case Module::Drive: return namOn && tapeOn ? "NAM \xC2\xB7 TAPE" : namOn ? "NAM" : tapeOn ? "TAPE" : "";
  case Module::Limiter: return "B-PARTY";
  case Module::Gate: return "D-NOISE";
  default: return "";
  }
}

inline bool meterDimmed(bool alwaysLit, bool globalBypass) { return globalBypass && !alwaysLit; }

// Paper tape with the model name, bottom right of the Drive box, right of the TAPE knob (a click opens the model picker).
constexpr Rect kModelStrip = {540, 546, 122, 28};

inline const ControlSpec* inButtonOf(Module m) {
  for (const auto& c : kControls)
    if (c.isIn && c.module == m) return &c;
  return nullptr;
}

// An IN button shows the opposite of its bypass parameter (lit = module in the signal path).
inline double uiValue(bool inverted, double paramNorm) { return inverted ? 1.0 - paramNorm : paramNorm; }
inline double paramValue(bool inverted, double uiNorm) { return inverted ? 1.0 - uiNorm : uiNorm; }

struct ModuleState {
  bool in[kModuleCount];
  bool globalBypass = false;
  ModuleState() {
    for (bool& b : in) b = true;
  }
};

// Dimmed when the global bypass is on or the module's own IN is off (modules without IN follow the global bypass).
inline bool moduleDimmed(Module m, const ModuleState& s) {
  return s.globalBypass || (inButtonOf(m) && !s.in[static_cast<int>(m)]);
}

// Controls that exist only in Channel mode (the Mix bus engine ignores the compressor mode).
// The Bell filter is part of the Black (Channel) EQ only; in Bus mode the DSP ignores it, so the buttons are out of use.
inline bool controlInUse(Steinberg::Vst::ParamID id, bool channelMode) { return channelMode || !isBellButton(id); }

inline bool visibleInMode(Steinberg::Vst::ParamID id, bool channelMode) {
  return id != kCompressorMode || channelMode;
}

} // namespace FairyTapes::TheStripper::Ui
