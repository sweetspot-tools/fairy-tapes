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
enum class Module { None, Input, HighPass, LowPass, Level, Output, Eq, Comp, Drive, Limiter, Gate };
constexpr Module kModules[] = {Module::Input, Module::HighPass, Module::LowPass, Module::Output, Module::Eq,
                               Module::Comp,  Module::Drive,    Module::Limiter, Module::Gate};
constexpr int kModuleCount = 11;   // size of ModuleState::in (indexed by Module)

struct BoxSpec {
  Rect rect;
  const char* title;
  bool titleOnTop;   // the EQ box carries its title on the top border, the others on the bottom one
  Module module;     // box that holds this module (HighPass and LowPass share FILTERS)
};

constexpr BoxSpec kBoxes[] = {
    {{24, 92, 150, 170}, "INPUT", false, Module::Input},
    {{180, 92, 230, 170}, "FILTERS", false, Module::HighPass},
    {{416, 92, 368, 170}, "LEVEL", false, Module::Level},
    {{790, 92, 186, 170}, "OUTPUT", false, Module::Output},
    {{24, 282, 952, 150}, "EQUALISER", true, Module::Eq},
    {{24, 452, 372, 146}, "COMPRESSION", false, Module::Comp},
    {{402, 452, 300, 146}, "DRIVE \xC2\xB7 NAM", false, Module::Drive},
    {{708, 452, 132, 146}, "LIMITER", false, Module::Limiter},
    {{846, 452, 130, 146}, "GATE", false, Module::Gate},
};

inline const BoxSpec* boxOf(Module m) {
  if (m == Module::LowPass) m = Module::HighPass;
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
};

// Cell of a knob: AnalogKnob::kPreferredWidth x kPreferredHeight, centre kCenterOffsetY below the top.
constexpr Rect knob(double cx, double cy) { return {cx - 32, cy - 30, 64, 88}; }
constexpr Rect button(double cx, double cy, double w = 24, double h = 22) { return {cx - w / 2, cy - h / 2, w, h}; }

constexpr ControlSpec kControls[] = {
    // Input / Filters / Output
    {kInputGain, Kind::Knob, Module::Input, knob(99, 160), "GAIN", false, false, {}},
    {kEqHPFFreq, Kind::Knob, Module::HighPass, knob(233, 160), "HIGH PASS", false, false, {}},
    {kEqHPFOn, Kind::Toggle, Module::HighPass, button(282, 130), "IN", true, false, {}},
    {kEqLPFFreq, Kind::Knob, Module::LowPass, knob(337, 160), "LOW PASS", false, false, {}},
    {kEqLPFOn, Kind::Toggle, Module::LowPass, button(382, 130), "IN", true, false, {}},
    {kOutputGain, Kind::Knob, Module::Output, knob(842, 160), "GAIN", false, false, {}},
    {kBypass, Kind::Toggle, Module::None, button(940, 130, 56), "BYPASS", false, false, {}},
    {kStripMode, Kind::Choice, Module::Output, button(930, 206, 84), nullptr, false, false, {"CHAN", "BUS"}},
    // Equaliser
    {kEqLFGain, Kind::Knob, Module::Eq, knob(78, 352), "GAIN", false, false, {}},
    {kEqLFFreq, Kind::Knob, Module::Eq, knob(160, 352), "FREQ", false, false, {}},
    {kEqLFBell, Kind::Toggle, Module::Eq, button(181, 306, 26, 18), "BELL", false, false, {}},
    {kEqLMGain, Kind::Knob, Module::Eq, knob(240, 352), "GAIN", false, false, {}},
    {kEqLMFreq, Kind::Knob, Module::Eq, knob(322, 352), "FREQ", false, false, {}},
    {kEqLMQ, Kind::Knob, Module::Eq, knob(404, 352), "Q", false, false, {}},
    {kEqHMGain, Kind::Knob, Module::Eq, knob(484, 352), "GAIN", false, false, {}},
    {kEqHMFreq, Kind::Knob, Module::Eq, knob(566, 352), "FREQ", false, false, {}},
    {kEqHMQ, Kind::Knob, Module::Eq, knob(648, 352), "Q", false, false, {}},
    {kEqHFGain, Kind::Knob, Module::Eq, knob(728, 352), "GAIN", false, false, {}},
    {kEqHFFreq, Kind::Knob, Module::Eq, knob(810, 352), "FREQ", false, false, {}},
    {kEqHFBell, Kind::Toggle, Module::Eq, button(831, 306, 26, 18), "BELL", false, false, {}},
    {kEqSaturation, Kind::Knob, Module::Eq, knob(891, 352), "SATURATE", false, false, {}},
    {kBypassEQ, Kind::Toggle, Module::Eq, button(940, 320), "IN", true, true, {}},
    // Compression
    {kCompressAmount, Kind::Knob, Module::Comp, knob(86, 520), "AMOUNT", false, false, {}},
    {kCompressMix, Kind::Knob, Module::Comp, knob(170, 520), "MIX", false, false, {}},
    {kCompressorMode, Kind::Choice, Module::Comp, button(300, 556, 168), nullptr, false, false,
     {"VOICE", "DRUMS", "CLASSIC"}},
    {kBypassCompression, Kind::Toggle, Module::Comp, button(360, 490), "IN", true, true, {}},
    // Drive, Limiter, Gate
    {kDriveAmount, Kind::Knob, Module::Drive, knob(458, 520), "DRIVE", false, false, {}},
    {kBypassDrive, Kind::Toggle, Module::Drive, button(666, 490), "IN", true, true, {}},
    {kLimiterPound, Kind::Knob, Module::Limiter, knob(766, 520), "POUND", false, false, {}},
    {kBypassLimiter, Kind::Toggle, Module::Limiter, button(811, 490), "IN", true, true, {}},
    {kGateThreshold, Kind::Knob, Module::Gate, knob(903, 520), "THRESH", false, false, {}},
    {kBypassGate, Kind::Toggle, Module::Gate, button(947, 490), "IN", true, true, {}},
};

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
    {kMeterGrComp, 194, "C", true, false}, {kMeterGrLimiter, 208, "L", true, false},
};
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
constexpr double kClipRowY = 232, kClipFirstX = 502, kClipStep = 44;   // LED centre of the first, pitch
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
constexpr double kGrCompMaxDb = 20.0, kGrLimiterMaxDb = 12.0;

inline bool meterDimmed(bool alwaysLit, bool globalBypass) { return globalBypass && !alwaysLit; }

// Paper tape with the model name, bottom right of the Drive box (a click opens the model picker).
constexpr Rect kModelStrip = {508, 546, 174, 28};

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
inline bool visibleInMode(Steinberg::Vst::ParamID id, bool channelMode) {
  return id != kCompressorMode || channelMode;
}

} // namespace FairyTapes::TheStripper::Ui
