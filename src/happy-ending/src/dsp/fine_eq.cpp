#include "dsp/fine_eq.h"
#include "params.h"
#include "tuning.h"
#include <algorithm>
#include <cmath>

namespace FairyTapes::HappyEnding::DSP {

namespace {

enum Band { kHp = 0, kLs, kLm, kMid, kHm, kHs };

constexpr ParamID kFreqParam[FineEq::kNumBands] = {kHpFreq, kLsFreq, kLmFreq, kMidFreq, kHmFreq, kHsFreq};

// Gain and Q parameters of the bands that have them (the HI PASS has neither).
ParamID gainParam(int band) {
  switch (band) {
    case kLs: return kLsGain;
    case kLm: return kLmGain;
    case kMid: return kMidGain;
    case kHm: return kHmGain;
    default: return kHsGain;
  }
}

ParamID qParam(int bell) { return bell == kLm ? kLmQ : (bell == kMid ? kMidQ : kHmQ); }

double tableValue(ParamID id, int index) {
  const ValueTable t = valueTable(id);
  const int last = static_cast<int>(t.size) - 1;
  return t.data[std::clamp(index, 0, last)];
}

} // namespace

double effectiveQ(double q, double gainDb) {
  return q * (1.0 + std::fabs(gainDb) / Tuning::kFineQCouplingDb);
}

void FineEq::prepare(double sampleRate, int /*maxBlock*/) {
  sampleRate_ = sampleRate;
  for (int b = 0; b < kNumBands; ++b) {
    Band& band = bands_[static_cast<std::size_t>(b)];
    band.zl[0] = band.zl[1] = band.zr[0] = band.zr[1] = 0.0;
    updateTarget(b);
    band.current = band.target;  // no glide from a previous configuration
    band.rampLeft = 0;
  }
}

void FineEq::setBand(int band, const BandSettings& settings) {
  if (band < 0 || band >= kNumBands) return;
  bands_[static_cast<std::size_t>(band)].settings = settings;
  updateTarget(band);
}

void FineEq::setStageOn(bool on) {
  if (stageOn_ == on) return;
  stageOn_ = on;
  for (int b = 0; b < kNumBands; ++b) updateTarget(b);
}

// Recomputes the target of one band and starts the glide from the coefficients now in use.
void FineEq::updateTarget(int index) {
  Band& band = bands_[static_cast<std::size_t>(index)];
  const BandSettings& s = band.settings;
  BiquadCoeffs t;  // identity: stage off, band off, 0 dB and HI PASS Off all land here
  if (stageOn_ && s.on) {
    const double freq = tableValue(kFreqParam[index], s.freqIndex);
    if (index == kHp) {
      if (freq > 0.0) matched::computeHighPass(t, freq, sampleRate_, Tuning::kFineHighPassQ);
    } else {
      const double gain = tableValue(gainParam(index), s.gainIndex);
      if (index == kLs) matched::computeLowShelf(t, freq, sampleRate_, gain, Tuning::kFineShelfQ);
      else if (index == kHs) matched::computeHighShelf(t, freq, sampleRate_, gain, Tuning::kFineShelfQ);
      else matched::computePeaking(t, freq, sampleRate_, gain, effectiveQ(tableValue(qParam(index), s.qIndex), gain));
    }
  }
  band.target = t;
  band.rampLeft = Tuning::kFineRampSamples;
  for (int i = 0; i < BiquadCoeffs::kCount; ++i)
    band.step.at(i) = (t.at(i) - band.current.at(i)) / static_cast<double>(Tuning::kFineRampSamples);
}

void FineEq::process(float* left, float* right, int n) {
  for (Band& band : bands_) {
    // Direct form II transposed. A band at rest on the identity, with its memory flushed, costs
    // nothing and passes the signal bit-exact.
    if (band.rampLeft == 0 && band.current.isIdentity() && band.zl[0] == 0.0 && band.zl[1] == 0.0 &&
        band.zr[0] == 0.0 && band.zr[1] == 0.0)
      continue;
    for (int i = 0; i < n; ++i) {
      if (band.rampLeft > 0) {
        if (--band.rampLeft == 0) band.current = band.target;
        else for (int k = 0; k < BiquadCoeffs::kCount; ++k) band.current.at(k) += band.step.at(k);
      }
      const BiquadCoeffs& c = band.current;
      const double xl = left[i];
      const double yl = c.b0 * xl + band.zl[0];
      band.zl[0] = c.b1 * xl - c.a1 * yl + band.zl[1];
      band.zl[1] = c.b2 * xl - c.a2 * yl;
      left[i] = static_cast<float>(yl);

      const double xr = right[i];
      const double yr = c.b0 * xr + band.zr[0];
      band.zr[0] = c.b1 * xr - c.a1 * yr + band.zr[1];
      band.zr[1] = c.b2 * xr - c.a2 * yr;
      right[i] = static_cast<float>(yr);
    }
  }
}

double FineEq::magnitudeDb(int target, double hz) const {
  if (target != 0) return 0.0;
  double db = 0.0;
  for (const Band& band : bands_) db += band.target.magnitudeDb(hz, sampleRate_);
  return db;
}

} // namespace FairyTapes::HappyEnding::DSP
