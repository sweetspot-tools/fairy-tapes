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

// GLUE: WARM slows the detector attack by this factor (the attack time constant is multiplied),
// and the controls glide over this many milliseconds (MAKEUP, MIX and the stage on/off).
inline constexpr double kGlueWarmAttackFactor = 3.0;
inline constexpr double kGlueRampMs = 10.0;

// METALHEAD (brickwall after FINAL) is built on the LIMIT engine as a fourth, fixed style that the
// LIMIT control does not offer: its look-ahead and release are fixed here, its attack ramp is as long
// as its look-ahead (so the ceiling holds by construction).
inline constexpr double kMetalLookAheadMs = 1.0;
inline constexpr double kMetalReleaseMs = 30.0;

// LIMIT: base release of each style (MODERN, PUNCH, DYN, then METAL); RELEASE trims around it.
inline constexpr double kLimitReleaseBaseMs[4] = {50.0, 100.0, 250.0, kMetalReleaseMs};

// LIMIT: look-ahead of each style (MODERN, PUNCH, DYN) and the length of the attack ramp that
// brings the gain down. The latency stays that of MODERN (the longest look-ahead) for every style.
// PUNCH ramps down more slowly than it looks ahead, so the first instant of a transient passes
// before the gain has fully arrived (the sample-peak guard still holds it under the ceiling).
inline constexpr double kLimitLookAheadMs[4] = {1.5, 0.3, 0.8, kMetalLookAheadMs};
inline constexpr double kLimitAttackMs[4] = {1.5, 0.45, 0.8, kMetalLookAheadMs};

// LIMIT DYN: soft knee width (the reduction starts this far below the threshold, centred on it) and
// the program-dependent release. The release time is the RELEASE value times a factor that goes from
// kLimitDynReleaseFast (brief peaks: the average reduction stays low) to kLimitDynReleaseSlow
// (sustained heavy reduction), following the reduction averaged over kLimitDynSustainMs; the
// reduction at which the factor reaches its slow end is kLimitDynGrRefDb.
inline constexpr double kLimitDynKneeDb = 6.0;
inline constexpr double kLimitDynReleaseFast = 0.5;
inline constexpr double kLimitDynReleaseSlow = 1.5;
inline constexpr double kLimitDynGrRefDb = 3.0;
inline constexpr double kLimitDynSustainMs = 400.0;

// LIMIT: the on/off crossfade between the delayed input and the limited signal.
inline constexpr double kLimitOnRampMs = 10.0;

// LIMIT SET: the forced threshold while the guided release setting is active, and the time the
// threshold takes to travel the whole distance from 0 dB to it (and back). Ordinary THRESH steps
// glide at the same speed.
inline constexpr double kSetThresholdDb = -8.0;
inline constexpr double kSetGlideMs = 150.0;

// FINAL: the INFLATE EFFECT and the CLIP DRIVE glide linearly over this many milliseconds, and so
// do the crossfades on switching the stage on or off and between its two modes.
inline constexpr double kFinalRampMs = 10.0;

// LOUDNESS macro: what the knob adds on top of the stage settings, in this order: CLIP first, then
// LIMIT, then FINAL. The knob travels 0..10 and each stage owns one third of it (CLIP over 0..10/3,
// LIMIT over 10/3..20/3, FINAL over 20/3..10): the stage ramps linearly over its third and then
// holds the maximum below.
struct LoudCurve {
  double clipDb;       // extra CLIP drive once its third is complete
  double limitDb;      // extra LIMIT threshold (negative = lower) once its third is complete
  double finalEffectMax;  // extra FINAL EFFECT (0..1 of its range) once its third is complete
  double finalDriveDb;    // extra FINAL DRIVE in dB once its third is complete
};
inline constexpr LoudCurve kLoudCurve{1.5, -3.0, 0.6, 2.0};

// OVERSAMPLER (linear-phase half-band stages, spec §9): the flat passband edge as a fraction of the
// base sample rate (ripple stays far below 0.1 dB up to here) and the stopband attenuation of the
// Kaiser design. The transition band sits between the passband edge and its mirror around the
// stage's own Nyquist, so images of content below the edge are rejected by at least this much.
inline constexpr double kOversamplerPassbandEdge = 0.4535;
inline constexpr double kOversamplerStopbandDb = 100.0;

// CLIP: the DRIVE gain (in and its compensation out) glides linearly over this many milliseconds.
inline constexpr double kClipRampMs = 10.0;

// MATCH: dB of LUFS error the LOUDNESS knob absorbs per 0.5 step of the knob.
inline constexpr double kMatchDbPerStep = 0.75;

} // namespace FairyTapes::HappyEnding::Tuning
