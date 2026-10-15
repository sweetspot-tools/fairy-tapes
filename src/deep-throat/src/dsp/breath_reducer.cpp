#include "dsp/breath_reducer.h"

#include <algorithm>
#include <cmath>

namespace FairyTapes::DeepThroat::DSP {

namespace {
constexpr double kMaxReductionDb = 12.0;
constexpr double kAttackMs = 5.0;
constexpr double kReleaseMs = 30.0;
constexpr double kDbFloor = 1e-6;  // |dB| below this snaps to exactly 0 dB (unity)
double coeffFor(double ms, double sr) { return std::exp(-1.0 / std::max(1.0, ms * 0.001 * sr)); }
}

void BreathReducer::prepare(double sampleRate) {
  const double sr = sampleRate > 1.0 ? sampleRate : 48000.0;
  attackCoeff_ = coeffFor(kAttackMs, sr);
  releaseCoeff_ = coeffFor(kReleaseMs, sr);
  reset();
}

void BreathReducer::reset() {
  gainDb_ = 0.0;
  gainLin_ = 1.0f;
}

void BreathReducer::setAmount(float amount01) {
  amount_ = std::isfinite(amount01) ? std::clamp(amount01, 0.0f, 1.0f) : 0.0f;
}

float BreathReducer::process(float breathProb) {
  const double p = std::isfinite(breathProb) ? std::clamp(breathProb, 0.0f, 1.0f) : 0.0f;
  const double targetDb = -p * static_cast<double>(amount_) * kMaxReductionDb;
  if (gainDb_ == 0.0 && targetDb == 0.0) return gainLin_;

  const double c = targetDb < gainDb_ ? attackCoeff_ : releaseCoeff_;
  gainDb_ = targetDb + c * (gainDb_ - targetDb);
  gainDb_ = std::clamp(gainDb_, -kMaxReductionDb, 0.0);
  if (targetDb == 0.0 && gainDb_ > -kDbFloor) gainDb_ = 0.0;  // settle, avoid endless tail
  gainLin_ = gainDb_ == 0.0 ? 1.0f
                             : static_cast<float>(std::exp(gainDb_ * 0.11512925464970229));
  return gainLin_;
}

} // namespace FairyTapes::DeepThroat::DSP
