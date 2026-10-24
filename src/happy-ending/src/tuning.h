/**
 * Fairy Tapes · Happy Ending — Tuning table.
 *
 * Every value to be voiced by ear lives here and only here (spec §9). They start from the
 * values of the preview (src/happy-ending/docs/mockups/happy_ending_rack.html); the tuning task
 * of the plan adjusts them. Tests read these constants, never copies of the numbers.
 */

#pragma once

namespace FairyTapes::HappyEnding::Tuning {

// FINE EQ: Q-coupling (a bell narrows as its gain grows: Q_eff = Q * (1 + |gain dB| / divisor)),
// the Q of the shelves and of the 12 dB/octave high pass, and the coefficient ramp length.
inline constexpr double kFineQCouplingDb = 8.0;
inline constexpr double kFineShelfQ = 0.7071067811865476;
inline constexpr double kFineHighPassQ = 0.7071067811865476;
inline constexpr int kFineRampSamples = 64;

// VIBE EQ: DYN pulls the LOW / AIR boost back on loud program (provisional starting values).
inline constexpr double kDynThresholdDb = -18.0;  // level above which the boost retreats
inline constexpr double kDynAttackMs = 10.0;
inline constexpr double kDynReleaseMs = 200.0;
inline constexpr double kDynRangeDb = 12.0;       // dB over the threshold at which the pull-back is complete
inline constexpr double kDynMaxPullback = 0.8;    // share of the LOW / AIR amount removed at most
inline constexpr double kDynLowDetectHz = 400.0;  // the LOW detector hears what is below this
inline constexpr double kDynAirDetectHz = 2000.0; // the AIR detector hears what is above this

// VIBE EQ network (port of the Tube EQ of Multi-Q, remapped to the stepped controls).
inline constexpr double kVibeLowBoostDbPerStep = 1.4;  // LOW boost, per step of the 0-10 knob
inline constexpr double kVibeLowAttenRatio = 0.5;      // the knob also drives the cut: attenuation steps = ratio * LOW
inline constexpr double kVibeLowDipDbPerStep = 1.75;   // cut, per step of the attenuation
inline constexpr double kVibeLowDipFreqRatio = 2.5;    // the cut sits this many times above the boost frequency
inline constexpr double kVibeLowDipQ = 0.7;
inline constexpr double kVibeAirDbPerStep = 1.8;       // AIR boost, per step of the 0-10 knob
inline constexpr double kVibeAirQ = 0.8;               // broad (built with the matched peaking, so it keeps its width near Nyquist)
inline constexpr double kVibeTiltHz = 1000.0;          // the two opposite shelves of TILT meet here
inline constexpr double kVibeTiltQ = 0.7071067811865476;
inline constexpr double kVibeTubeDriveMax = 1.0;       // tube stage drive at TUBE 10 (1.0 = the Multi-Q maximum)
// The inductors of the LC network work well below the signal level: the Multi-Q source says 10-15 dB
// in its comments but feeds them the full level, which compresses hard on hot boosted program.
// Their input is scaled by this many dB (and the output scaled back): core saturation then only
// colours the hottest peaks.
inline constexpr double kVibeInductorLevelDb = -30.0;
inline constexpr int kVibeSubBlock = 32;               // samples between coefficient updates (linear ramp)
inline constexpr int kVibeTubeRampSamples = 256;       // length of the TUBE drive ramp

// LIMIT: base release of each style (MODERN, PUNCH, DYN); RELEASE trims around it.
inline constexpr double kLimitReleaseBaseMs[3] = {50.0, 100.0, 250.0};

// LIMIT SET: the forced threshold while the guided release setting is active.
inline constexpr double kSetThresholdDb = -8.0;

// LOUDNESS macro: what the last third of the knob adds to each stage, in this order
// (CLIP first, then LIMIT, then FINAL). The knob travels 0..10; each stage owns one third.
struct LoudCurve {
  double clipDb;   // extra CLIP drive at the end of the first third
  double limitDb;  // extra LIMIT threshold (negative = lower) at the end of the second third
  double final;    // extra FINAL EFFECT / DRIVE, 0..1 of its range, at the end of the last third
};
inline constexpr LoudCurve kLoudCurve{1.5, -3.0, 1.0};

// MATCH: dB of LUFS error the LOUDNESS knob absorbs per 0.5 step of the knob.
inline constexpr double kMatchDbPerStep = 0.75;

} // namespace FairyTapes::HappyEnding::Tuning
