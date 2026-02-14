/**
 * Fairy Tapes · The Stripper — Biquad filter (direct form II).
 * Used by 4K-EQ DSP (coefficient logic from Dusk Audio 4K-EQ, GPL-3.0).
 */

#pragma once

namespace FairyTapes::TheStripper::DSP {

class Biquad {
public:
  Biquad() = default;

  void setCoefficients(float b0, float b1, float b2, float a1, float a2) {
    b0_ = b0;
    b1_ = b1;
    b2_ = b2;
    a1_ = a1;
    a2_ = a2;
  }

  float processSample(float x) {
    float w = x - a1_ * w1_ - a2_ * w2_;
    float y = b0_ * w + b1_ * w1_ + b2_ * w2_;
    w2_ = w1_;
    w1_ = w;
    return y;
  }

  void reset() {
    w1_ = 0.f;
    w2_ = 0.f;
  }

private:
  float b0_ = 1.f, b1_ = 0.f, b2_ = 0.f, a1_ = 0.f, a2_ = 0.f;
  float w1_ = 0.f, w2_ = 0.f;
};

}  // namespace FairyTapes::TheStripper::DSP
