#pragma once
#include "dsp/biquad.h"

namespace FairyTapes::PillowTalk::DSP {

/**
 * Vocal-aware ducker: sidechain -> mono -> band-pass -> envelope -> threshold
 * with hold and exponential release. Outputs a per-sample send gain in [0,1].
 */
class Ducker {
public:
  void prepare(double sampleRate);
  void setParams(float thresholdDb, float depthDb, float holdMs, float releaseMs);
  float processSample(float scL, float scR);
  void reset();

private:
  void updateDerived();

  double sr_ = 48000.0;
  Biquad hp1_, hp2_, lp_;
  float envAtk_ = 0.f, envRel_ = 0.f, relCoef_ = 0.f, smoothCoef_ = 0.f;
  float env_ = 0.f, target_ = 1.f, gain_ = 1.f;
  long holdCounter_ = 0;

  float thresholdLin_ = 0.01f;
  float duckGain_ = 0.0316f;
  float holdMs_ = 200.f, releaseMs_ = 300.f;
  long holdSamples_ = 9600;
};

} // namespace FairyTapes::PillowTalk::DSP
