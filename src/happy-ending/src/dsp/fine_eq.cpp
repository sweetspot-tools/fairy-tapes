#include "dsp/fine_eq.h"
#include "dsp/ms.h"
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

int clampRoute(int ms) { return std::clamp(ms, 0, 2); }

// One direct form II transposed step; `z` is the two-value memory of the section.
inline double tick(const BiquadCoeffs& c, double* z, double x) {
  const double y = c.b0 * x + z[0];
  z[0] = c.b1 * x - c.a1 * y + z[1];
  z[1] = c.b2 * x - c.a2 * y;
  return y;
}

// One band on one route for one sample pair: ST filters L and R, MID / SIDE filter that component
// and leave the other one alone, scaled by `other` (1 for a band; the LISTEN band-pass fades it to 0).
inline void runPath(const BiquadCoeffs& c, int route, double* zl, double* zr, double other, double inL, double inR,
                    double& outL, double& outR) {
  if (route == 0) {
    outL = tick(c, zl, inL);
    outR = tick(c, zr, inR);
    return;
  }
  double m, s;
  msEncode<double>(inL, inR, m, s);
  if (route == 1) {
    m = tick(c, zl, m);
    s *= other;
  } else {
    s = tick(c, zl, s);
    m *= other;
  }
  msDecode<double>(m, s, outL, outR);
}

} // namespace

double effectiveQ(double q, double gainDb) {
  return q * (1.0 + std::fabs(gainDb) / Tuning::kFineQCouplingDb);
}

void FineEq::prepare(double sampleRate, int /*maxBlock*/) {
  sampleRate_ = sampleRate;
  for (int slot = 0; slot <= kListenSlot; ++slot) {
    Band& band = bands_[static_cast<std::size_t>(slot)];
    band.zl[0] = band.zl[1] = band.zr[0] = band.zr[1] = 0.0;
    updateTarget(slot);
    band.current = band.target;  // no glide from a previous configuration
    band.rampLeft = 0;
    band.fadeLeft = 0;
    band.soloLevel = band.soloTarget;
    band.soloLeft = 0;
  }
}

// Nothing to do for this slot: identity, memory flushed, no fade or mute in progress. It then costs
// nothing and passes the signal bit-exact.
bool FineEq::atRest(const Band& band) {
  return band.rampLeft == 0 && band.fadeLeft == 0 && band.soloLevel == 0.0 && band.soloTarget == 0.0 &&
         band.current.isIdentity() && band.zl[0] == 0.0 && band.zl[1] == 0.0 && band.zr[0] == 0.0 && band.zr[1] == 0.0;
}

void FineEq::setBand(int band, const BandSettings& settings) {
  if (band < 0 || band >= kNumBands) return;
  Band& b = bands_[static_cast<std::size_t>(band)];
  if (b.settings == settings) return;  // nothing changed: leave the glide alone
  b.settings = settings;
  updateTarget(band);
  if (listen_ >= 0 && band == kLm + listen_) updateTarget(kListenSlot);
}

void FineEq::setStageOn(bool on) {
  if (stageOn_ == on) return;
  stageOn_ = on;
  for (int slot = 0; slot <= kListenSlot; ++slot) updateTarget(slot);
}

void FineEq::setListen(int bellOrMinus1) {
  const int bell = (bellOrMinus1 >= 0 && bellOrMinus1 <= 2) ? bellOrMinus1 : -1;
  if (listen_ == bell) return;
  listen_ = bell;
  for (int slot = 0; slot <= kListenSlot; ++slot) updateTarget(slot);
}

// Response of the settings of one band alone: identity when the stage or the band is off, at
// 0 dB, and for HI PASS Off. HI PASS on SIDE reads the mono-bass scale at the same index.
BiquadCoeffs FineEq::designFor(int index) const {
  const BandSettings& s = bands_[static_cast<std::size_t>(index)].settings;
  BiquadCoeffs t;
  if (!stageOn_ || !s.on) return t;
  if (index == kHp) {
    const double freq = hpFreqHz(clampRoute(s.ms) == 2, std::clamp(s.freqIndex, 0, static_cast<int>(Tables::kHpFreqs.size()) - 1));
    if (freq > 0.0) matched::computeHighPass(t, freq, sampleRate_, Tuning::kFineHighPassQ);
    return t;
  }
  const double freq = tableValue(kFreqParam[index], s.freqIndex);
  const double gain = tableValue(gainParam(index), s.gainIndex);
  if (index == kLs) matched::computeLowShelf(t, freq, sampleRate_, gain, Tuning::kFineShelfQ);
  else if (index == kHs) matched::computeHighShelfMatched(t, freq, sampleRate_, gain, Tuning::kFineShelfQ);
  else matched::computePeakingMatched(t, freq, sampleRate_, gain, effectiveQ(tableValue(qParam(index), s.qIndex), gain));
  return t;
}

// Recomputes the target of one slot (a band, or the LISTEN band-pass) and starts the glide from
// the coefficients now in use. A change of route starts a crossfade from the old route (frozen
// coefficients, its own memory) to the new one, which starts with a cleared memory: that one holds
// another signal. A slot at rest has nothing to fade from and switches at once.
void FineEq::updateTarget(int slot) {
  Band& band = bands_[static_cast<std::size_t>(slot)];
  BiquadCoeffs t;  // identity
  int route = band.route;
  double soloTarget = 0.0;
  if (slot < kNumBands) {
    band.design = designFor(slot);
    route = clampRoute(band.settings.ms);
    if (listen_ < 0) t = band.design;  // while LISTEN is on the six bands do not sound
  } else if (listen_ >= 0) {
    const int bell = kLm + listen_;
    const BandSettings& s = bands_[static_cast<std::size_t>(bell)].settings;
    route = clampRoute(s.ms);
    if (stageOn_) {
      const double gain = tableValue(gainParam(bell), s.gainIndex);
      matched::computeBandPass(t, tableValue(kFreqParam[bell], s.freqIndex), sampleRate_,
                               effectiveQ(tableValue(qParam(bell), s.qIndex), gain));
      soloTarget = 1.0;
    }
  }
  if (route != band.route) {
    if (atRest(band)) {
      band.fadeLeft = 0;
    } else {
      band.oldRoute = band.route;
      band.oldCoeffs = band.current;
      for (int k = 0; k < 2; ++k) {
        band.oldZl[k] = band.zl[k];
        band.oldZr[k] = band.zr[k];
      }
      band.fadeLeft = Tuning::kFineRampSamples;
    }
    band.route = route;
    band.zl[0] = band.zl[1] = band.zr[0] = band.zr[1] = 0.0;
  }
  band.target = t;
  band.rampLeft = Tuning::kFineRampSamples;
  for (int i = 0; i < BiquadCoeffs::kCount; ++i)
    band.step.at(i) = (t.at(i) - band.current.at(i)) / static_cast<double>(Tuning::kFineRampSamples);
  if (slot == kListenSlot) {
    band.soloTarget = soloTarget;
    band.soloLeft = Tuning::kFineRampSamples;
    band.soloStep = (soloTarget - band.soloLevel) / static_cast<double>(Tuning::kFineRampSamples);
  }
}

void FineEq::process(float* left, float* right, int n) {
  for (int slot = 0; slot <= kListenSlot; ++slot) {
    Band& band = bands_[static_cast<std::size_t>(slot)];
    if (atRest(band)) continue;
    for (int i = 0; i < n; ++i) {
      if (band.rampLeft > 0) {
        if (--band.rampLeft == 0) band.current = band.target;
        else for (int k = 0; k < BiquadCoeffs::kCount; ++k) band.current.at(k) += band.step.at(k);
      }
      if (band.soloLeft > 0) {
        if (--band.soloLeft == 0) band.soloLevel = band.soloTarget;
        else band.soloLevel += band.soloStep;
      }
      const double other = 1.0 - band.soloLevel;
      double l, r;
      runPath(band.current, band.route, band.zl, band.zr, other, left[i], right[i], l, r);
      if (band.fadeLeft > 0) {
        double ol, orr;
        runPath(band.oldCoeffs, band.oldRoute, band.oldZl, band.oldZr, other, left[i], right[i], ol, orr);
        const double a = static_cast<double>(Tuning::kFineRampSamples - band.fadeLeft + 1) / Tuning::kFineRampSamples;
        --band.fadeLeft;
        l = ol + (l - ol) * a;
        r = orr + (r - orr) * a;
      }
      left[i] = static_cast<float>(l);
      right[i] = static_cast<float>(r);
    }
  }
}

double FineEq::magnitudeDb(int target, double hz) const {
  if (target < 0 || target > 2) return 0.0;
  double db = 0.0;
  for (int b = 0; b < kNumBands; ++b) {
    const Band& band = bands_[static_cast<std::size_t>(b)];
    const int route = clampRoute(band.settings.ms);
    if (route == 0 || route == target) db += band.design.magnitudeDb(hz, sampleRate_);
  }
  return db;
}

} // namespace FairyTapes::HappyEnding::DSP
