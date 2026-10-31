/**
 * Fairy Tapes · Happy Ending — Frozen parameter table: IDs, steps, defaults and value tables.
 *
 * Every control is stepped. A parameter has `steps` positions (index 0..steps-1) and is
 * normalized as index / (steps - 1). The order of ParamID is frozen (it is the saved state
 * and the automation identity): never reorder, only append before kNumParams if ever needed.
 */

#pragma once

#include "pluginterfaces/vst/vsttypes.h"
#include <array>
#include <cmath>
#include <cstddef>

namespace FairyTapes::HappyEnding {

enum ParamID : Steinberg::Vst::ParamID {
  // Service row
  kBypass = 0,
  kInputGain,
  kOutputGain,
  kCeiling,
  kLoudness,
  kTarget,
  kOversampling,  // Off / 2x / 4x, not automatable

  // FINE EQ: stage IN, then per band IN, M/S, FREQ, GAIN, Q (where the band has them)
  kFineOn,
  kHpOn, kHpMs, kHpFreq,
  kLsOn, kLsMs, kLsFreq, kLsGain,
  kLmOn, kLmMs, kLmFreq, kLmGain, kLmQ,
  kMidOn, kMidMs, kMidFreq, kMidGain, kMidQ,
  kHmOn, kHmMs, kHmFreq, kHmGain, kHmQ,
  kHsOn, kHsMs, kHsFreq, kHsGain,

  // VIBE EQ
  kVibeOn,
  kLowFreq, kLow,
  kAirFreq, kAir,
  kTilt,
  kTube,
  kDynLow, kDynAir,

  // GLUE
  kGlueOn,
  kGlueThresh, kGlueAttack, kGlueRelease, kGlueRatio,
  kGlueMakeup, kGlueMix, kGlueScHpf, kGlueWarm,

  // CLIP
  kClipOn,
  kClipDrive,

  // LIMIT
  kLimitOn,
  kLimitThresh, kLimitStyle, kLimitRelease,

  // FINAL
  kFinalOn,
  kFinalMode, kFinalEffect, kFinalCurve, kFinalDrive,

  // METALHEAD
  kMetalOn,
  kPush,

  kNumParams
};

// Hidden session parameters, outside the stored table (never part of the state):
// LISTEN (-1 = off, 0..2 = the three bells) and SET (LIMIT release setting on/off).
enum HiddenID : Steinberg::Vst::ParamID {
  kListenBand = kNumParams + 1,
  kSetActive
};

// LISTEN is a 4-position session parameter (-1, 0, 1, 2), normalized as (band + 1) / 3: off at the default.
inline constexpr int kListenSteps = 4;
inline constexpr double kDefaultListenNorm = 0.0;  // -1 = off
inline constexpr double kDefaultSetNorm = 0.0;     // SET off
inline double normFromListen(int band) {
  const int i = band < -1 ? 0 : (band > 2 ? 3 : band + 1);
  return static_cast<double>(i) / 3.0;
}
// Same reading as the SDK for a 3-step parameter: min(3, floor(norm * 4)).
inline int listenFromNorm(double norm) {
  if (!(norm > 0.0)) return -1;
  if (norm >= 1.0) return 2;
  const int i = static_cast<int>(norm * kListenSteps);
  return (i > 3 ? 3 : i) - 1;
}

// Display parameters (processor -> editor): hidden, read-only, outside the stored table. Values are
// normalized as in display.h (peaks over -60..0 dB, GR over 0..12 dB, LUFS over -60..0, clip 0/1).
// kClearClips and kResetIntegrated are the only writable ones: the editor sets 1, the processor does
// the action on the audio thread and writes 0 back (the acknowledgement).
inline constexpr Steinberg::Vst::ParamID kDisplayBase = 1000;
enum DisplayID : Steinberg::Vst::ParamID {
  kDispInL = kDisplayBase, kDispInR, kDispOutL, kDispOutR,
  kDispGr0, kDispGr1, kDispGr2, kDispGr3, kDispGr4,          // G C L F M
  kDispClip0, kDispClip1, kDispClip2, kDispClip3, kDispClip4,
  kDispClip5, kDispClip6, kDispClip7, kDispClip8,            // IN FEQ VEQ GLU CLP LIM FIN MTL OUT
  kDispLufsShort, kDispLufsInt,
  kDispDynLow, kDispDynAir,
  kDispLatency,                                              // samples, normalized over 0..kLatencyDisplayMax
  kClearClips,
  kResetIntegrated,
  kNumDisplayIDs
};

struct ParamSpec {
  const char* name;
  int steps;         // number of positions (2 for a switch)
  int defaultIndex;  // position 0..steps-1
};

namespace detail {

// n values lo, lo + step, ... (exact for the binary-friendly steps used here).
template <std::size_t N>
constexpr std::array<double, N> linear(double lo, double step) {
  std::array<double, N> a{};
  for (std::size_t i = 0; i < N; ++i) a[i] = lo + step * static_cast<double>(i);
  return a;
}

} // namespace detail

// Value tables. Namespaced because several share the name of their ParamID (kGlueThresh, kPush, ...).
// Frequencies in Hz, gains and thresholds in dB, times as named, percentages as 0..100.
namespace Tables {

// FINE EQ
inline constexpr std::array<double, 6> kHpFreqs{0, 20, 25, 30, 35, 40};       // 0 = Off
inline constexpr std::array<double, 6> kHpSideFreqs{0, 60, 80, 120, 160, 200}; // mono-bass scale, 0 = Off
inline constexpr std::array<double, 7> kLowShelfFreqs{30, 50, 70, 100, 150, 200, 300};
inline constexpr std::array<double, 10> kLoMidFreqs{100, 125, 160, 200, 250, 315, 400, 500, 630, 800};
inline constexpr std::array<double, 10> kMidFreqs{400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150};
inline constexpr std::array<double, 10> kHiMidFreqs{1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500};
inline constexpr std::array<double, 7> kHiShelfFreqs{3000, 5000, 8000, 10000, 12000, 16000, 20000};
inline constexpr auto kEqGains = detail::linear<17>(-4.0, 0.5);               // dB
inline constexpr std::array<double, 4> kQValues{0.5, 1, 2, 5};                // Wide / Medium / Narrow / Surgical

// VIBE EQ
inline constexpr auto kAmounts = detail::linear<11>(0.0, 1.0);             // 0..10 integers: LOW, AIR, TUBE
inline constexpr std::array<double, 4> kLowFreqs{20, 30, 60, 100};
inline constexpr std::array<double, 5> kAirFreqs{5000, 8000, 10000, 12000, 16000};
inline constexpr auto kTilts = detail::linear<13>(-3.0, 0.5);                 // dB, DARK..BRIGHT

// GLUE
inline constexpr auto kGlueThresh = detail::linear<21>(-20.0, 1.0);           // dB
inline constexpr std::array<double, 6> kGlueAttackMs{0.1, 0.3, 1, 3, 10, 30};
inline constexpr std::array<double, 5> kGlueReleaseS{0.1, 0.3, 0.6, 1.2, 0};  // 0 = Auto
inline constexpr std::array<double, 3> kGlueRatios{1.5, 2, 4};
inline constexpr auto kGlueMakeup = detail::linear<13>(0.0, 0.5);             // dB
inline constexpr auto kGlueMix = detail::linear<21>(0.0, 5.0);                // %
inline constexpr std::array<double, 4> kScHpfHz{0, 60, 90, 150};              // 0 = Off

// CLIP, LIMIT, FINAL, METALHEAD
inline constexpr auto kClipDrive = detail::linear<13>(0.0, 0.5);              // dB
inline constexpr auto kLimitThresh = detail::linear<9>(0.0, -0.5);            // dB, 0 .. -4
inline constexpr auto kFinalEffect = detail::linear<11>(0.0, 10.0);           // %
inline constexpr std::array<double, 5> kFinalCurve{0, 25, 50, 75, 100};       // %
inline constexpr auto kFinalDrive = detail::linear<5>(0.0, 0.5);              // dB
inline constexpr auto kPush = detail::linear<13>(0.0, 0.5);                   // dB

// Service row
inline constexpr std::array<double, 4> kCeilings{-0.1, -0.3, -0.5, -1.0};     // dB
inline constexpr auto kLoudness = detail::linear<21>(0.0, 0.5);              // 0..10 in half steps
inline constexpr std::array<double, 4> kTargets{-14, -11, -9, -7};            // LUFS
inline constexpr auto kIoGain = detail::linear<49>(-12.0, 0.5);               // dB, INPUT and OUTPUT

} // namespace Tables

// A value table seen through a pointer, so params with tables of different sizes share a type.
struct ValueTable {
  const double* data;
  std::size_t size;  // 0 = the parameter has no value table
};

namespace detail {
template <class A>
constexpr ValueTable tableOf(const A& a) { return {a.data(), a.size()}; }
} // namespace detail

// ParamID -> its value table. Parameters without one (switches, the M/S / STYLE / MODE /
// OVERSAMPLING selectors, kLimitRelease whose values come from Tuning) return {nullptr, 0}.
// kHpFreq reports the normal scale; on SIDE read kHpSideFreqs at the same index (hpFreqHz).
constexpr ValueTable valueTable(ParamID id) {
  switch (id) {
    case kInputGain: case kOutputGain: return detail::tableOf(Tables::kIoGain);
    case kCeiling:     return detail::tableOf(Tables::kCeilings);
    case kLoudness:    return detail::tableOf(Tables::kLoudness);
    case kLow: case kAir: case kTube: return detail::tableOf(Tables::kAmounts);
    case kTarget:      return detail::tableOf(Tables::kTargets);
    case kHpFreq:      return detail::tableOf(Tables::kHpFreqs);
    case kLsFreq:      return detail::tableOf(Tables::kLowShelfFreqs);
    case kLmFreq:      return detail::tableOf(Tables::kLoMidFreqs);
    case kMidFreq:     return detail::tableOf(Tables::kMidFreqs);
    case kHmFreq:      return detail::tableOf(Tables::kHiMidFreqs);
    case kHsFreq:      return detail::tableOf(Tables::kHiShelfFreqs);
    case kLsGain: case kLmGain: case kMidGain: case kHmGain: case kHsGain:
      return detail::tableOf(Tables::kEqGains);
    case kLmQ: case kMidQ: case kHmQ: return detail::tableOf(Tables::kQValues);
    case kLowFreq:     return detail::tableOf(Tables::kLowFreqs);
    case kAirFreq:     return detail::tableOf(Tables::kAirFreqs);
    case kTilt:        return detail::tableOf(Tables::kTilts);
    case kGlueThresh:  return detail::tableOf(Tables::kGlueThresh);
    case kGlueAttack:  return detail::tableOf(Tables::kGlueAttackMs);
    case kGlueRelease: return detail::tableOf(Tables::kGlueReleaseS);
    case kGlueRatio:   return detail::tableOf(Tables::kGlueRatios);
    case kGlueMakeup:  return detail::tableOf(Tables::kGlueMakeup);
    case kGlueMix:     return detail::tableOf(Tables::kGlueMix);
    case kGlueScHpf:   return detail::tableOf(Tables::kScHpfHz);
    case kClipDrive:   return detail::tableOf(Tables::kClipDrive);
    case kLimitThresh: return detail::tableOf(Tables::kLimitThresh);
    case kFinalEffect: return detail::tableOf(Tables::kFinalEffect);
    case kFinalCurve:  return detail::tableOf(Tables::kFinalCurve);
    case kFinalDrive:  return detail::tableOf(Tables::kFinalDrive);
    case kPush:        return detail::tableOf(Tables::kPush);
    default:           return {nullptr, 0};
  }
}

namespace detail {
constexpr ParamSpec sw(const char* name, int def) { return {name, 2, def}; }
} // namespace detail

// In ParamID order. Switches default to on for every stage and band except METALHEAD
// (off by default), the DYN buttons and WARM.
inline constexpr std::array<ParamSpec, kNumParams> kParams = {{
  // Service row
  detail::sw("Bypass", 0),
  {"Input Gain", 49, 24},
  {"Output Gain", 49, 24},
  {"Ceiling", 4, 1},
  {"Loudness", 21, 0},
  {"Target", 4, 0},
  {"Oversampling", 3, 1},
  // FINE EQ
  detail::sw("Fine EQ", 1),
  detail::sw("Hi Pass In", 1),   {"Hi Pass M/S", 3, 0},   {"Hi Pass Freq", 6, 0},
  detail::sw("Low Shelf In", 1), {"Low Shelf M/S", 3, 0}, {"Low Shelf Freq", 7, 3}, {"Low Shelf Gain", 17, 8},
  detail::sw("Lo Mid In", 1),    {"Lo Mid M/S", 3, 0},    {"Lo Mid Freq", 10, 4},   {"Lo Mid Gain", 17, 8},   {"Lo Mid Q", 4, 1},
  detail::sw("Mid In", 1),       {"Mid M/S", 3, 0},       {"Mid Freq", 10, 4},      {"Mid Gain", 17, 8},      {"Mid Q", 4, 1},
  detail::sw("Hi Mid In", 1),    {"Hi Mid M/S", 3, 0},    {"Hi Mid Freq", 10, 4},   {"Hi Mid Gain", 17, 8},   {"Hi Mid Q", 4, 1},
  detail::sw("Hi Shelf In", 1),  {"Hi Shelf M/S", 3, 0},  {"Hi Shelf Freq", 7, 4},  {"Hi Shelf Gain", 17, 8},
  // VIBE EQ
  detail::sw("Vibe EQ", 1),
  {"Low Freq", 4, 2}, {"Low", 11, 0},
  {"Air Freq", 5, 3}, {"Air", 11, 0},
  {"Tilt", 13, 6},
  {"Tube", 11, 2},
  detail::sw("Dyn Low", 0), detail::sw("Dyn Air", 0),
  // GLUE
  detail::sw("Glue", 1),
  {"Glue Thresh", 21, 10}, {"Glue Attack", 6, 3}, {"Glue Release", 5, 4}, {"Glue Ratio", 3, 1},
  {"Glue Makeup", 13, 0}, {"Glue Mix", 21, 10}, {"Glue SC HPF", 4, 1}, detail::sw("Glue Warm", 0),
  // CLIP
  detail::sw("Clip", 1),
  {"Clip Drive", 13, 4},
  // LIMIT
  detail::sw("Limit", 1),
  {"Limit Thresh", 9, 4}, {"Limit Style", 3, 0}, {"Limit Release", 9, 4},
  // FINAL
  detail::sw("Final", 1),
  {"Final Mode", 2, 0}, {"Final Effect", 11, 5}, {"Final Curve", 5, 1}, {"Final Drive", 5, 4},
  // METALHEAD
  detail::sw("Metalhead", 0),
  {"Push", 13, 4},
}};

// Position <-> normalized value. Out-of-range input is clamped.
// The index is min(steps - 1, floor(norm * steps)): exactly what the SDK's Parameter::toPlain and
// StringListParameter read (FromNormalized), so the processor, the controller (strings, MATCH) and
// the host's generic UI all see the same position for any normalized value, not only for the grid
// points index / (steps - 1).
inline int indexFromNorm(ParamID id, double norm) {
  const int steps = kParams[id].steps;
  if (!(norm > 0.0)) return 0;  // also NaN
  if (norm >= 1.0) return steps - 1;
  const int i = static_cast<int>(norm * steps);
  return i > steps - 1 ? steps - 1 : i;
}

inline double normFromIndex(ParamID id, int index) {
  const int last = kParams[id].steps - 1;
  if (index <= 0) return 0.0;
  if (index >= last) return 1.0;
  return static_cast<double>(index) / static_cast<double>(last);
}

// Normalized defaults of the whole table, in ParamID order (kNumParams values).
inline void loadDefaults(double* norm) {
  for (int i = 0; i < static_cast<int>(kNumParams); ++i)
    norm[i] = normFromIndex(static_cast<ParamID>(i), kParams[static_cast<std::size_t>(i)].defaultIndex);
}

// The oversampling choice is a project setting, not an automation target.
constexpr bool isAutomatable(ParamID id) { return id != kOversampling; }

// HI PASS on SIDE uses the mono-bass scale at the same index; the index stays limited to the
// range of the normal scale (kHpFreq has Tables::kHpFreqs.size() positions).
constexpr int hpIndexForScale(bool side, int index) {
  const int last = static_cast<int>(Tables::kHpFreqs.size()) - 1;
  if (!side) return index;
  return index < 0 ? 0 : (index > last ? last : index);
}

constexpr double hpFreqHz(bool side, int index) {
  return side ? Tables::kHpSideFreqs[hpIndexForScale(true, index)] : Tables::kHpFreqs[index];
}

} // namespace FairyTapes::HappyEnding
