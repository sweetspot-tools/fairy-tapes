/**
 * Fairy Tapes · Hotline — Parameter IDs, defaults and pure mapping helpers.
 */

#pragma once

#include "pluginterfaces/vst/vsttypes.h"
#include <algorithm>
#include <cmath>

namespace FairyTapes::Hotline {

enum ParamID : Steinberg::Vst::ParamID {
  kBypass = 0,
  kDevice,
  kMorph,
  kGain,
  kMono,
  kDrive,
  kTone,
  kNoise,
  kRoom,
  kMix,
  kCharacterIn,
  kNumParams
};

constexpr int kNumDevices = 6;

// Read-only hidden display parameters (processor -> editor), outside the persistent table.
// Peaks are normalized with the dB map 0..1 below (meterNormFromDb, the range the editor's LED rows show); clip flags are 0/1.
constexpr Steinberg::Vst::ParamID kMeterBase = 1000;
enum DisplayID : Steinberg::Vst::ParamID {
  kMeterInL = kMeterBase,
  kMeterInR,
  kMeterOutL,
  kMeterOutR,
  kClipIn,
  kClipDrive,
  kClipOut,
  kClipClear,   // the only writable one: the editor sets it to 1, the processor clears the clip flags
  kNumDisplayIDs
};

// Level meters: the processor publishes the peaks, the editor's LED rows read them, both over this dB range.
constexpr double kMeterMinDb = -60.0;
constexpr double kMeterMaxDb = 6.0;

// Defaults (normalized)
constexpr double kDefaultBypass = 0.0;
constexpr double kDefaultDevice = 0.0;
constexpr double kDefaultMorph = 1.0;
constexpr double kDefaultGain = 0.5;
constexpr double kDefaultMono = 0.0;
constexpr double kDefaultDrive = 0.5;
constexpr double kDefaultTone = 0.5;
constexpr double kDefaultNoise = 0.5;
constexpr double kDefaultRoom = 0.5;
constexpr double kDefaultMix = 1.0;
constexpr double kDefaultCharacterIn = 1.0;

namespace detail {
inline double clamp01(double n) { return std::isfinite(n) ? std::clamp(n, 0.0, 1.0) : 0.0; }
} // namespace detail

// Gain: -20..+20 dB.
inline double gainDbFromNorm(double n) { return -20.0 + 40.0 * detail::clamp01(n); }

// Drive / Noise / Room: multiplier of the device value, 0 (off) .. 1 (n = 0.5) .. 2.
inline double amountFromNorm(double n) { return 2.0 * detail::clamp01(n); }

// Tone: moves the device LPF by -1..+1 octaves and its HPF by +0.5..-0.5 octaves.
inline double toneLpOctavesFromNorm(double n) { return (detail::clamp01(n) - 0.5) * 2.0; }
inline double toneHpOctavesFromNorm(double n) { return -(detail::clamp01(n) - 0.5) * 1.0; }

// Device list: rounds over kNumDevices entries like a StringListParameter.
inline int deviceIndexFromNorm(double n) {
  return static_cast<int>(std::lround(detail::clamp01(n) * (kNumDevices - 1)));
}

// Index <-> normalized value of the Device parameter: the inverse of deviceIndexFromNorm (an index out of range is clamped).
inline double normFromDeviceIndex(int index) {
  return static_cast<double>(std::clamp(index, 0, kNumDevices - 1)) / (kNumDevices - 1);
}

// Peak level in dB <-> normalized meter value (linear over kMeterMinDb..kMeterMaxDb, clamped; NaN -> 0).
inline double meterNormFromDb(double db) { return detail::clamp01((db - kMeterMinDb) / (kMeterMaxDb - kMeterMinDb)); }
inline double meterDbFromNorm(double n) { return kMeterMinDb + detail::clamp01(n) * (kMeterMaxDb - kMeterMinDb); }

} // namespace FairyTapes::Hotline
