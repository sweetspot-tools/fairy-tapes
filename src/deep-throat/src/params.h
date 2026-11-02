/**
 * Fairy Tapes · Deep Throat — Parameter IDs, defaults and pure mapping helpers.
 */

#pragma once

#include "pluginterfaces/vst/vsttypes.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace FairyTapes::DeepThroat {

enum ParamID : Steinberg::Vst::ParamID {
  kBypass = 0,
  kMode,
  kInput,
  kRiderTarget,
  kRiderRange,
  kBreathAmount,
  kDeEssAmount,
  kDeEssListen,
  kCompAmount,
  kTone,
  kOutput,
  kRiderIn,
  kBreathIn,
  kEqIn,
  kDeEssIn,
  kCompIn,
  kToneIn,
  kBreathSens,
  kBreathListen,
  kDeEssFreq,
  kCompMix,
  kCompRatio,
  kCompDist,
  kCompHpf,
  kEqAmount,
  kNumParams
};

// Hidden read-only display parameters (processor -> editor) and the writable clear-clips trigger.
// They are not part of the persistent parameter table (ids >= kNumParams are ignored by the state).
constexpr Steinberg::Vst::ParamID kMeterBase = 1000;
enum DisplayID : Steinberg::Vst::ParamID {
  kDispInL = kMeterBase,
  kDispInR,
  kDispOutL,
  kDispOutR,
  kDispGr,
  kDispVoice,
  kDispRider,
  kDispBreath,
  kDispClipIn,
  kDispClipOut,
  kNumDisplayIDs
};
constexpr Steinberg::Vst::ParamID kClearClips = kMeterBase + 100;

enum class Mode { Speak, Sing, Puke };

// LATENCY (spec 9b): a project setting of the back panel (SETTINGS > BEHAVIOUR), not a parameter and not automatable.
// MIX: look-ahead 80 ms + the spectral frame (the host compensates it); LIVE: no look-ahead, latency 0, the level breath
// detector in every mode. The values are the ones stored in the state (uint32).
enum class LatencyMode : uint32_t { Mix = 0, Live = 1 };
constexpr int kLatencyModeCount = 2;

// Step counts of the choice parameters (StringListParameter: norm = index / (count - 1)).
constexpr int kModeCount = 3;
constexpr int kCompRatioCount = 8;  // AUTO, 2, 3, 4, 6, 8, 10, 20 :1
constexpr int kCompDistCount = 4;   // AUTO, OFF, Dist 2, Dist 3
constexpr int kCompHpfCount = 3;    // OFF, 80 Hz, 150 Hz

// Defaults (normalized)
constexpr double kDefaultBypass = 0.0;
constexpr double kDefaultMode = 0.0;  // Speak
constexpr double kDefaultInput = 0.5;
constexpr double kDefaultRiderTarget = 0.5;
constexpr double kDefaultRiderRange = 0.5;
constexpr double kDefaultBreathAmount = 0.5;
constexpr double kDefaultDeEssAmount = 0.5;
constexpr double kDefaultDeEssListen = 0.0;
constexpr double kDefaultCompAmount = 0.5;
constexpr double kDefaultTone = 0.5;
constexpr double kDefaultOutput = 0.5;
constexpr double kDefaultIn = 1.0;  // all section IN buttons
constexpr double kDefaultBreathSens = 0.5;
constexpr double kDefaultBreathListen = 0.0;
constexpr double kDefaultDeEssFreq = 0.53;  // ~6500 Hz
constexpr double kDefaultCompMix = 1.0;
constexpr double kDefaultCompRatio = 0.0;  // AUTO
constexpr double kDefaultCompDist = 0.0;   // AUTO
constexpr double kDefaultCompHpf = 0.5;    // 80 Hz
constexpr double kDefaultEqAmount = 0.5;

// Fills `d` (kNumParams entries) with the normalized defaults, indexed by ParamID.
inline void loadDefaults(double* d) {
  d[kBypass] = kDefaultBypass;
  d[kMode] = kDefaultMode;
  d[kInput] = kDefaultInput;
  d[kRiderTarget] = kDefaultRiderTarget;
  d[kRiderRange] = kDefaultRiderRange;
  d[kBreathAmount] = kDefaultBreathAmount;
  d[kDeEssAmount] = kDefaultDeEssAmount;
  d[kDeEssListen] = kDefaultDeEssListen;
  d[kCompAmount] = kDefaultCompAmount;
  d[kTone] = kDefaultTone;
  d[kOutput] = kDefaultOutput;
  d[kRiderIn] = d[kBreathIn] = d[kEqIn] = d[kDeEssIn] = d[kCompIn] = d[kToneIn] = kDefaultIn;
  d[kBreathSens] = kDefaultBreathSens;
  d[kBreathListen] = kDefaultBreathListen;
  d[kDeEssFreq] = kDefaultDeEssFreq;
  d[kCompMix] = kDefaultCompMix;
  d[kCompRatio] = kDefaultCompRatio;
  d[kCompDist] = kDefaultCompDist;
  d[kCompHpf] = kDefaultCompHpf;
  d[kEqAmount] = kDefaultEqAmount;
}

namespace detail {
inline double clamp01(double n) { return std::clamp(n, 0.0, 1.0); }
// Index 0..count-1 of a stepped parameter at normalized value n, the same entry the SDK's
// StringListParameter displays (Parameter::toPlain: min(count - 1, floor(n * count))). The exact
// steps index / (count - 1) map to their index.
inline int stepIndex(double n, int count) {
  return std::min(count - 1, (int)std::floor(clamp01(n) * count));
}
} // namespace detail

inline Mode modeFromNorm(double n) { return (Mode)detail::stepIndex(n, kModeCount); }

inline double inputDbFromNorm(double n) { return -12.0 + 24.0 * detail::clamp01(n); }
inline double outputDbFromNorm(double n) { return -12.0 + 24.0 * detail::clamp01(n); }
inline double riderTargetDbFromNorm(double n) { return -30.0 + 20.0 * detail::clamp01(n); }

// 4000..10000 Hz, logarithmic.
inline double deEssFreqHzFromNorm(double n) {
  return 4000.0 * std::pow(10000.0 / 4000.0, detail::clamp01(n));
}

inline int compRatioChoiceFromNorm(double n) { return detail::stepIndex(n, kCompRatioCount); }
inline int compDistChoiceFromNorm(double n) { return detail::stepIndex(n, kCompDistCount); }
inline int compHpfChoiceFromNorm(double n) { return detail::stepIndex(n, kCompHpfCount); }

// Ratio for a choice index: 0 (AUTO) -> 0.0, otherwise N of N:1.
inline double compRatioFromChoice(int choice) {
  static constexpr double kRatios[kCompRatioCount] = {0.0, 2.0, 3.0, 4.0, 6.0, 8.0, 10.0, 20.0};
  return kRatios[std::clamp(choice, 0, kCompRatioCount - 1)];
}
// Side-chain high-pass for a choice index: 0 (OFF) -> 0.0 Hz.
inline double compHpfHzFromChoice(int choice) {
  static constexpr double kHz[kCompHpfCount] = {0.0, 80.0, 150.0};
  return kHz[std::clamp(choice, 0, kCompHpfCount - 1)];
}

inline double dbToGain(double db) { return std::pow(10.0, db / 20.0); }

} // namespace FairyTapes::DeepThroat
