/**
 * Fairy Tapes · The Stripper — BlockParty limiter (ported from Airwindows, MIT).
 * Vari-mu style analog limiter with moderately saturated character.
 */

#pragma once

#include <cstdint>

namespace FairyTapes::TheStripper::DSP {

class BlockParty {
public:
  BlockParty();

  void setSampleRate(double sampleRate);
  void reset();

  /** pound 0..1 (limiting amount), wet 0..1 (dry/wet mix). */
  void setParams(float pound, float wet);

  /** Process stereo in place. */
  void process(float* left, float* right, int numSamples);

private:
  double sampleRate_{44100.0};
  float pound_{0.0f};
  float wet_{1.0f};

  // Per-channel state (L)
  double muVaryL_, muAttackL_, muNewSpeedL_;
  double muSpeedAL_, muSpeedBL_, muSpeedCL_, muSpeedDL_, muSpeedEL_;
  double muCoefficientAL_, muCoefficientBL_, muCoefficientCL_, muCoefficientDL_, muCoefficientEL_;
  double lastCoefficientAL_, lastCoefficientBL_, lastCoefficientCL_, lastCoefficientDL_;
  double mergedCoefficientsL_;
  double thresholdL_, thresholdBL_;

  // Per-channel state (R)
  double muVaryR_, muAttackR_, muNewSpeedR_;
  double muSpeedAR_, muSpeedBR_, muSpeedCR_, muSpeedDR_, muSpeedER_;
  double muCoefficientAR_, muCoefficientBR_, muCoefficientCR_, muCoefficientDR_, muCoefficientER_;
  double lastCoefficientAR_, lastCoefficientBR_, lastCoefficientCR_, lastCoefficientDR_;
  double mergedCoefficientsR_;
  double thresholdR_, thresholdBR_;

  int count_;
  bool fpFlip_;

  uint32_t fpdL_, fpdR_;
};

}  // namespace FairyTapes::TheStripper::DSP
