#include "dsp/deesser.h"

#include <algorithm>
#include <cmath>

namespace FairyTapes::DeepThroat::DSP {

namespace {
constexpr double kQ = 0.70710678118654752;  // two Butterworth stages = Linkwitz-Riley 4th order
constexpr double kLoHz = 4000.0, kHiHz = 10000.0;
constexpr double kBandLo = 0.62, kBandHi = 1.38;
constexpr double kThresholdDb = -30.0;
constexpr double kRatio = 4.0;
constexpr double kMaxReductionDb = 12.0;
constexpr double kPukeAmountScale = 0.15;
constexpr double kAttackMs = 1.0, kReleaseMs = 40.0;
constexpr double kPowerMs = 2.0;   // detector power averaging
constexpr double kFreqMs = 20.0;   // crossover frequency glide

double coeffFor(double ms, double dtSec) { return 1.0 - std::exp(-dtSec / (ms * 0.001)); }
}  // namespace

void DeEsser::prepare(double sampleRate, int channels) {
  sr_ = sampleRate > 0.0 ? sampleRate : 48000.0;
  ch_.assign((size_t)std::max(1, channels), Channel{});
  freqCoeff_ = coeffFor(kFreqMs, refreshInterval_ / sr_);
  powerCoeff_ = coeffFor(kPowerMs, 1.0 / sr_);
  attackCoeff_ = coeffFor(kAttackMs, 1.0 / sr_);
  releaseCoeff_ = coeffFor(kReleaseMs, 1.0 / sr_);
  reset();
}

void DeEsser::reset() {
  power_ = 0.0;
  reductionDb_ = 0.0;
  counter_ = 0;
  hz_ = targetHz_;
  for (auto& c : ch_) { c.lp1.reset(); c.lp2.reset(); }
  dhp1_.reset(); dhp2_.reset(); dlp1_.reset(); dlp2_.reset();
  applyFreq(hz_);
}

void DeEsser::setAmount(float amount01) {
  amount_ = std::isfinite(amount01) ? std::clamp(amount01, 0.0f, 1.0f) : 0.0f;
  effAmount_ = amount_ * (mode_ == Mode::Puke ? kPukeAmountScale : 1.0);
}

void DeEsser::setFreqHz(float hz) {
  targetHz_ = std::isfinite(hz) ? std::clamp((double)hz, kLoHz, kHiHz) : 6500.0;
}

void DeEsser::setMode(Mode mode) {
  mode_ = mode;
  effAmount_ = amount_ * (mode_ == Mode::Puke ? kPukeAmountScale : 1.0);
}

void DeEsser::applyFreq(double hz) {
  // Keep every edge below Nyquist at low sample rates.
  const double maxHz = 0.45 * sr_;
  const double xo = std::min(hz, maxHz);
  const double bandLo = std::min(kBandLo * hz, maxHz), bandHi = std::min(kBandHi * hz, maxHz);
  for (auto& c : ch_) {
    c.lp1.setLowPass(sr_, xo, kQ);
    c.lp2.setLowPass(sr_, xo, kQ);
  }
  dhp1_.setHighPass(sr_, bandLo, kQ);
  dhp2_.setHighPass(sr_, bandLo, kQ);
  dlp1_.setLowPass(sr_, bandHi, kQ);
  dlp2_.setLowPass(sr_, bandHi, kQ);
}

void DeEsser::processFrame(float* samples) {
  const int n = (int)ch_.size();
  if (++counter_ >= refreshInterval_) {
    counter_ = 0;
    if (hz_ != targetHz_) {
      const double lf = std::log(hz_), lt = std::log(targetHz_);
      hz_ = std::fabs(lt - lf) < 1e-4 ? targetHz_ : std::exp(lf + (lt - lf) * freqCoeff_);
      applyFreq(hz_);
    }
  }

  float low[16];
  float high[16];
  double mono = 0.0;
  const int m = std::min(n, 16);
  for (int i = 0; i < m; ++i) {
    float x = samples[i];
    if (!std::isfinite(x)) x = 0.0f;
    samples[i] = x;
    low[i] = ch_[(size_t)i].lp2.process(ch_[(size_t)i].lp1.process(x));
    high[i] = x - low[i];
    mono += x;
  }
  mono /= m;

  // Detector: band-pass of the linked mono signal, short-time power, level in dB.
  const float d = dlp2_.process(dlp1_.process(dhp2_.process(dhp1_.process((float)mono))));
  power_ += ((double)d * d - power_) * powerCoeff_;
  if (power_ < 1e-30) power_ = 0.0;
  const double levelDb = 10.0 * std::log10(power_ + 1e-20);  // RMS level of the band
  const double over = std::max(0.0, levelDb - kThresholdDb);
  const double target = std::min(over * (1.0 - 1.0 / kRatio), kMaxReductionDb * effAmount_);
  const double k = target > reductionDb_ ? attackCoeff_ : releaseCoeff_;
  reductionDb_ += (target - reductionDb_) * k;
  if (reductionDb_ < 1e-4 && target <= 0.0) reductionDb_ = 0.0;
  const double gain = reductionDb_ > 0.0 ? std::pow(10.0, -reductionDb_ / 20.0) : 1.0;

  const float removed = (float)(1.0 - gain);
  for (int i = 0; i < m; ++i) {
    samples[i] = listen_ ? high[i] * removed : samples[i] - high[i] * removed;
  }
}

}  // namespace FairyTapes::DeepThroat::DSP
