/**
 * Fairy Tapes · The Stripper — ButterComp algorithm (Airwindows, MIT).
 * Ported from Airwindows ButterComp; no VST2 dependency.
 * Used for Channel mode compression (neutral glue).
 */

#pragma once

namespace FairyTapes::TheStripper::DSP {

class ButterComp {
public:
  ButterComp();

  /** Set sample rate (for time constants). Call when host changes rate. */
  void setSampleRate(double sampleRate);

  /**
   * Process stereo buffer in place.
   * A = amount (0..1), B = dry/wet (0 = dry, 1 = wet).
   */
  void process(float* left, float* right, int numSamples,
               float amountNorm, float mixNorm);

private:
  double sampleRate_;
  double controlAposL_, controlAnegL_, targetposL_, targetnegL_;
  double controlAposR_, controlAnegR_, targetposR_, targetnegR_;
  unsigned int fpdL_, fpdR_;
};

} // namespace FairyTapes::TheStripper::DSP
