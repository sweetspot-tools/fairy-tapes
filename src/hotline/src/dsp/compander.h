#pragma once
#include <algorithm>
#include <cmath>

namespace FairyTapes::Hotline::DSP {

/**
 * Compressor (threshold -24 dB, ratio R) followed by a soft squelch gate: below squelchDb the
 * gain falls linearly (in amplitude, smoothstep) to 0 at squelchDb - 10 dB. Peak envelope with
 * 2 ms attack / 80 ms release. Exact pass-through when ratio <= 1 and squelchDb <= -120.
 * Both stages vanish continuously (ratio -> 1: gain reduction -> 0 dB). Cheap set(), keeps the
 * envelope; non-finite parameters are ignored, non-finite input samples are read as 0.
 */
class Compander {
public:
  void prepare(double fs) {
    fs_ = fs > 0.0 ? fs : 48000.0;
    att_ = (float)std::exp(-1.0 / (0.002 * fs_));
    rel_ = (float)std::exp(-1.0 / (0.080 * fs_));
    ratio_ = 1.0; squelch_ = -120.0;
    update(); neutral_ = true; clear();
  }
  void set(double ratio, double squelchDb) {
    if (std::isfinite(ratio)) ratio_ = ratio;
    if (std::isfinite(squelchDb)) squelch_ = squelchDb;
    const bool wasNeutral = neutral_;
    update();
    if (neutral_) return;
    if (wasNeutral) clear();  // envelope is frozen while bypassed: drop it on re-activation
  }
  float process(float x) {
    if (neutral_) return x;
    if (!std::isfinite(x)) x = 0.f;
    const float a = std::fabs(x);
    env_ = a > env_ ? a + att_ * (env_ - a) : a + rel_ * (env_ - a);
    if (env_ < 1e-20f) env_ = 0.f;
    const double envDb = 20.0 * std::log10(std::max((double)env_, 1e-9));
    double gainDb = 0.0, gate = 1.0;
    if (comp_ && envDb > kThreshDb) gainDb = (envDb - kThreshDb) * (1.0 / ratio_ - 1.0);
    if (gate_ && envDb < squelch_) {
      const double t = std::clamp((envDb - (squelch_ - 10.0)) / 10.0, 0.0, 1.0);
      gate = t * t * (3.0 - 2.0 * t);
    }
    return (float)(x * std::pow(10.0, gainDb / 20.0) * gate);
  }
  void clear() { env_ = 0.f; }
  bool neutral() const { return neutral_; }

private:
  static constexpr double kThreshDb = -24.0;
  void update() {
    ratio_ = std::clamp(ratio_, 1.0, 100.0);
    squelch_ = std::clamp(squelch_, -120.0, 0.0);
    comp_ = ratio_ > 1.0;
    gate_ = squelch_ > -120.0;
    neutral_ = !comp_ && !gate_;
  }
  double fs_ = 48000.0, ratio_ = 1.0, squelch_ = -120.0;
  float att_ = 0.f, rel_ = 0.f, env_ = 0.f;
  bool neutral_ = true, comp_ = false, gate_ = false;
};

}  // namespace FairyTapes::Hotline::DSP
