#include "dsp/loudness_meter.h"
#include <algorithm>
#include <cmath>

namespace FairyTapes::HappyEnding::DSP {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kNegInf = -INFINITY;

// BS.1770 offset: the K-weighted mean square of a full-scale 997 Hz sine pair reads 0 LUFS.
constexpr double kLoudnessOffset = -0.691;
constexpr double kAbsoluteGateLufs = -70.0;
constexpr double kRelativeGateLu = -10.0;

// Analog prototypes of the two stages. The constants below are those of libebur128 (MIT licence,
// Copyright (c) 2011 Jan Kokemüller, https://github.com/jiixyj/libebur128, filter.c), fitted so
// that the bilinear transform at 48 kHz gives the coefficients of the standard; the same
// prototypes are re-warped for every other rate.
constexpr double kShelfHz = 1681.974450955533;
constexpr double kShelfGainDb = 3.999843853973347;
constexpr double kShelfQ = 0.7071752369554196;
constexpr double kShelfBandExponent = 0.4996667741545416;
constexpr double kHighPassHz = 38.13547087602444;
constexpr double kHighPassQ = 0.5003270373238773;

inline double lufsOf(double energy) { return kLoudnessOffset + 10.0 * std::log10(energy); }
inline double energyOf(double lufs) { return std::pow(10.0, (lufs - kLoudnessOffset) / 10.0); }

inline double runBiquad(const KBiquad& c, double& s1, double& s2, double x) {
  const double y = c.b0 * x + s1;
  s1 = c.b1 * x - c.a1 * y + s2;
  s2 = c.b2 * x - c.a2 * y;
  return y;
}
}

KWeighting designKWeighting(double sampleRate) {
  KWeighting k;

  {  // pre-filter: high shelf
    const double kk = std::tan(kPi * kShelfHz / sampleRate);
    const double vh = std::pow(10.0, kShelfGainDb / 20.0);
    const double vb = std::pow(vh, kShelfBandExponent);
    const double a0 = 1.0 + kk / kShelfQ + kk * kk;
    k.shelf.b0 = (vh + vb * kk / kShelfQ + kk * kk) / a0;
    k.shelf.b1 = 2.0 * (kk * kk - vh) / a0;
    k.shelf.b2 = (vh - vb * kk / kShelfQ + kk * kk) / a0;
    k.shelf.a1 = 2.0 * (kk * kk - 1.0) / a0;
    k.shelf.a2 = (1.0 - kk / kShelfQ + kk * kk) / a0;
  }
  {  // RLB: second-order high pass
    const double kk = std::tan(kPi * kHighPassHz / sampleRate);
    const double a0 = 1.0 + kk / kHighPassQ + kk * kk;
    k.highPass.b0 = 1.0;
    k.highPass.b1 = -2.0;
    k.highPass.b2 = 1.0;
    k.highPass.a1 = 2.0 * (kk * kk - 1.0) / a0;
    k.highPass.a2 = (1.0 - kk / kHighPassQ + kk * kk) / a0;
  }
  return k;
}

LoudnessMeter::LoudnessMeter() : shortTerm_(kNegInf), integrated_(kNegInf) {
  k_ = designKWeighting(48000.0);
}

void LoudnessMeter::prepare(double sampleRate) {
  k_ = designKWeighting(sampleRate);
  hop_ = std::max(1, static_cast<int>(std::lround(0.1 * sampleRate)));
  shelfState_ = {};
  hpState_ = {};
  hopPos_ = 0;
  hopEnergy_ = 0.0;
  sub_.fill(0.0);
  subPos_ = 0;
  subCount_ = 0;
  shortTerm_.store(kNegInf, std::memory_order_relaxed);
  resetIntegrated();
}

void LoudnessMeter::resetIntegrated() {
  binEnergy_.fill(0.0);
  binCount_.fill(0);
  // The sub-block in progress started before the reset: it must not reach a gating block.
  sinceReset_ = -1;
  integrated_.store(kNegInf, std::memory_order_relaxed);
}

void LoudnessMeter::process(const float* left, const float* right, int n) {
  for (int i = 0; i < n; ++i) {
    const double in[2] = {static_cast<double>(left[i]), static_cast<double>(right[i])};
    for (int c = 0; c < 2; ++c) {
      const double y1 = runBiquad(k_.shelf, shelfState_[c].s1, shelfState_[c].s2, in[c]);
      const double y2 = runBiquad(k_.highPass, hpState_[c].s1, hpState_[c].s2, y1);
      hopEnergy_ += y2 * y2;
    }
    if (++hopPos_ >= hop_) {
      subBlockDone();
      hopPos_ = 0;
      hopEnergy_ = 0.0;
    }
  }
}

void LoudnessMeter::subBlockDone() {
  sub_[subPos_] = hopEnergy_;
  subPos_ = (subPos_ + 1) % kSubBlocksShortTerm;
  subCount_ = std::min(subCount_ + 1, kSubBlocksShortTerm);
  if (sinceReset_ < kSubBlocksPerBlock) ++sinceReset_;  // saturated: every hop then closes a block

  shortTerm_.store(shortTermValue(), std::memory_order_relaxed);

  if (sinceReset_ >= kSubBlocksPerBlock && subCount_ >= kSubBlocksPerBlock) {
    double sum = 0.0;
    for (int j = 1; j <= kSubBlocksPerBlock; ++j)
      sum += sub_[(subPos_ - j + kSubBlocksShortTerm) % kSubBlocksShortTerm];
    addBlock(sum / (static_cast<double>(kSubBlocksPerBlock) * hop_));
    updateIntegrated();
  }
}

double LoudnessMeter::shortTermValue() const {
  if (subCount_ < kSubBlocksShortTerm) return kNegInf;
  double sum = 0.0;
  for (double e : sub_) sum += e;
  const double energy = sum / (static_cast<double>(kSubBlocksShortTerm) * hop_);
  return energy > 0.0 ? lufsOf(energy) : kNegInf;
}

void LoudnessMeter::addBlock(double energy) {
  if (!(energy > 0.0)) return;
  const double lufs = lufsOf(energy);
  if (lufs <= kAbsoluteGateLufs) return;  // absolute gate
  int bin = static_cast<int>(std::floor((lufs - kHistogramMinLufs) * kBinsPerLu));
  bin = std::clamp(bin, 0, kBins - 1);
  binEnergy_[bin] += energy;
  ++binCount_[bin];
}

void LoudnessMeter::updateIntegrated() {
  // Mean of the blocks that passed the absolute gate, which sets the relative gate.
  double sum = 0.0;
  std::uint64_t count = 0;
  for (int b = 0; b < kBins; ++b) {
    sum += binEnergy_[b];
    count += binCount_[b];
  }
  if (count == 0) {
    integrated_.store(kNegInf, std::memory_order_relaxed);
    return;
  }
  const double relGateLufs = lufsOf(sum / static_cast<double>(count)) + kRelativeGateLu;
  // First bin whose lower edge is at or above the relative gate (blocks in the bin that
  // straddles it are decided by the bin: the gate resolution is 0.1 LU).
  const int first = std::clamp(static_cast<int>(std::ceil((relGateLufs - kHistogramMinLufs) * kBinsPerLu - 1e-9)),
                               0, kBins);
  sum = 0.0;
  count = 0;
  for (int b = first; b < kBins; ++b) {
    sum += binEnergy_[b];
    count += binCount_[b];
  }
  integrated_.store(count ? lufsOf(sum / static_cast<double>(count)) : kNegInf, std::memory_order_relaxed);
}

} // namespace FairyTapes::HappyEnding::DSP
