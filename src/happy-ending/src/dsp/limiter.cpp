#include "dsp/limiter.h"
#include "tuning.h"
#include <algorithm>
#include <cmath>

namespace FairyTapes::HappyEnding::DSP {

namespace {
constexpr double kMinThreshDb = -24.0;
constexpr double kMaxPeak = 1.0e6;  // keeps a stray huge sample from making the gain NaN

inline double dbOf(double lin) { return 20.0 * std::log10(lin); }
inline double linOf(double db) { return std::pow(10.0, db / 20.0); }
}

void Limiter::prepare(double sampleRate, int /*maxBlock*/, int latencyMultiple, int fixedStyle) {
  sampleRate_ = sampleRate;
  const int mult = std::max(1, latencyMultiple);
  const int latencyStyle = fixedStyle == kLimiterStyleSafety ? kLimiterStyleSafety : 0;  // MODERN for every LIMIT style
  const int base = std::max(1, static_cast<int>(std::ceil(Tuning::kLimitLookAheadMs[latencyStyle] * 0.001 * sampleRate)));
  delay_ = (base + mult - 1) / mult * mult;

  const auto d = static_cast<std::size_t>(delay_);
  delayL_.assign(d, 0.0f);
  delayR_.assign(d, 0.0f);
  delayPos_ = 0;
  dqIndex_.assign(d + 2, 0);
  dqValue_.assign(d + 2, 1.0);
  envHist_.assign(d, 1.0);
  gainRing_.assign(d + 1, 1.0);
  n_ = 0;

  sustainCoef_ = 1.0 - std::exp(-1.0 / std::max(1.0, Tuning::kLimitDynSustainMs * 0.001 * sampleRate));
  sustain_ = 0.0;
  mixStep_ = fixedStyle == kLimiterStyleSafety ? 1.0 / Tuning::kSafetyRampSamples
                                               : 1.0 / std::max(1.0, Tuning::kLimitOnRampMs * 0.001 * sampleRate);
  glidePerSample_ = std::fabs(Tuning::kSetThresholdDb) / std::max(1.0, Tuning::kSetGlideMs * 0.001 * sampleRate);

  primed_ = false;
  s_ = LimiterSettings{};
  if (fixedStyle == kLimiterStyleSafety) s_.style = kLimiterStyleSafety;  // never MODERN, even before the first set()
  attack_ = 1;
  apply(true);
  grDb_ = 0.0f;
}

void Limiter::set(const LimiterSettings& settings) {
  s_ = settings;
  apply(!primed_);
  primed_ = true;
}

void Limiter::apply(bool snap) {
  const int style = std::clamp(s_.style, 0, kLimiterStyleSafety);
  style_ = style;
  const int trim = std::clamp(s_.releaseTrimIndex, 0, 8);

  window_ = std::clamp(static_cast<int>(std::lround(Tuning::kLimitLookAheadMs[style] * 0.001 * sampleRate_)), 1, delay_);
  const int attack = std::clamp(static_cast<int>(std::lround(Tuning::kLimitAttackMs[style] * 0.001 * sampleRate_)), 1, delay_);
  const bool attackChanged = attack != attack_;
  attack_ = attack;
  softKnee_ = style == 2;
  programRelease_ = style == 2;
  kneeFactor_ = softKnee_ ? linOf(-Tuning::kLimitDynKneeDb * 0.5) : 1.0;

  releaseSamples_ = Tuning::kLimitReleaseBaseMs[style] * std::pow(2.0, (trim - 4) / 3.0) * 0.001 * sampleRate_;
  releaseSamples_ = std::max(1.0, releaseSamples_);
  releaseCoef_ = std::exp(-1.0 / releaseSamples_);

  // The ceiling as the largest float not above it: a float output at or below it is at or below
  // the exact ceiling too.
  const double c = linOf(std::min(static_cast<double>(s_.ceilingDb), 0.0));
  float cf = static_cast<float>(c);
  if (static_cast<double>(cf) > c) cf = std::nextafter(cf, 0.0f);
  ceilLin_ = static_cast<double>(cf);

  knobTargetDb_ = std::clamp(static_cast<double>(s_.threshDb), kMinThreshDb, 0.0);
  targetDb_ = s_.set ? std::min(Tuning::kSetThresholdDb, knobTargetDb_) : knobTargetDb_;
  mixTarget_ = s_.on ? 1.0 : 0.0;
  if (snap) {
    effDb_ = targetDb_;
    knobDb_ = knobTargetDb_;
    mix_ = mixTarget_;
  }
  refreshLevels();
  if (snap) resetState();
  else if (attackChanged) recomputeEnvSum();
}

void Limiter::refreshLevels() {
  if (style_ == kLimiterStyleSafety) {
    // SAFETY: the threshold is the ceiling and there is no gain recovery, so nothing below the
    // ceiling is touched and the level is never raised
    threshLin_ = ceilLin_;
    makeupLin_ = 1.0;
    return;
  }
  threshLin_ = linOf(effDb_);
  // gain recovery from THRESH, not from the forced SET threshold; effDb_ never rises above it for
  // long (both glide at the same speed), so the peaks stay at or under the ceiling
  makeupLin_ = ceilLin_ / linOf(knobDb_);
}

void Limiter::resetState() {
  env_ = makeupLin_;
  std::fill(envHist_.begin(), envHist_.end(), env_);
  std::fill(gainRing_.begin(), gainRing_.end(), env_);
  envPos_ = 0;
  dqHead_ = dqCount_ = 0;
  recomputeEnvSum();
  sustain_ = 0.0;
}

// Sum of the last `attack_` envelope values (the ring's write position is envPos_).
void Limiter::recomputeEnvSum() {
  const int size = static_cast<int>(envHist_.size());
  double sum = 0.0;
  for (int j = 1; j <= attack_; ++j) sum += envHist_[static_cast<std::size_t>((envPos_ - j + size) % size)];
  envSum_ = sum;
  sinceRecompute_ = 0;
}

// The total gain the signal asks for at a sample whose louder channel has this magnitude:
// the gain recovery up to the threshold, then whatever brings the sample down to the ceiling.
double Limiter::requiredGain(double peak) const {
  if (peak <= threshLin_ * kneeFactor_) return makeupLin_;
  if (!softKnee_) return makeupLin_ * threshLin_ / peak;
  const double over = dbOf(peak / threshLin_);
  const double half = Tuning::kLimitDynKneeDb * 0.5;
  const double reduction = over >= half ? over : (over + half) * (over + half) / (2.0 * Tuning::kLimitDynKneeDb);
  return makeupLin_ * linOf(-reduction);
}

void Limiter::process(float* left, float* right, int n) {
  const int size = static_cast<int>(envHist_.size());
  const int ringSize = static_cast<int>(gainRing_.size());
  const int dqCap = static_cast<int>(dqIndex_.size());
  const int gainOffset = delay_ - window_;
  double maxRatio = 1.0;

  for (int i = 0; i < n; ++i) {
    // threshold glide (SET and THRESH steps)
    if (effDb_ != targetDb_ || knobDb_ != knobTargetDb_) {
      auto glide = [this](double v, double target) {
        const double diff = target - v;
        return std::fabs(diff) <= glidePerSample_ ? target : v + std::copysign(glidePerSample_, diff);
      };
      effDb_ = glide(effDb_, targetDb_);
      knobDb_ = glide(knobDb_, knobTargetDb_);
      refreshLevels();
    }

    const double inL = std::isfinite(left[i]) ? static_cast<double>(left[i]) : 0.0;
    const double inR = std::isfinite(right[i]) ? static_cast<double>(right[i]) : 0.0;
    const double peak = std::min(std::max(std::fabs(inL), std::fabs(inR)), kMaxPeak);
    const double req = requiredGain(peak);

    // sliding minimum of the request over the look-ahead window (index n_ - window_ .. n_)
    while (dqCount_ > 0 && dqValue_[static_cast<std::size_t>((dqHead_ + dqCount_ - 1) % dqCap)] >= req) --dqCount_;
    {
      const auto slot = static_cast<std::size_t>((dqHead_ + dqCount_) % dqCap);
      dqIndex_[slot] = n_;
      dqValue_[slot] = req;
      ++dqCount_;
    }
    while (dqIndex_[static_cast<std::size_t>(dqHead_)] < n_ - window_) {
      dqHead_ = (dqHead_ + 1) % dqCap;
      --dqCount_;
    }
    const double gmin = dqValue_[static_cast<std::size_t>(dqHead_)];

    // instant attack, exponential release (time constant follows the program in DYN)
    double coef = releaseCoef_;
    if (programRelease_) {
      const double factor = Tuning::kLimitDynReleaseFast + (Tuning::kLimitDynReleaseSlow - Tuning::kLimitDynReleaseFast) * sustain_;
      coef = std::exp(-1.0 / (releaseSamples_ * factor));
    }
    env_ = gmin < env_ ? gmin : gmin + (env_ - gmin) * coef;

    // moving average over the attack ramp, then the delay that lines the gain up with the signal
    const auto leaving = static_cast<std::size_t>((envPos_ + size - attack_) % size);
    envSum_ += env_ - envHist_[leaving];
    envHist_[static_cast<std::size_t>(envPos_)] = env_;
    envPos_ = (envPos_ + 1) % size;
    const double gain = std::max(envSum_ / attack_, 0.0);
    gainRing_[static_cast<std::size_t>(n_ % ringSize)] = gain;
    const double gDel = gainRing_[static_cast<std::size_t>(((n_ - gainOffset) % ringSize + ringSize) % ringSize)];
    ++n_;

    const double ratio = makeupLin_ / std::max(gDel, 1e-9);
    maxRatio = std::max(maxRatio, ratio);
    if (programRelease_) {
      const double grNorm = std::clamp(dbOf(std::max(ratio, 1.0)) / Tuning::kLimitDynGrRefDb, 0.0, 1.0);
      sustain_ += (grNorm - sustain_) * sustainCoef_;
    }

    // the delayed signal
    const auto pos = static_cast<std::size_t>(delayPos_);
    const float dryL = delayL_[pos], dryR = delayR_[pos];
    delayL_[pos] = static_cast<float>(inL);
    delayR_[pos] = static_cast<float>(inR);
    delayPos_ = (delayPos_ + 1) % delay_;

    float outL, outR;
    if (mix_ <= 0.0) {
      outL = dryL;
      outR = dryR;
    } else {
      double wl = dryL * gDel, wr = dryR * gDel;
      const double wp = std::max(std::fabs(wl), std::fabs(wr));
      if (wp > ceilLin_) {  // sample-peak guard, linked
        const double g = ceilLin_ / wp;
        wl *= g;
        wr *= g;
      }
      if (mix_ >= 1.0) {
        outL = static_cast<float>(wl);
        outR = static_cast<float>(wr);
      } else {
        const double ml = dryL + (wl - dryL) * mix_, mr = dryR + (wr - dryR) * mix_;
        const double mp = std::max(std::fabs(ml), std::fabs(mr));
        const double g = mp > ceilLin_ ? ceilLin_ / mp : 1.0;
        outL = static_cast<float>(ml * g);
        outR = static_cast<float>(mr * g);
      }
      const auto cf = static_cast<float>(ceilLin_);
      outL = std::clamp(outL, -cf, cf);
      outR = std::clamp(outR, -cf, cf);
    }
    left[i] = outL;
    right[i] = outR;

    if (mix_ != mixTarget_) {
      mix_ += mixTarget_ > mix_ ? mixStep_ : -mixStep_;
      if ((mixTarget_ > 0.5 && mix_ >= 1.0) || (mixTarget_ < 0.5 && mix_ <= 0.0)) mix_ = mixTarget_;
    }
  }

  if (n > 0) {
    grDb_ = static_cast<float>(dbOf(maxRatio));
    sinceRecompute_ += n;
    if (sinceRecompute_ >= 65536) recomputeEnvSum();  // keeps the running sum free of drift
  }
}

} // namespace FairyTapes::HappyEnding::DSP
