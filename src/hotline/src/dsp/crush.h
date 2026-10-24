#pragma once
#include <algorithm>
#include <cmath>

namespace FairyTapes::Hotline::DSP {

/**
 * Sample-rate / bit-depth reducer: sample-and-hold at rateHz, mid-tread quantiser with step
 * 2^(1-bits). Exact pass-through when rateHz >= fs and bits >= 24 (neutral). Cheap set(), keeps
 * the hold; non-finite parameters are ignored, non-finite input samples are read as 0.
 */
class Crush {
public:
  void prepare(double fs) { fs_ = fs > 0.0 ? fs : 48000.0; rate_ = fs_; bits_ = 24.0; update(); neutral_ = true; clear(); }
  void set(double rateHz, double bits) {
    if (std::isfinite(rateHz)) rate_ = rateHz;
    if (std::isfinite(bits)) bits_ = bits;
    const bool wasNeutral = neutral_;
    update();
    if (neutral_) return;
    if (wasNeutral) clear();  // hold/phase are frozen while bypassed: drop them on re-activation
  }
  float process(float x) {
    if (neutral_) return x;
    if (!std::isfinite(x)) x = 0.f;
    if (phase_ >= 1.0) {
      phase_ -= 1.0;
      hold_ = quantise(x);
    }
    phase_ += ratio_;
    return hold_;
  }
  void clear() { hold_ = 0.f; phase_ = 1.0; }  // phase 1: the next sample is taken straight away
  bool neutral() const { return neutral_; }

private:
  void update() {
    ratio_ = std::clamp(rate_ / fs_, 1e-4, 1.0);
    bits_ = std::clamp(bits_, 1.0, 24.0);
    rate_ = std::max(rate_, 0.0);
    neutral_ = rate_ >= fs_ && bits_ >= 24.0;
    quantise_ = bits_ < 24.0;
    step_ = (float)std::ldexp(1.0, 1) * (float)std::pow(2.0, -bits_);
    inv_ = 1.f / step_;
  }
  float quantise(float x) const {
    if (!quantise_) return x;
    float y = std::nearbyint(x * inv_) * step_;
    if (x < 1.f) y = std::min(y, 1.f - step_);  // 2^bits levels over [-1, 1)
    return y;
  }
  double fs_ = 48000.0, rate_ = 48000.0, bits_ = 24.0, ratio_ = 1.0, phase_ = 1.0;
  float step_ = 1.f, inv_ = 1.f, hold_ = 0.f;
  bool neutral_ = true, quantise_ = false;
};

}  // namespace FairyTapes::Hotline::DSP
