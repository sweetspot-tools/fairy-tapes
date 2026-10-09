/**
 * Fairy Tapes · Pillow Talk — Parameter IDs, defaults and pure mapping helpers.
 */

#pragma once

#include "pluginterfaces/vst/vsttypes.h"
#include <cmath>

namespace FairyTapes::PillowTalk {

enum ParamID : Steinberg::Vst::ParamID {
  kBypass = 0,
  kTimeSync,
  kTimeMs,
  kTimeNote,
  kFeedback,
  kTone,
  kSaturation,
  kWowFlutter,
  kMix,
  kThreshold,
  kDuckDepth,
  kHoldSync,
  kHoldMs,
  kHoldNote,
  kRelease,
  kSidechain,
  kNumParams
};

// Read-only hidden display parameters (processor -> editor), outside the persistent table.
// Normalized: voice and echo = meter norm of the peak in dB over -60..0, send = linear gain 0..1.
constexpr Steinberg::Vst::ParamID kMeterBase = 1000;
enum DisplayID : Steinberg::Vst::ParamID {
  kDispVoice = kMeterBase,
  kDispSend,
  kDispEcho,
  kNumDisplayIDs
};

// Defaults (normalized)
constexpr double kDefaultTimeSync = 0.0;
constexpr double kDefaultTime = 0.5;
constexpr double kDefaultFeedback = 0.4;
constexpr double kDefaultTone = 0.6;
constexpr double kDefaultSaturation = 0.3;
constexpr double kDefaultWowFlutter = 0.2;
constexpr double kDefaultMix = 0.3;
constexpr double kDefaultThreshold = 0.42;  // ~ -35 dB
constexpr double kDefaultDepth = 0.6;       // ~ 36 dB
constexpr double kDefaultHoldSync = 0.0;
constexpr double kDefaultHold = 0.5;
constexpr double kDefaultRelease = 0.5;
constexpr double kDefaultSidechain = 0.0;

// Duck depth at normalized 1.0 means full mute.
constexpr double kDuckMuteDb = 60.0;

namespace detail {
inline double logMap(double n, double lo, double hi) {
  return lo * std::pow(hi / lo, n);
}
} // namespace detail

// noteIndex 0..3 = 1/16, 1/8, 1/4, 1/2. bpm <= 0 falls back to 120.
inline double noteToMs(int noteIndex, double bpm) {
  if (bpm <= 0.0) bpm = 120.0;
  const double quarterMs = 60000.0 / bpm;
  static constexpr double kQuarters[4] = {0.25, 0.5, 1.0, 2.0};
  if (noteIndex < 0) noteIndex = 0;
  if (noteIndex > 3) noteIndex = 3;
  return quarterMs * kQuarters[noteIndex];
}

inline double timeMsFromNorm(double n) { return detail::logMap(n, 20.0, 1200.0); }
inline double holdMsFromNorm(double n) { return detail::logMap(n, 50.0, 1000.0); }
inline double releaseMsFromNorm(double n) { return detail::logMap(n, 50.0, 2000.0); }
inline double feedbackFromNorm(double n) { return 0.95 * n; }
inline double toneHzFromNorm(double n) { return detail::logMap(n, 800.0, 12000.0); }
inline double thresholdDbFromNorm(double n) { return -60.0 + 60.0 * n; }
inline double duckDepthDbFromNorm(double n) { return kDuckMuteDb * n; }

} // namespace FairyTapes::PillowTalk
