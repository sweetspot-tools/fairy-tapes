#include "dsp/ducker.h"
#include "params.h"
#include <algorithm>
#include <cmath>

namespace FairyTapes::PillowTalk::DSP {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kHpHz = 120.0, kLpHz = 5000.0;
constexpr double kEnvAttackMs = 2.0, kEnvReleaseMs = 3.0, kSmoothMs = 3.0;
constexpr float kHoldCompMs = 8.f;

float msToCoef(double ms, double sr) {
  return (float)std::exp(-1.0 / (0.001 * ms * sr));
}
} // namespace

void Ducker::designHighpass(Biquad& b, double fs, double fc, double q) {
  const double w = 2.0 * kPi * fc / fs, c = std::cos(w), al = std::sin(w) / (2.0 * q);
  const double a0 = 1.0 + al;
  b.b0 = (float)(((1.0 + c) / 2.0) / a0);
  b.b1 = (float)(-(1.0 + c) / a0);
  b.b2 = b.b0;
  b.a1 = (float)(-2.0 * c / a0);
  b.a2 = (float)((1.0 - al) / a0);
}

void Ducker::designLowpass(Biquad& b, double fs, double fc, double q) {
  const double w = 2.0 * kPi * fc / fs, c = std::cos(w), al = std::sin(w) / (2.0 * q);
  const double a0 = 1.0 + al;
  b.b0 = (float)(((1.0 - c) / 2.0) / a0);
  b.b1 = (float)((1.0 - c) / a0);
  b.b2 = b.b0;
  b.a1 = (float)(-2.0 * c / a0);
  b.a2 = (float)((1.0 - al) / a0);
}

void Ducker::prepare(double sampleRate) {
  sr_ = sampleRate > 0.0 ? sampleRate : 48000.0;
  // High-pass is 4th order (two Butterworth sections) so that sub-80 Hz
  // rumble falls well below the threshold; low-pass is 12 dB/oct.
  designHighpass(hp1_, sr_, kHpHz, 0.5411961);
  designHighpass(hp2_, sr_, kHpHz, 1.3065630);
  designLowpass(lp_, sr_, kLpHz, 0.7071068);
  envAtk_ = msToCoef(kEnvAttackMs, sr_);
  envRel_ = msToCoef(kEnvReleaseMs, sr_);
  smoothCoef_ = msToCoef(kSmoothMs, sr_);
  updateDerived();
  reset();
}

void Ducker::setParams(float thresholdDb, float depthDb, float holdMs, float releaseMs) {
  thresholdLin_ = std::pow(10.f, thresholdDb / 20.f);
  duckGain_ = depthDb >= (float)kDuckMuteDb ? 0.f : std::pow(10.f, -std::max(0.f, depthDb) / 20.f);
  holdMs_ = std::max(0.f, holdMs);
  releaseMs_ = std::max(1.f, releaseMs);
  updateDerived();
}

void Ducker::updateDerived() {
  // Envelope tail + gain smoother add ~8 ms of delay on average; compensate.
  holdSamples_ = (long)(std::max(0.f, holdMs_ - kHoldCompMs) * 0.001 * sr_);
  relCoef_ = msToCoef(releaseMs_, sr_);
}

void Ducker::reset() {
  hp1_.clear(); hp2_.clear(); lp_.clear();
  env_ = 0.f;
  target_ = 1.f;
  gain_ = 1.f;
  holdCounter_ = 0;
}

float Ducker::processSample(float scL, float scR) {
  const float x = lp_.process(hp2_.process(hp1_.process(0.5f * (scL + scR))));
  const float a = std::fabs(x);
  env_ = a > env_ ? envAtk_ * env_ + (1.f - envAtk_) * a : envRel_ * env_ + (1.f - envRel_) * a;
  if (env_ < 1e-20f) env_ = 0.f;

  if (env_ > thresholdLin_) {
    holdCounter_ = holdSamples_;
    target_ = duckGain_;
  } else if (holdCounter_ > 0) {
    --holdCounter_;
  } else {
    target_ = 1.f - (1.f - target_) * relCoef_;
    if (target_ > 0.99999f) target_ = 1.f;
  }

  gain_ = smoothCoef_ * gain_ + (1.f - smoothCoef_) * target_;
  if (std::fabs(gain_ - target_) < 1e-9f) gain_ = target_;
  return std::clamp(gain_, 0.f, 1.f);
}

} // namespace FairyTapes::PillowTalk::DSP
