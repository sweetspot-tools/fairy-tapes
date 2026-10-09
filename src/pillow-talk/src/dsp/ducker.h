#pragma once
#include <cmath>

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
  struct Biquad {
    float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f;
    float z1 = 0.f, z2 = 0.f;
    float process(float x) {
      const float y = b0 * x + z1;
      z1 = b1 * x - a1 * y + z2;
      z2 = b2 * x - a2 * y;
      if (std::fabs(z1) < 1e-20f) z1 = 0.f;
      if (std::fabs(z2) < 1e-20f) z2 = 0.f;
      return y;
    }
    void clear() { z1 = z2 = 0.f; }
  };

  static void designHighpass(Biquad& b, double fs, double fc, double q);
  static void designLowpass(Biquad& b, double fs, double fc, double q);
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
