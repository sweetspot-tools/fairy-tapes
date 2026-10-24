/**
 * Fairy Tapes · The Stripper — Tape stage (ported from Airwindows Tape, MIT).
 *
 * Single-knob tape: Amount drives an internal macro that sets input slam and
 * head bump together. Two voicings mirror real-world use:
 *   Tracking (Channel mode)  — multitrack machine, hit hard, more head bump.
 *   Mixdown  (Mix bus mode)  — 2-track master machine, gentler slam and bump.
 * Output level is auto-compensated (stereo-linked RMS) against the input,
 * so Amount changes timbre without changing perceived loudness.
 */

#pragma once

#include <cstdint>

namespace FairyTapes::TheStripper::DSP {

class TapeSaturation {
public:
  enum class Voicing { Tracking, Mixdown };

  TapeSaturation();

  void setSampleRate(double sampleRate);
  void reset();

  /** amount 0..1 (macro over slam + head bump). */
  void setParams(float amount, Voicing voicing);

  /** Process stereo in place. */
  void process(float* left, float* right, int numSamples);

  /** Macro: input slam in dB for a given amount (0 dB at amount 0). */
  static float slamDb(float amount, Voicing voicing);
  /** Macro: head bump amount 0..1 for a given amount. */
  static float bump(float amount, Voicing voicing);

private:
  // Bandpass biquad coefficients, Airwindows layout: [2]=a0, [3]=a1 (always 0), [4]=a2, [5]=b1, [6]=b2.
  struct BiquadCoefs {
    double a0{0.0}, a2{0.0}, b1{0.0}, b2{0.0};
  };
  struct BiquadState {
    double s1{0.0}, s2{0.0};
  };
  // Airwindows Tape runs two interleaved filter sets (A/B) on alternating samples.
  struct Channel {
    double iirMidRoller[2];
    double iirHeadBump[2];
    BiquadState headBumpFilter[2];
    BiquadState mainFilter[2];
    double lastSample;
    uint32_t fpd;
  };

  double processSample(Channel& ch, double input, bool flip, double inputGain, double bumpGain,
                       double headBumpFreq, double rollAmount) const;
  static double runBiquad(const BiquadCoefs& c, BiquadState& st, double x);
  void updateCoefficients();

  double sampleRate_{44100.0};
  float amount_{0.0f};
  Voicing voicing_{Voicing::Tracking};

  BiquadCoefs headBumpCoefs_;
  BiquadCoefs mainCoefs_;
  Channel channels_[2];
  bool flip_{false};

  // Auto-compensation (stereo-linked RMS)
  float inputRmsSmooth_{0.0f};
  float outputRmsSmooth_{0.0f};
  float compensation_{1.0f};
};

}  // namespace FairyTapes::TheStripper::DSP
