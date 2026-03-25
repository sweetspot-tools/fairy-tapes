/**
 * Fairy Tapes · The Stripper — DeNoise gate/expander (ported from Airwindows, MIT).
 * Multiband noise gate with Pear filter topology. Simplified to threshold + speed.
 */

#pragma once

#include <cstdint>

namespace FairyTapes::TheStripper::DSP {

class DeNoise {
public:
  DeNoise();

  void setSampleRate(double sampleRate);
  void reset();

  /** threshold 0..1 (applied equally to all 6 bands), speed 0..1, wet 0..1. */
  void setParams(float threshold, float speed, float wet);

  /** Process stereo in place. */
  void process(float* left, float* right, int numSamples);

private:
  double sampleRate_{44100.0};
  float threshold_{0.0f};
  float speed_{0.25f};
  float wet_{1.0f};

  enum {
    prevSampL1, prevSlewL1, prevSampL2, prevSlewL2,
    prevSampL3, prevSlewL3, prevSampL4, prevSlewL4,
    prevSampL5, prevSlewL5, prevSampL6, prevSlewL6,
    prevSampL7, prevSlewL7, prevSampL8, prevSlewL8,
    prevSampL9, prevSlewL9,
    prevSampR1, prevSlewR1, prevSampR2, prevSlewR2,
    prevSampR3, prevSlewR3, prevSampR4, prevSlewR4,
    prevSampR5, prevSlewR5, prevSampR6, prevSlewR6,
    prevSampR7, prevSlewR7, prevSampR8, prevSlewR8,
    prevSampR9, prevSlewR9,
    kPearMax,
    figL, gndL, figR, gndR,
    slew, freq, levlL, levlR,
    kPearTotal
  };

  double topLevlL_;
  double topLevlR_;
  double pearA_[kPearTotal];
  double pearB_[kPearTotal];
  double pearC_[kPearTotal];
  double pearD_[kPearTotal];
  double pearE_[kPearTotal];

  uint32_t fpdL_, fpdR_;
};

}  // namespace FairyTapes::TheStripper::DSP
