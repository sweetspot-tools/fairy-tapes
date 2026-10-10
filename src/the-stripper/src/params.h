/**
 * Fairy Tapes · The Stripper — Parameter IDs and constants.
 * No custom UI: host (e.g. Reaper) shows standard parameter list.
 */

#pragma once

#include "pluginterfaces/vst/vsttypes.h"
#include <cmath>

namespace FairyTapes::TheStripper {

enum ParamID : Steinberg::Vst::ParamID {
  kBypass = 0,
  kStripMode,       // 0 = Channel, 1 = Mix bus
  kInputGain,
  kOutputGain,
  kBypassGainStaging,
  kBypassLPHP,   // Bypass LP/HP section. Channel: 4K-EQ HPF/LPF. Mix bus: LP/HP active when feasible (same params).
  kBypassEQ,
  kBypassCompression,
  kBypassDrive,
  kBypassLimiter,
  kBypassGate,
  kGainStagingTrim,  // Channel only; gain staging not implemented; 0..1 -> linear or dB (kept for future)
  kCompressAmount,   // Multi-Comp (Channel / Mix bus) amount
  kCompressMix,      // dry/wet
  kCompressorMode,   // Channel only: 0=Voice, 1=Drums, 2=Classic (step 0,1,2). Mix bus ignores; engine = Bus.
  // 4K-EQ (Channel mode): HPF/LPF + 4 bands + type + saturation
  kEqHPFFreq,     // 20..500 Hz
  kEqHPFOn,       // 0/1
  kEqLPFFreq,     // 3000..20000 Hz
  kEqLPFOn,       // 0/1
  kEqLFGain,      // -20..+20 dB
  kEqLFFreq,      // 30..480 Hz
  kEqLFBell,      // 0 = shelf, 1 = bell (Black only)
  kEqLMGain, kEqLMFreq, kEqLMQ,   // LM: 200..2500 Hz, Q 0.4..4
  kEqHMGain, kEqHMFreq, kEqHMQ,   // HM: 600..7000 Hz, Q 0.4..4
  kEqHFGain, kEqHFFreq,           // HF: 1500..16000 Hz
  kEqHFBell,      // 0 = shelf, 1 = bell (Black only)
  // EQ Type (Brown/Black) removed: derived from Strip Mode (Channel=Black, Mix bus=Brown)
  kEqSaturation,  // 0..1 (0..100%)
  // Drive (NAM): single knob. Output auto-compensated. Always 100% wet (bypass = off).
  kDriveAmount,   // 0 = unity (model coloration only), 1 = max (+20 dB into model)
  // Added after state v7 (appended at the end so existing IDs never change):
  kLimiterPound,  // Limiter (BlockParty) pound amount, 0..1
  kGateThreshold, // Gate (DeNoise) threshold, 0..1
  kNumParams
};

// Metering: read-only parameters the processor writes to the controller (about 60 Hz). They live in their own id
// range, are not part of kNumParams, are never saved in the state and are not in the frozen parameter table.
// All values are normalised 0..1. kClearClips is the only writable one: a click on a clip LED sets it to 1.
constexpr Steinberg::Vst::ParamID kMeterBase = 1000;
enum MeterID : Steinberg::Vst::ParamID {
  kMeterInL = kMeterBase,   // peak, -60..+6 dBFS
  kMeterInR,
  kMeterOutL,
  kMeterOutR,
  kMeterGrComp,             // gain reduction, 0..20 dB (shown downwards)
  kMeterGrLimiter,          // 0..12 dB
  kClipInput,               // 0/1: the stage's output reached 0 dBFS in the last 2 s
  kClipEq,
  kClipComp,
  kClipDrive,
  kClipLimiter,
  kClipOutput,
  kClearClips,
  kNumMeterIDs
};
constexpr int kNumMeters = static_cast<int>(kNumMeterIDs - kMeterBase);

// Parameter count of state v7 (everything up to and including kDriveAmount).
constexpr int kNumParamsV7 = static_cast<int>(kDriveAmount) + 1;

// Strip mode values (normalized)
constexpr Steinberg::Vst::ParamValue kStripModeChannel = 0.0;
constexpr Steinberg::Vst::ParamValue kStripModeMixBus  = 1.0;

// Defaults
constexpr Steinberg::Vst::ParamValue kDefaultBypass = 0.0;
constexpr Steinberg::Vst::ParamValue kDefaultStripMode = kStripModeChannel;
constexpr Steinberg::Vst::ParamValue kDefaultInputGain = 0.5;
constexpr Steinberg::Vst::ParamValue kDefaultOutputGain = 0.5;
constexpr Steinberg::Vst::ParamValue kDefaultBypassStage = 0.0;  // 0 = stage on
constexpr Steinberg::Vst::ParamValue kDefaultGainStagingTrim = 0.5;
constexpr Steinberg::Vst::ParamValue kDefaultCompressAmount = 0.0;
constexpr Steinberg::Vst::ParamValue kDefaultCompressMix = 1.0;
// Compressor Mode: 0 = Voice (Opto), 0.5 = Drums (FET), 1 = Classic (VCA dbx 160). Step count 2 → 3 values.
constexpr Steinberg::Vst::ParamValue kDefaultCompressorMode = 0.0;

// 4K-EQ defaults
constexpr Steinberg::Vst::ParamValue kDefaultEqHPFFreq = 0.0;            // 20 Hz (eqNormToHPFFreq(0))
constexpr Steinberg::Vst::ParamValue kDefaultEqHPFOn = 1.0;   // IN lit by default: HPF 20 Hz is neutral
constexpr Steinberg::Vst::ParamValue kDefaultEqLPFFreq = 1.0;            // 20 kHz norm
constexpr Steinberg::Vst::ParamValue kDefaultEqLPFOn = 1.0;   // IN lit by default: LPF 20 kHz is neutral
constexpr Steinberg::Vst::ParamValue kDefaultEqLFGain = 0.5;             // 0 dB
constexpr Steinberg::Vst::ParamValue kDefaultEqLFFreq = (100.0f - 30.0f) / (480.0f - 30.0f);
constexpr Steinberg::Vst::ParamValue kDefaultEqLFBell = 0.0;
constexpr Steinberg::Vst::ParamValue kDefaultEqLMGain = 0.5;
constexpr Steinberg::Vst::ParamValue kDefaultEqLMFreq = (600.0f - 200.0f) / (2500.0f - 200.0f);
constexpr Steinberg::Vst::ParamValue kDefaultEqLMQ = (0.7f - 0.4f) / (4.0f - 0.4f);
constexpr Steinberg::Vst::ParamValue kDefaultEqHMGain = 0.5;
constexpr Steinberg::Vst::ParamValue kDefaultEqHMFreq = (2000.0f - 600.0f) / (7000.0f - 600.0f);
constexpr Steinberg::Vst::ParamValue kDefaultEqHMQ = (0.7f - 0.4f) / (4.0f - 0.4f);
constexpr Steinberg::Vst::ParamValue kDefaultEqHFGain = 0.5;
constexpr Steinberg::Vst::ParamValue kDefaultEqHFFreq = (8000.0f - 1500.0f) / (16000.0f - 1500.0f);
constexpr Steinberg::Vst::ParamValue kDefaultEqHFBell = 0.0;
constexpr Steinberg::Vst::ParamValue kDefaultEqSaturation = 0.0;

// Drive defaults
constexpr Steinberg::Vst::ParamValue kDefaultDriveAmount = 0.0;   // unity (coloration only)

// Limiter / Gate defaults (match the values that were hard-coded in the processor before they became parameters)
constexpr Steinberg::Vst::ParamValue kDefaultLimiterPound = 0.5;
constexpr Steinberg::Vst::ParamValue kDefaultGateThreshold = 0.0;

// Gain range in dB for display; normalized 0 = -20 dB, 1 = +20 dB
constexpr double kGainMinDb = -20.0;
constexpr double kGainMaxDb = 20.0;

inline double normalizedToDb(Steinberg::Vst::ParamValue norm) {
  if (norm <= 0.) return kGainMinDb;
  return kGainMinDb + norm * (kGainMaxDb - kGainMinDb);
}

inline Steinberg::Vst::ParamValue dbToNormalized(double db) {
  if (db <= kGainMinDb) return 0.;
  if (db >= kGainMaxDb) return 1.;
  return static_cast<Steinberg::Vst::ParamValue>((db - kGainMinDb) / (kGainMaxDb - kGainMinDb));
}

inline double normalizedToLinear(Steinberg::Vst::ParamValue norm) {
  return std::pow(10.0, normalizedToDb(norm) / 20.0);
}

// Gain staging trim: normalized 0..1 -> linear 0.1 .. 2 (approx -20 dB to +6 dB)
inline double gainStagingTrimToLinear(Steinberg::Vst::ParamValue norm) {
  if (norm <= 0.) return 0.1;
  if (norm >= 1.) return 2.0;
  return 0.1 + norm * 1.9;
}

inline bool isBypassStage(Steinberg::Vst::ParamValue value) {
  return value >= 0.5;
}

inline bool isChannelMode(Steinberg::Vst::ParamValue stripMode) {
  return stripMode < 0.5;
}

// Drive: normalized -> linear gain
// Drive amount: 0..1 -> 1x..10x (0..+20 dB) — exponential taper
inline double driveAmountToLinear(Steinberg::Vst::ParamValue norm) {
  if (norm <= 0.) return 1.0;
  return std::pow(10.0, norm);  // 0->1x, 0.5->~3.16x, 1->10x
}

// 4K-EQ: normalized 0..1 <-> frequency / gain / Q
inline float eqNormToHPFFreq(Steinberg::Vst::ParamValue n) {
  if (n <= 0.0) return 20.0f;
  return 20.0f + static_cast<float>(n) * (500.0f - 20.0f);
}
inline float eqNormToLPFFreq(Steinberg::Vst::ParamValue n) {
  if (n >= 1.0) return 20000.0f;
  return 3000.0f + static_cast<float>(n) * (20000.0f - 3000.0f);
}
inline float eqNormToLFFreq(Steinberg::Vst::ParamValue n) {
  return 30.0f + static_cast<float>(n) * (480.0f - 30.0f);
}
inline float eqNormToLMFreq(Steinberg::Vst::ParamValue n) {
  return 200.0f + static_cast<float>(n) * (2500.0f - 200.0f);
}
inline float eqNormToHMFreq(Steinberg::Vst::ParamValue n) {
  return 600.0f + static_cast<float>(n) * (7000.0f - 600.0f);
}
inline float eqNormToHFFreq(Steinberg::Vst::ParamValue n) {
  return 1500.0f + static_cast<float>(n) * (16000.0f - 1500.0f);
}
// EQ band gain: 0..1 -> -20..+20 dB
inline float eqNormToGainDb(Steinberg::Vst::ParamValue n) {
  return -20.0f + static_cast<float>(n) * 40.0f;
}
// LM/HM Q: 0..1 -> 0.4..4
inline float eqNormToQ(Steinberg::Vst::ParamValue n) {
  return 0.4f + static_cast<float>(n) * (4.0f - 0.4f);
}

} // namespace FairyTapes::TheStripper
