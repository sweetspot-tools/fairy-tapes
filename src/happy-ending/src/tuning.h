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
