#include "engine.h"
#include "mode_table.h"

#include <algorithm>
#include <cmath>

namespace FairyTapes::DeepThroat {

namespace {
constexpr float kMaxInput = 1e6f;      // sanitized input bound (keeps every stage finite)
constexpr float kGainSnap = 1e-6f;

inline float clean(float v) {
  return std::isfinite(v) ? std::clamp(v, -kMaxInput, kMaxInput) : 0.0f;
}

// Linear crossfade a -> b; exact at both ends.
inline float xfade(float a, float b, float t) {
  return t <= 0.0f ? a : (t >= 1.0f ? b : a + t * (b - a));
}

inline float smooth(float v, float target, float coef) {
  v += (target - v) * coef;
  return std::fabs(target - v) < kGainSnap ? target : v;
}

inline float finiteIn(float v, float def, float lo, float hi) {
  return std::isfinite(v) ? std::clamp(v, lo, hi) : def;
}

Settings sanitize(const Settings& s) {
  Settings c = s;
  const int m = static_cast<int>(s.mode);
  c.mode = static_cast<Mode>(m < 0 ? 0 : (m > 2 ? 2 : m));
  c.inputDb = finiteIn(s.inputDb, 0.0f, -12.0f, 12.0f);
  c.outputDb = finiteIn(s.outputDb, 0.0f, -12.0f, 12.0f);
  c.riderTargetDb = finiteIn(s.riderTargetDb, -20.0f, -30.0f, -10.0f);
  c.riderRangeDb = finiteIn(s.riderRangeDb, 3.0f, 0.0f, 6.0f);
  c.breathAmount = finiteIn(s.breathAmount, 0.5f, 0.0f, 1.0f);
  c.deEssAmount = finiteIn(s.deEssAmount, 0.5f, 0.0f, 1.0f);
  c.compAmount = finiteIn(s.compAmount, 0.5f, 0.0f, 1.0f);
  c.tone = finiteIn(s.tone, 0.5f, 0.0f, 1.0f);
  c.breathSens = finiteIn(s.breathSens, 0.5f, 0.0f, 1.0f);
  c.deEssFreqHz = finiteIn(s.deEssFreqHz, 6500.0f, 4000.0f, 10000.0f);
  c.compMix = finiteIn(s.compMix, 1.0f, 0.0f, 1.0f);
  c.eqAmount = finiteIn(s.eqAmount, 0.5f, 0.0f, 1.0f);
  c.compRatioChoice = std::clamp(s.compRatioChoice, 0, kCompRatioCount - 1);
  c.compDistChoice = std::clamp(s.compDistChoice, 0, kCompDistCount - 1);
  c.compHpfChoice = std::clamp(s.compHpfChoice, 0, kCompHpfCount - 1);
  return c;
}
} // namespace

Settings mapParams(const double* raw) {
  double def[kNumParams], v[kNumParams];
  loadDefaults(def);
  for (int i = 0; i < (int)kNumParams; ++i)
    v[i] = std::isfinite(raw[i]) ? std::clamp(raw[i], 0.0, 1.0) : def[i];
  Settings s{};
  s.mode = modeFromNorm(v[kMode]);
  s.inputDb = (float)inputDbFromNorm(v[kInput]);
  s.outputDb = (float)outputDbFromNorm(v[kOutput]);
  s.riderTargetDb = (float)riderTargetDbFromNorm(v[kRiderTarget]);
  s.riderRangeDb = (float)(modeRow(s.mode).riderMaxRangeDb * v[kRiderRange]);
  s.breathAmount = (float)v[kBreathAmount];
  s.deEssAmount = (float)v[kDeEssAmount];
  s.compAmount = (float)v[kCompAmount];
  s.tone = (float)v[kTone];
  s.breathSens = (float)v[kBreathSens];
  s.deEssFreqHz = (float)deEssFreqHzFromNorm(v[kDeEssFreq]);
  s.compMix = (float)v[kCompMix];
  s.eqAmount = (float)v[kEqAmount];
  s.compRatioChoice = compRatioChoiceFromNorm(v[kCompRatio]);
  s.compDistChoice = compDistChoiceFromNorm(v[kCompDist]);
  s.compHpfChoice = compHpfChoiceFromNorm(v[kCompHpf]);
  s.breathListen = v[kBreathListen] >= 0.5;
  s.deEssListen = v[kDeEssListen] >= 0.5;
  s.bypass = v[kBypass] >= 0.5;
  s.riderOn = v[kRiderIn] >= 0.5;
  s.breathOn = v[kBreathIn] >= 0.5;
  s.eqOn = v[kEqIn] >= 0.5;
  s.deEssOn = v[kDeEssIn] >= 0.5;
  s.compOn = v[kCompIn] >= 0.5;
  s.toneOn = v[kToneIn] >= 0.5;
  return s;
}

Engine::Engine() {
  double d[kNumParams];
  loadDefaults(d);
  s_ = mapParams(d);
  prepare(48000.0);
}

void Engine::prepare(double sampleRate) {
  sr_ = sampleRate > 1.0 && std::isfinite(sampleRate) ? sampleRate : 48000.0;
  spectral_.prepare(sr_);
  level_.prepare(sr_);
  meter_.prepare(sr_, kRiderMeterMs);
  rider_.prepare(sr_);
  reducer_.prepare(sr_);
  eq_.prepare(sr_, 2);
  deess_.prepare(sr_, 2);
  comp_.prepare(sr_, 2);
  tone_.prepare(sr_, 2);

  clipHoldSamples_ = (long long)std::lround(kClipHoldSeconds * sr_);
  lookahead_ = (int)std::lround(kLookaheadMs * 0.001 * sr_);
  latency_ = lookahead_ + spectral_.latencySamples();
  delayL_.assign((size_t)latency_ + 1, 0.0f);
  delayR_.assign((size_t)latency_ + 1, 0.0f);
  gainedL_.assign((size_t)latency_ + 1, 0.0f);
  gainedR_.assign((size_t)latency_ + 1, 0.0f);
  window_ = latency_;
  maxVal_.assign((size_t)window_ + 1, 0.0f);
  maxIdx_.assign((size_t)window_ + 1, 0);

  gainCoef_ = (float)(1.0 - std::exp(-1.0 / (kGainSmoothMs * 0.001 * sr_)));
  rampInc_ = (float)(1.0 / (kSwitchFadeMs * 0.001 * sr_));

  applySettings(s_, true);
  reset();
}

void Engine::resetBlocks() {
  spectral_.reset();
  level_.reset();
  meter_.reset();
  rider_.reset();
  reducer_.reset();
  eq_.reset();
  deess_.reset();
  comp_.reset();
  tone_.reset();
}

void Engine::reset() {
  resetBlocks();
  std::fill(delayL_.begin(), delayL_.end(), 0.0f);
  std::fill(delayR_.begin(), delayR_.end(), 0.0f);
  std::fill(gainedL_.begin(), gainedL_.end(), 0.0f);
  std::fill(gainedR_.begin(), gainedR_.end(), 0.0f);
  writePos_ = 0;
  maxHead_ = maxCount_ = 0;
  sampleIndex_ = 0;
  snapPending_ = true;
  snapSmoothers();
  resetDisplay();
  clipInLeft_ = clipOutLeft_ = 0;
}

void Engine::resetDisplay() {
  inPeakL_ = inPeakR_ = outPeakL_ = outPeakR_ = 0.f;
  voiceMaxDb_ = -120.f;
  riderMinGain_ = kNoRiderGain;
  breathMax_ = 0.f;
  grMaxDb_ = 0.f;
}

namespace {
inline float peakToDb(float peak) { return peak > 1e-6f ? 20.0f * std::log10(peak) : -120.0f; }
} // namespace

LevelValues Engine::takeLevels() {
  LevelValues v;
  v.inL = peakToDb(inPeakL_);
  v.inR = peakToDb(inPeakR_);
  v.outL = peakToDb(outPeakL_);
  v.outR = peakToDb(outPeakR_);
  v.clipIn = clipInLeft_ > 0;
  v.clipOut = clipOutLeft_ > 0;
  inPeakL_ = inPeakR_ = outPeakL_ = outPeakR_ = 0.f;
  return v;
}

DisplayValues Engine::takeDisplay() {
  DisplayValues v;
  v.voiceDb = voiceMaxDb_;
  v.riderGainDb = riderMinGain_ == kNoRiderGain ? 0.f : peakToDb(riderMinGain_);
  v.breathProb = breathMax_;
  v.grDb = grMaxDb_;
  voiceMaxDb_ = -120.f;
  riderMinGain_ = kNoRiderGain;
  breathMax_ = 0.f;
  grMaxDb_ = 0.f;
  return v;
}

void Engine::snapSmoothers() {
  inGain_ = inGainTarget_;
  outGain_ = outGainTarget_;
  for (Ramp* r : {&riderIn_, &breathIn_, &eqIn_, &deEssIn_, &compIn_, &toneIn_, &breathListen_,
                  &deEssListen_, &bypass_})
    r->v = r->target;
}

void Engine::applySettings(const Settings& s, bool force) {
  if (force || s.mode != s_.mode) {
    eq_.setMode(s.mode);
    deess_.setMode(s.mode);
    comp_.setMode(s.mode);
    tone_.setMode(s.mode);
    DSP::BreathDetector* want =
        modeRow(s.mode).levelDetector ? static_cast<DSP::BreathDetector*>(&level_) : &spectral_;
    if (want != detector_) {
      detector_ = want;
      // The newly selected detector starts clean: it needs a few hundred ms of phrase context
      // before it detects again.
      detector_->reset();
    }
  }
  // Setters are idempotent and never allocate; every block-level change is smoothed inside.
  rider_.setTargetDb(s.riderTargetDb);
  rider_.setMaxRangeDb(s.riderRangeDb);
  rider_.setSlewDbPerSec(modeRow(s.mode).riderSlewDbPerSec);
  spectral_.setSensitivity(s.breathSens);
  level_.setSensitivity(s.breathSens);
  reducer_.setAmount(s.breathAmount);
  eq_.setAmount(s.eqAmount);
  deess_.setAmount(s.deEssAmount);
  deess_.setFreqHz(s.deEssFreqHz);
  comp_.setAmount(s.compAmount);
  comp_.setMix(s.compMix);
  comp_.setRatioChoice(s.compRatioChoice);
  comp_.setDistChoice(s.compDistChoice);
  comp_.setDetectorHpf(s.compHpfChoice);
  tone_.setTone(s.tone);

  inGainTarget_ = (float)dbToGain(s.inputDb);
  outGainTarget_ = (float)dbToGain(s.outputDb);
  riderIn_.target = s.riderOn ? 1.0f : 0.0f;
  breathIn_.target = s.breathOn ? 1.0f : 0.0f;
  eqIn_.target = s.eqOn ? 1.0f : 0.0f;
  deEssIn_.target = s.deEssOn ? 1.0f : 0.0f;
  compIn_.target = s.compOn ? 1.0f : 0.0f;
  toneIn_.target = s.toneOn ? 1.0f : 0.0f;
  breathListen_.target = s.breathListen ? 1.0f : 0.0f;
  deEssListen_.target = s.deEssListen ? 1.0f : 0.0f;
  bypass_.target = s.bypass ? 1.0f : 0.0f;
  s_ = s;
}

void Engine::setSettings(const Settings& raw) {
  applySettings(sanitize(raw), false);
  if (snapPending_) {
    // First settings after prepare()/reset(): start at the targets, no glide from stale values.
    snapPending_ = false;
    resetBlocks();
    snapSmoothers();
  }
}

float Engine::lookaheadMax(float prob) {
  // Window: the last latency_ + 1 probabilities, i.e. every value produced since the delayed audio
  // sample entered the engine. Both detectors drop their probability as soon as the phrase reaches
  // their newest input sample (the level detector has no latency; the spectral frame turns
  // voiced as soon as the phrase enters it, ~F before the frame-centre alignment its
  // latencySamples() assumes), so the window must reach back the full delay D for the breath tail
  // to stay covered. A window of D - detector latency left the last ~F of every breath
  // unreduced in Puke and released ~20 ms early in Speak.
  const int cap = (int)maxVal_.size();
  const long long oldest = sampleIndex_ - window_;
  while (maxCount_ > 0 && maxIdx_[(size_t)maxHead_] < oldest) {
    maxHead_ = maxHead_ + 1 == cap ? 0 : maxHead_ + 1;
    --maxCount_;
  }
  while (maxCount_ > 0) {
    int back = maxHead_ + maxCount_ - 1;
    if (back >= cap) back -= cap;
    if (maxVal_[(size_t)back] > prob) break;
    --maxCount_;
  }
  int pos = maxHead_ + maxCount_;
  if (pos >= cap) pos -= cap;
  maxVal_[(size_t)pos] = prob;
  maxIdx_[(size_t)pos] = sampleIndex_;
  ++maxCount_;
  ++sampleIndex_;
  return maxVal_[(size_t)maxHead_];
}

void Engine::process(const float* inL, const float* inR, float* outL, float* outR, int n) {
  if (!inL || !outL || n <= 0) return;
  const int len = latency_ + 1;
  for (int i = 0; i < n; ++i) {
    // Read both inputs before writing: buffers may alias.
    const float xl = clean(inL[i]);
    const float xr = inR ? clean(inR[i]) : xl;

    inGain_ = smooth(inGain_, inGainTarget_, gainCoef_);
    outGain_ = smooth(outGain_, outGainTarget_, gainCoef_);
    const float rRider = riderIn_.step(rampInc_), rBreath = breathIn_.step(rampInc_);
    const float rEq = eqIn_.step(rampInc_), rDeEss = deEssIn_.step(rampInc_);
    const float rComp = compIn_.step(rampInc_), rTone = toneIn_.step(rampInc_);
    const float rBreathListen = breathListen_.step(rampInc_);
    const float rDeEssListen = deEssListen_.step(rampInc_);
    const float rBypass = bypass_.step(rampInc_);
    const float gIn = inGain_;

    // Detection on the live (undelayed) stereo-linked mono signal.
    const float ll = xl * gIn, lr = xr * gIn;
    const float mono = 0.5f * (ll + lr);
    const float levelDb = meter_.process(mono);
    const float rawProb = detector_->process(mono);
    const float prob = lookaheadMax(rawProb);
    voiceMaxDb_ = std::max(voiceMaxDb_, levelDb);
    breathMax_ = std::max(breathMax_, rawProb);
    {  // input peaks and clip: before the Input gain
      const float al_ = std::fabs(xl), ar_ = std::fabs(xr);
      inPeakL_ = std::max(inPeakL_, al_);
      inPeakR_ = std::max(inPeakR_, ar_);
      if (al_ >= 1.0f || ar_ >= 1.0f) clipInLeft_ = clipHoldSamples_;
      else if (clipInLeft_ > 0) --clipInLeft_;
    }

    // Audio delay.
    delayL_[(size_t)writePos_] = xl;
    delayR_[(size_t)writePos_] = xr;
    gainedL_[(size_t)writePos_] = ll;
    gainedR_[(size_t)writePos_] = lr;
    writePos_ = writePos_ + 1 == len ? 0 : writePos_ + 1;
    const float dl = delayL_[(size_t)writePos_], dr = delayR_[(size_t)writePos_];
    const float al = gainedL_[(size_t)writePos_], ar = gainedR_[(size_t)writePos_];

    // Rider and breath reducer gains (they always run; IN crossfades the gain to 1).
    const float gRider = rider_.processFrame(levelDb, prob > 0.5f);
    const float gBreath = reducer_.process(prob);
    const float gR = xfade(1.0f, gRider, rRider), gB = xfade(1.0f, gBreath, rBreath);
    riderMinGain_ = std::min(riderMinGain_, gR);
    float l = al * gR * gB, r = ar * gR * gB;

    // Corrective EQ.
    {
      const float el = eq_.process(0, l), er = eq_.process(1, r);
      l = xfade(l, el, rEq);
      r = xfade(r, er, rEq);
    }
    // De-esser (the removed band, input - output, is what De-ess Listen plays).
    float f[2] = {l, r};
    deess_.processFrame(f);
    const float remL = l - f[0], remR = r - f[1];
    l = xfade(l, f[0], rDeEss);
    r = xfade(r, f[1], rDeEss);
    // Compressor (parallel Mix inside).
    f[0] = l;
    f[1] = r;
    comp_.processFrame(f);
    grMaxDb_ = std::max(grMaxDb_, comp_.lastGainReductionDb());
    l = xfade(l, f[0], rComp);
    r = xfade(r, f[1], rComp);
    // Tone.
    {
      const float tl = tone_.process(0, l), tr = tone_.process(1, r);
      l = xfade(l, tl, rTone);
      r = xfade(r, tr, rTone);
    }
    // Listen: De-ess, then Breath (precedence). The later stages are not heard. Breath Listen uses
    // the reducer gain only (no Rider gain), also with Breath IN off.
    l = xfade(l, remL, rDeEssListen);
    r = xfade(r, remR, rDeEssListen);
    const float removed = 1.0f - gBreath;
    l = xfade(l, al * removed, rBreathListen);
    r = xfade(r, ar * removed, rBreathListen);

    l *= outGain_;
    r *= outGain_;
    // Bypass: the delayed input only.
    l = xfade(l, dl, rBypass);
    r = xfade(r, dr, rBypass);

    {  // output peaks and clip: after the Output gain and Bypass
      const float ol_ = std::fabs(l), or_ = std::fabs(r);
      outPeakL_ = std::max(outPeakL_, ol_);
      outPeakR_ = std::max(outPeakR_, or_);
      if (ol_ >= 1.0f || or_ >= 1.0f) clipOutLeft_ = clipHoldSamples_;
      else if (clipOutLeft_ > 0) --clipOutLeft_;
    }

    outL[i] = l;
    if (outR) outR[i] = r;
  }
}

} // namespace FairyTapes::DeepThroat
