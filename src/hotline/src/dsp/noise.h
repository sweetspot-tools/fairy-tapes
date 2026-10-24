#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace FairyTapes::Hotline::DSP {

/**
 * Line noise: low-passed hiss (90% of the power) plus sparse single-sample crackle (10%), both
 * from one deterministic xorshift32. levelDb is the total RMS (clamped to <= 0 dB); at <= -120 dB
 * next() returns exactly 0 and the generator does not advance. Non-finite levels are ignored.
 * It is a source, so there is no input-dependent state to go stale.
 */
class Noise {
public:
  void prepare(double fs, uint32_t seed) {
    fs_ = fs > 0.0 ? fs : 48000.0;
    // scramble so that nearby seeds give unrelated sequences; xorshift must never hold 0
    uint32_t s = seed * 2654435761u + 0x9E3779B9u;
    s ^= s >> 15; s *= 2246822519u; s ^= s >> 13;
    rng_ = s ? s : 0x1234567u;
    const double a = std::exp(-2.0 * 3.14159265358979323846 * 5000.0 / fs_);
    a_ = (float)a;
    // white uniform [-1,1] has variance 1/3; y = a*y + (1-a)*w has variance (1-a)/(1+a) of that
    hissScale_ = (float)std::sqrt(0.9 / ((1.0 / 3.0) * (1.0 - a) / (1.0 + a)));
    crackleProb_ = (uint32_t)std::min(kCrackleHz / fs_ * 4294967296.0, 4294967295.0);
    crackleAmp_ = (float)std::sqrt(0.3 * fs_ / kCrackleHz);  // power = rate/fs * A^2/3 = 0.1
    lp_ = 0.f;
    level_ = -120.0; gain_ = 0.f;
  }
  void set(double levelDb) {
    if (!std::isfinite(levelDb)) return;
    level_ = std::min(levelDb, 0.0);
    gain_ = level_ <= -120.0 ? 0.f : (float)std::pow(10.0, level_ / 20.0);
  }
  float next() {
    if (gain_ == 0.f) return 0.f;
    const float w = uni();
    lp_ = a_ * lp_ + (1.f - a_) * w;
    float y = hissScale_ * lp_;
    if (draw() < crackleProb_) y += crackleAmp_ * uni();
    return gain_ * y;
  }

private:
  static constexpr double kCrackleHz = 15.0;
  uint32_t draw() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return rng_; }
  float uni() { return (float)((double)draw() * (2.0 / 4294967296.0) - 1.0); }
  double fs_ = 48000.0, level_ = -120.0;
  uint32_t rng_ = 0x1234567u, crackleProb_ = 0;
  float a_ = 0.f, hissScale_ = 0.f, crackleAmp_ = 0.f, lp_ = 0.f, gain_ = 0.f;
};

}  // namespace FairyTapes::Hotline::DSP
