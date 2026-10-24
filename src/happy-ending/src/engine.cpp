#include "engine.h"
#include "loudness_macro.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace FairyTapes::HappyEnding {

namespace {

// Value of a parameter from its table (index clamped), as float.
float tableAt(ParamID id, int index) {
  const ValueTable t = valueTable(id);
  const int last = static_cast<int>(t.size) - 1;
  return static_cast<float>(t.data[std::clamp(index, 0, last)]);
}

double dbToGain(double db) { return std::pow(10.0, db / 20.0); }

float peakDb(double peak) {
  if (!(peak > 0.0)) return static_cast<float>(kPeakFloorDb);
  return static_cast<float>(std::max(kPeakFloorDb, 20.0 * std::log10(peak)));
}

// The ceiling as the largest float not above it: a float at or below it is at or below the exact ceiling.
float ceilingFloat(float ceilingDb) {
  const double c = std::pow(10.0, static_cast<double>(std::min(ceilingDb, 0.0f)) / 20.0);
  float cf = static_cast<float>(c);
  if (static_cast<double>(cf) > c) cf = std::nextafter(cf, 0.0f);
  return cf;
}

double blockPeak(const float* x, int n) {
  float m = 0.0f;
  for (int i = 0; i < n; ++i) m = std::max(m, std::fabs(x[i]));
  return m;
}

} // namespace

EngineSettings mapParams(const double* norm, int listenBand, bool setActive) {
  const auto idx = [norm](ParamID id) { return indexFromNorm(id, norm[id]); };
  const auto on = [&](ParamID id) { return idx(id) != 0; };
  const auto val = [&](ParamID id) { return tableAt(id, idx(id)); };

  EngineSettings s;
  s.bypass = on(kBypass);
  s.inputGainDb = val(kInputGain);
  s.outputGainDb = val(kOutputGain);
  s.ceilingDb = val(kCeiling);
  s.loudness = val(kLoudness);
  s.targetLufs = val(kTarget);
  static constexpr int kFactors[3] = {1, 2, 4};
  s.oversampling = kFactors[std::clamp(idx(kOversampling), 0, 2)];

  // FINE EQ (positions, as FineEq takes them)
  s.fineOn = on(kFineOn);
  struct BandIds {
    ParamID on, ms, freq, gain, q;
    bool hasGain, hasQ;
  };
  static constexpr BandIds kBands[DSP::FineEq::kNumBands] = {
      {kHpOn, kHpMs, kHpFreq, kHpFreq, kHpFreq, false, false},
      {kLsOn, kLsMs, kLsFreq, kLsGain, kLsGain, true, false},
      {kLmOn, kLmMs, kLmFreq, kLmGain, kLmQ, true, true},
      {kMidOn, kMidMs, kMidFreq, kMidGain, kMidQ, true, true},
      {kHmOn, kHmMs, kHmFreq, kHmGain, kHmQ, true, true},
      {kHsOn, kHsMs, kHsFreq, kHsGain, kHsGain, true, false},
  };
  for (int b = 0; b < DSP::FineEq::kNumBands; ++b) {
    const BandIds& ids = kBands[b];
    DSP::BandSettings& band = s.bands[static_cast<std::size_t>(b)];
    band.on = on(ids.on);
    band.ms = idx(ids.ms);
    band.freqIndex = idx(ids.freq);
    if (ids.hasGain) band.gainIndex = idx(ids.gain);
    if (ids.hasQ) band.qIndex = idx(ids.q);
  }
  // LISTEN is locked (off) while its bell or the stage is off
  s.listenBand = (listenBand >= 0 && listenBand <= 2 && s.fineOn && s.bands[static_cast<std::size_t>(2 + listenBand)].on)
                     ? listenBand
                     : -1;

  // VIBE EQ (positions; LOW / AIR / TUBE are the 0-10 integers)
  s.vibe.on = on(kVibeOn);
  s.vibe.lowFreqIndex = idx(kLowFreq);
  s.vibe.low = static_cast<int>(val(kLow));
  s.vibe.airFreqIndex = idx(kAirFreq);
  s.vibe.air = static_cast<int>(val(kAir));
  s.vibe.tiltIndex = idx(kTilt);
  s.vibe.tube = static_cast<int>(val(kTube));
  s.vibe.dynLow = on(kDynLow);
  s.vibe.dynAir = on(kDynAir);

  // GLUE (units)
  s.glue.on = on(kGlueOn);
  s.glue.threshDb = val(kGlueThresh);
  s.glue.attackMs = val(kGlueAttack);
  s.glue.releaseS = val(kGlueRelease);
  s.glue.ratio = val(kGlueRatio);
  s.glue.makeupDb = val(kGlueMakeup);
  s.glue.mix = val(kGlueMix) / 100.0f;
  s.glue.scHpfHz = val(kGlueScHpf);
  s.glue.warm = on(kGlueWarm);

  s.clipOn = on(kClipOn);
  s.clipDriveDb = val(kClipDrive);

  s.limitOn = on(kLimitOn);
  s.limitThreshDb = val(kLimitThresh);
  s.limitStyle = idx(kLimitStyle);
  s.limitReleaseIndex = idx(kLimitRelease);
  s.setActive = setActive;

  s.finalOn = on(kFinalOn);
  s.finalMode = idx(kFinalMode);
  s.finalEffect = val(kFinalEffect) / 100.0f;
  s.finalCurve = val(kFinalCurve) / 100.0f;
  s.finalDriveDb = val(kFinalDrive);

  s.metalOn = on(kMetalOn);
  s.pushDb = val(kPush);
  return s;
}

void Engine::Ramp::to(double v, int samples) {
  if (v == target) return;
  target = v;
  left = std::max(1, samples);
  step = (target - cur) / left;
}

void Engine::prepare(double sampleRate, int maxBlock, int oversampling) {
  sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
  maxBlock_ = std::max(1, maxBlock);
  factor_ = (oversampling == 2 || oversampling == 4) ? oversampling : 1;
  const double osRate = sampleRate_ * factor_;
  const int osBlock = maxBlock_ * factor_;

  fine_.prepare(sampleRate_, maxBlock_);
  vibe_.prepare(sampleRate_, maxBlock_);
  vibe_.prepareTube(osRate);
  glue_.prepare(sampleRate_, maxBlock_);
  clip_.prepare(osRate);
  limiter_.prepare(osRate, osBlock, factor_);
  final_.prepare(osRate, factor_);
  metal_.prepare(osRate, osBlock, factor_);
  loud_.prepare(sampleRate_);
  for (int c = 0; c < 2; ++c) {
    osA_[c].prepare(factor_, maxBlock_);
    osB_[c].prepare(factor_, maxBlock_);
  }

  latency_ = osA_[0].latencySamples() + osB_[0].latencySamples() +
             (limiter_.latencySamples() + final_.latencySamples() + metal_.latencySamples()) / factor_;

  const auto ring = static_cast<std::size_t>(std::max(1, latency_));
  bypassRingL_.assign(ring, 0.0f);
  bypassRingR_.assign(ring, 0.0f);
  bypassPos_ = 0;
  delayedL_.assign(static_cast<std::size_t>(maxBlock_), 0.0f);
  delayedR_.assign(static_cast<std::size_t>(maxBlock_), 0.0f);
  osL_.assign(static_cast<std::size_t>(osBlock), 0.0f);
  osR_.assign(static_cast<std::size_t>(osBlock), 0.0f);
  dryL_.assign(static_cast<std::size_t>(osBlock), 0.0f);
  dryR_.assign(static_cast<std::size_t>(osBlock), 0.0f);

  clipMixStep_ = 1.0 / std::max(1.0, Tuning::kClipRampMs * 0.001 * osRate);
  clipHoldSamples_ = static_cast<int>(std::lround(kClipHoldSeconds * sampleRate_));
  clipHold_.fill(0);
  meters_ = Meters{};
  prepared_ = true;
  forceNext_ = true;
  if (hasSettings_) apply(applied_, true);
}

void Engine::set(const EngineSettings& settings) {
  hasSettings_ = true;
  if (!prepared_) {
    applied_ = settings;
    return;
  }
  apply(settings, forceNext_);
}

// Hands each block its settings (only on a change, or everything when `force`: the first set after
// prepare(), which also snaps the engine's own glides).
void Engine::apply(const EngineSettings& s, bool force) {
  const LoudPush push = loudPush(s.loudness);

  // FineEq and VibeEq compare with their own state and return early when nothing changed
  fine_.setStageOn(s.fineOn);
  for (int b = 0; b < DSP::FineEq::kNumBands; ++b) fine_.setBand(b, s.bands[static_cast<std::size_t>(b)]);
  fine_.setListen(s.listenBand);
  vibe_.set(s.vibe);

  if (force || !(s.glue == applied_.glue)) glue_.set(s.glue);

  // ClipSoftly restarts its ramp on every call: only on a change
  const float clipDb = s.clipDriveDb + push.clipDb;
  if (force || clipDb != clipDriveApplied_) {
    clip_.setDriveDb(clipDb);
    clipDriveApplied_ = clipDb;
  }

  DSP::LimiterSettings ls;
  ls.on = s.limitOn;
  ls.threshDb = s.limitThreshDb - push.limitDb;
  ls.style = s.limitStyle;
  ls.releaseTrimIndex = s.limitReleaseIndex;
  ls.ceilingDb = s.ceilingDb;
  ls.set = s.setActive;
  if (force || !(ls == limitApplied_)) {
    limiter_.set(ls);
    limitApplied_ = ls;
  }

  const FinalArgs fa{s.finalOn,
                     s.finalMode,
                     std::min(1.0f, s.finalEffect + 0.6f * push.final01),
                     s.finalCurve,
                     std::min(2.0f, s.finalDriveDb + 2.0f * push.final01),
                     s.ceilingDb};
  if (force || !(fa == finalApplied_)) {
    final_.set(fa.on, fa.mode, fa.effect, fa.curve, fa.driveDb, fa.ceilingDb);
    finalApplied_ = fa;
  }

  const MetalArgs ma{s.metalOn, s.pushDb, s.ceilingDb};
  if (force || !(ma == metalApplied_)) {
    metal_.set(ma.on, ma.pushDb, ma.ceilingDb);
    metalApplied_ = ma;
  }

  ceilLin_ = ceilingFloat(s.ceilingDb);
  guardOn_ = s.limitOn || s.finalOn || s.metalOn;
  clipMixTarget_ = s.clipOn ? 1.0 : 0.0;

  const double inG = dbToGain(s.inputGainDb), outG = dbToGain(s.outputGainDb), byp = s.bypass ? 1.0 : 0.0;
  if (force) {
    inGain_.snap(inG);
    outGain_.snap(outG);
    bypassMix_.snap(byp);
    clipMix_ = clipMixTarget_;
  } else {
    inGain_.to(inG, kGainRampSamples);
    outGain_.to(outG, kGainRampSamples);
    bypassMix_.to(byp, kGainRampSamples);
  }

  applied_ = s;
  forceNext_ = false;
}

void Engine::clearClips() {
  clipHold_.fill(0);
  for (bool& c : meters_.clip) c = false;
}

void Engine::resetIntegrated() {
  loud_.resetIntegrated();
  meters_.lufsIntegrated = loud_.integratedLufs();
}

void Engine::markClip(int led, const float* l, const float* r, int n, bool* hit) const {
  if (hit[led]) return;
  for (int i = 0; i < n; ++i)
    if (std::fabs(l[i]) >= 1.0f || std::fabs(r[i]) >= 1.0f) {
      hit[led] = true;
      return;
    }
}

void Engine::process(float* left, float* right, int n) {
  if (!prepared_ || n <= 0) return;
  ScopedNoDenormals noDenormals;
  double inPeak[2] = {0.0, 0.0}, outPeak[2] = {0.0, 0.0};
  for (int pos = 0; pos < n; pos += maxBlock_) {
    const int len = std::min(maxBlock_, n - pos);
    processChunk(left + pos, right + pos, len, inPeak, outPeak);
  }
  for (int c = 0; c < 2; ++c) {
    meters_.inPeakDb[c] = peakDb(inPeak[c]);
    meters_.outPeakDb[c] = peakDb(outPeak[c]);
  }
  meters_.lufsShort = loud_.shortTermLufs();
  meters_.lufsIntegrated = loud_.integratedLufs();
  meters_.dynLow = vibe_.dynLowAmount();
  meters_.dynAir = vibe_.dynAirAmount();
}

void Engine::processChunk(float* l, float* r, int n, double* inPeak, double* outPeak) {
  bool hit[kNumLeds] = {};

  // entry: non-finite samples become 0; the bypass path is this input delayed by the latency
  const int ring = static_cast<int>(bypassRingL_.size());
  for (int i = 0; i < n; ++i) {
    if (!std::isfinite(l[i])) l[i] = 0.0f;
    if (!std::isfinite(r[i])) r[i] = 0.0f;
    const auto k = static_cast<std::size_t>(i);
    if (latency_ > 0) {
      const auto p = static_cast<std::size_t>(bypassPos_);
      delayedL_[k] = bypassRingL_[p];
      delayedR_[k] = bypassRingR_[p];
      bypassRingL_[p] = l[i];
      bypassRingR_[p] = r[i];
      bypassPos_ = bypassPos_ + 1 == ring ? 0 : bypassPos_ + 1;
    } else {
      delayedL_[k] = l[i];
      delayedR_[k] = r[i];
    }
  }

  // INPUT
  for (int i = 0; i < n; ++i) {
    const auto g = static_cast<float>(inGain_.next());
    l[i] *= g;
    r[i] *= g;
  }
  inPeak[0] = std::max(inPeak[0], blockPeak(l, n));
  inPeak[1] = std::max(inPeak[1], blockPeak(r, n));
  markClip(kLedIn, l, r, n, hit);

  // FINE EQ
  fine_.process(l, r, n);
  markClip(kLedFeq, l, r, n, hit);

  // VIBE EQ: the network at the base rate, the tube in zone A
  vibe_.processLinear(l, r, n);
  const int nOs = n * factor_;
  if (factor_ > 1) {
    osA_[0].up(l, n, osL_.data());
    osA_[1].up(r, n, osR_.data());
    vibe_.processTube(osL_.data(), osR_.data(), nOs);
    osA_[0].down(osL_.data(), nOs, l);
    osA_[1].down(osR_.data(), nOs, r);
  } else {
    vibe_.processTube(l, r, n);
  }
  markClip(kLedVeq, l, r, n, hit);

  // GLUE
  glue_.process(l, r, n);
  markClip(kLedGlu, l, r, n, hit);

  // zone B: CLIP -> LIMIT -> FINAL -> METALHEAD
  float* zl = l;
  float* zr = r;
  if (factor_ > 1) {
    osB_[0].up(l, n, osL_.data());
    osB_[1].up(r, n, osR_.data());
    zl = osL_.data();
    zr = osR_.data();
  }

  // CLIP has no off state of its own: the engine crossfades it with the dry signal (zero latency)
  const bool clipRan = clipMix_ > 0.0 || clipMixTarget_ > 0.0;
  if (clipRan) {
    if (clipMix_ >= 1.0 && clipMixTarget_ >= 1.0) {
      clip_.process(zl, zr, nOs);
    } else {
      std::memcpy(dryL_.data(), zl, sizeof(float) * static_cast<std::size_t>(nOs));
      std::memcpy(dryR_.data(), zr, sizeof(float) * static_cast<std::size_t>(nOs));
      clip_.process(zl, zr, nOs);
      for (int i = 0; i < nOs; ++i) {
        if (clipMix_ != clipMixTarget_) {
          clipMix_ += clipMixTarget_ > clipMix_ ? clipMixStep_ : -clipMixStep_;
          if (clipMix_ >= 1.0 || clipMix_ <= 0.0) clipMix_ = clipMixTarget_;
        }
        const auto w = static_cast<float>(clipMix_);
        const auto k = static_cast<std::size_t>(i);
        zl[i] = dryL_[k] + (zl[i] - dryL_[k]) * w;
        zr[i] = dryR_[k] + (zr[i] - dryR_[k]) * w;
      }
    }
  }
  markClip(kLedClp, zl, zr, nOs, hit);

  limiter_.process(zl, zr, nOs);
  markClip(kLedLim, zl, zr, nOs, hit);
  final_.process(zl, zr, nOs);
  markClip(kLedFin, zl, zr, nOs, hit);
  metal_.process(zl, zr, nOs);
  markClip(kLedMtl, zl, zr, nOs, hit);

  if (factor_ > 1) {
    osB_[0].down(osL_.data(), nOs, l);
    osB_[1].down(osR_.data(), nOs, r);
  }

  // ceiling guard (the decimation can rebuild samples a little above what the stages held)
  if (guardOn_) {
    const float c = ceilLin_;
    for (int i = 0; i < n; ++i) {
      l[i] = std::clamp(l[i], -c, c);
      r[i] = std::clamp(r[i], -c, c);
    }
  }

  // OUTPUT, then BYPASS
  for (int i = 0; i < n; ++i) {
    const auto g = static_cast<float>(outGain_.next());
    l[i] *= g;
    r[i] *= g;
  }
  if (bypassMix_.settled() && bypassMix_.cur >= 1.0) {
    std::memcpy(l, delayedL_.data(), sizeof(float) * static_cast<std::size_t>(n));
    std::memcpy(r, delayedR_.data(), sizeof(float) * static_cast<std::size_t>(n));
  } else if (!(bypassMix_.settled() && bypassMix_.cur <= 0.0)) {
    for (int i = 0; i < n; ++i) {
      const auto w = static_cast<float>(bypassMix_.next());
      const auto k = static_cast<std::size_t>(i);
      l[i] += (delayedL_[k] - l[i]) * w;
      r[i] += (delayedR_[k] - r[i]) * w;
    }
  }
  outPeak[0] = std::max(outPeak[0], blockPeak(l, n));
  outPeak[1] = std::max(outPeak[1], blockPeak(r, n));
  markClip(kLedOut, l, r, n, hit);

  loud_.process(l, r, n);

  // LEDs held for kClipHoldSeconds of audio time
  for (int k = 0; k < kNumLeds; ++k) {
    int& hold = clipHold_[static_cast<std::size_t>(k)];
    hold = hit[k] ? clipHoldSamples_ : std::max(0, hold - n);
    meters_.clip[k] = hold > 0;
  }

  // GR rows (0 while the stage is off)
  meters_.grDb[kGrGlue] = glue_.netGrDb();
  meters_.grDb[kGrClip] = clipRan ? clip_.shavedDb() : 0.0f;
  meters_.grDb[kGrLimit] = applied_.limitOn ? limiter_.grDb() : 0.0f;
  meters_.grDb[kGrFinal] = applied_.finalOn ? final_.shavedDb() : 0.0f;
  meters_.grDb[kGrMetal] = applied_.metalOn ? metal_.grDb() : 0.0f;
}

} // namespace FairyTapes::HappyEnding
