#pragma once
#include "biquad.h"
#include <algorithm>

namespace FairyTapes::Hotline::DSP {

/** 4th-order Butterworth high-pass + low-pass (two biquads each). Cheap set(), keeps state. */
class Band {
public:
  void prepare(double fs) { fs_ = fs; set(hp_, lp_); clear(); }
  void set(double hpHz, double lpHz) {
    hp_ = std::max(hpHz, 10.0);
    lp_ = std::min(lpHz, 0.45 * fs_);
    hp_ = std::min(hp_, lp_ * 0.99);  // keep the two corners ordered at extreme settings
    const bool wasNeutral = neutral_;
    neutral_ = hp_ <= 20.5 && lp_ >= 19500.0;
    if (neutral_) return;
    if (wasNeutral) clear();  // state is frozen while bypassed: drop it on re-activation
    designHighpass(h1_, fs_, hp_, kQ1);
    designHighpass(h2_, fs_, hp_, kQ2);
    designLowpass(l1_, fs_, lp_, kQ1);
    designLowpass(l2_, fs_, lp_, kQ2);
  }
  float process(float x) {
    if (neutral_) return x;
    return l2_.process(l1_.process(h2_.process(h1_.process(x))));
  }
  void clear() { h1_.clear(); h2_.clear(); l1_.clear(); l2_.clear(); }
  bool neutral() const { return neutral_; }

private:
  static constexpr double kQ1 = 0.5412, kQ2 = 1.3065;
  double fs_ = 48000.0, hp_ = 10.0, lp_ = 20000.0;
  bool neutral_ = false;
  Biquad h1_, h2_, l1_, l2_;
};

}  // namespace FairyTapes::Hotline::DSP
