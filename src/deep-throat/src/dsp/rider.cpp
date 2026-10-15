#include "dsp/rider.h"

#include <algorithm>
#include <cmath>

namespace FairyTapes::DeepThroat::DSP {

namespace {
constexpr float kSilenceDb = -60.0f;
constexpr double kMeterFloorDb = -120.0;
constexpr double kMeanSqFloor = 1e-25;  // far below -120 dB; flushes denormals to zero
}

void LevelMeter::prepare(double sampleRate, float windowMs) {
  const double sr = sampleRate > 1.0 ? sampleRate : 48000.0;
  const double samples = std::max(1.0, static_cast<double>(windowMs) * 0.001 * sr);
  coeff_ = std::exp(-1.0 / samples);
  meanSq_ = 0.0;
}

void LevelMeter::reset() { meanSq_ = 0.0; }

float LevelMeter::process(float monoSample) {
  const double x = std::isfinite(monoSample) ? static_cast<double>(monoSample) : 0.0;
  meanSq_ = coeff_ * meanSq_ + (1.0 - coeff_) * x * x;
  if (meanSq_ < kMeanSqFloor) meanSq_ = 0.0;
  if (meanSq_ <= 0.0) return static_cast<float>(kMeterFloorDb);
  return static_cast<float>(std::max(kMeterFloorDb, 10.0 * std::log10(meanSq_)));
}

void Rider::prepare(double sampleRate) {
  sampleRate_ = sampleRate > 1.0 ? sampleRate : 48000.0;
  reset();
}

void Rider::reset() {
  gainDb_ = 0.0;
  gainLin_ = 1.0f;
}

float Rider::processFrame(float levelDb, bool freeze) {
  if (freeze || !std::isfinite(levelDb) || levelDb < kSilenceDb) return gainLin_;

  const double range = rangeDb_;
  const double error = std::clamp(static_cast<double>(targetDb_) - levelDb, -range, range);
  const double maxStep = static_cast<double>(slewDbPerSec_) / sampleRate_;
  const double delta = std::clamp(error - gainDb_, -maxStep, maxStep);
  if (delta != 0.0) {
    gainDb_ += delta;
    gainLin_ = static_cast<float>(std::exp(gainDb_ * 0.11512925464970229));  // ln(10)/20
  }
  return gainLin_;
}

} // namespace FairyTapes::DeepThroat::DSP
