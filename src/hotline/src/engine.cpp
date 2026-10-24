#include "engine.h"
#include "dsp/drive.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace FairyTapes::Hotline {

namespace {
constexpr int kUpdateInterval = 32;      // samples between two block-value updates
constexpr double kSmoothMs = 10.0;       // one-pole time constant of every smoothed value
constexpr double kMonoRampMs = 10.0;     // linear
constexpr double kBypassRampMs = 5.0;    // linear
constexpr double kBandFadeOctaves = 0.5; // band crossfades from dry over its first half octave
constexpr double kDcHz = 10.0;
constexpr double kOffDb = -120.0;
constexpr double kMinHz = 20.0, kMaxHz = 20000.0;  // neutral band corners (neutralParams)
constexpr uint32_t kSeed[2] = {0x484C494Eu, 0x484C4952u};

double norm(const double* v, int id, const double* defaults) {
  return std::isfinite(v[id]) ? std::clamp(v[id], 0.0, 1.0) : defaults[id];
}

bool finite(const Settings& s) {
  for (float x : {s.morph, s.gainDb, s.drive, s.tone, s.noise, s.room, s.mix})
    if (!std::isfinite(x)) return false;
  return true;
}

void applyCharacterIn(Settings& s) {
  if (s.characterIn) return;
  s.drive = 0.f;
  s.tone = 0.f;
  s.noise = 0.f;
  s.room = 0.f;
  s.mix = 1.f;
}

// One-pole step in the linear domain; snaps onto the target within eps. True if v moved.
bool approach(double& v, double t, double k, double eps) {
  if (v == t) return false;
  v += (t - v) * k;
  if (std::fabs(v - t) <= eps) v = t;
  return true;
}

// Same in the log domain (frequencies, rates). The snap assigns the target itself, not
// exp(log(target)), so values compared exactly by the blocks (crush rate >= fs) stay exact.
bool approachLog(double& v, double t, double k, double eps) {
  if (v == t) return false;
  const double lt = std::log(t);
  const double n = std::log(v) + (lt - std::log(v)) * k;
  v = std::fabs(n - lt) <= eps ? t : std::exp(n);
  return true;
}

// Linear ramp of a 0/1 switch value; lands exactly on 0 or 1.
void ramp(float& v, float t, float step) {
  if (v == t) return;
  v = t > v ? std::min(t, v + step) : std::max(t, v - step);
}

// One-pole per sample with a relative snap.
void glide(float& v, float t, float c) {
  if (v == t) return;
  v = t + c * (v - t);
  if (std::fabs(v - t) <= 1e-6f * std::max(1.f, std::fabs(t))) v = t;
}

float toDb(float peak) { return 20.f * std::log10(std::max(peak, 1e-6f)); }
} // namespace

void loadDefaults(double* v) {
  v[kBypass] = kDefaultBypass;
  v[kDevice] = kDefaultDevice;
  v[kMorph] = kDefaultMorph;
  v[kGain] = kDefaultGain;
  v[kMono] = kDefaultMono;
  v[kDrive] = kDefaultDrive;
  v[kTone] = kDefaultTone;
  v[kNoise] = kDefaultNoise;
  v[kRoom] = kDefaultRoom;
  v[kMix] = kDefaultMix;
  v[kCharacterIn] = kDefaultCharacterIn;
}

Settings mapParams(const double* raw) {
  double defaults[kNumParams], v[kNumParams];
  loadDefaults(defaults);
  for (int i = 0; i < (int)kNumParams; ++i) v[i] = norm(raw, i, defaults);
  Settings s;
  s.device = deviceIndexFromNorm(v[kDevice]);
  s.morph = (float)v[kMorph];
  s.gainDb = (float)gainDbFromNorm(v[kGain]);
  s.drive = (float)amountFromNorm(v[kDrive]);
  s.tone = (float)toneLpOctavesFromNorm(v[kTone]);
  s.noise = (float)amountFromNorm(v[kNoise]);
  s.room = (float)amountFromNorm(v[kRoom]);
  s.mix = (float)v[kMix];
  s.mono = v[kMono] >= 0.5;
  s.bypass = v[kBypass] >= 0.5;
  s.characterIn = v[kCharacterIn] >= 0.5;
  applyCharacterIn(s);
  return s;
}

BlockParams targetParams(const BlockParams& device, const Settings& in) {
  Settings s = in;
  applyCharacterIn(s);
  auto amount = [](float a) { return std::isfinite(a) ? std::clamp((double)a, 0.0, 2.0) : 1.0; };
  const double t = std::isfinite(s.morph) ? std::clamp((double)s.morph, 0.0, 1.0) : 0.0;
  const double drive = amount(s.drive), noise = amount(s.noise), room = amount(s.room);
  const double tone = std::isfinite(s.tone) ? std::clamp((double)s.tone, -1.0, 1.0) : 0.0;
  BlockParams p = morphParams(device, t);
  p.drive = std::clamp(p.drive * drive, 0.0, 1.0);
  p.room = std::clamp(p.room * room, 0.0, 1.0);
  // noise is an amplitude multiplier; an off floor stays off (Morph 0 must stay silent)
  if (noise <= 0.0 || p.noiseDb <= kOffDb) p.noiseDb = kOffDb;
  else p.noiseDb = std::clamp(p.noiseDb + 20.0 * std::log10(noise), kOffDb, kNoiseCeilingDb);
  // Tone moves the corners by octaves scaled with morph: a neutral band stays neutral at Morph 0.
  p.lpHz = std::clamp(p.lpHz * std::exp2(tone * t), kMinHz, kMaxHz);
  p.hpHz = std::clamp(p.hpHz * std::exp2(-0.5 * tone * t), kMinHz, kMaxHz);
  return p;
}

void Engine::prepare(double sampleRate, int maxBlockSize) {
  fs_ = std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 48000.0;
  maxBlock_ = std::max(1, maxBlockSize);
  xL_.assign((size_t)maxBlock_, 0.f);
  xR_.assign((size_t)maxBlock_, 0.f);
  for (int c = 0; c < 2; ++c) {
    Channel& ch = ch_[c];
    ch.band.prepare(fs_);
    ch.cone.prepare(fs_);
    ch.crush.prepare(fs_);
    ch.comp.prepare(fs_);
    ch.noise.prepare(fs_, kSeed[c]);
    ch.room.prepare(fs_, c);  // R gets the stereo spread: decorrelated tails
    ch.dcX = ch.dcY = 0.f;
    ch.bandOn = false;
  }
  smoothK_ = 1.0 - std::exp(-(double)kUpdateInterval / (0.001 * kSmoothMs * fs_));
  smoothC_ = (float)std::exp(-1.0 / (0.001 * kSmoothMs * fs_));
  monoStep_ = (float)(1.0 / (0.001 * kMonoRampMs * fs_));
  bypStep_ = (float)(1.0 / (0.001 * kBypassRampMs * fs_));
  dcR_ = (float)std::exp(-2.0 * 3.14159265358979323846 * kDcHz / fs_);
  dcOn_ = false;
  bandW_ = 0.f;
  drive_ = 0.f;
  clipHoldSamples_ = (long long)(fs_ * kClipHoldSeconds);
  clearClips();
  display_ = DisplayValues{{-120.f, -120.f}, {-120.f, -120.f}, false, false, false};
  double v[kNumParams];
  loadDefaults(v);
  s_ = mapParams(v);
  computeTargets();
  snapToTargets();
  init_ = false;  // the host's first settings jump straight in too
}

void Engine::computeTargets() {
  const int d = std::clamp(s_.device, 0, kNumDevices - 1);
  target_ = targetParams(kDevices[d].p, s_);
  gainT_ = (float)std::pow(10.0, std::clamp((double)s_.gainDb, -20.0, 20.0) / 20.0);
  mixT_ = std::clamp(s_.mix, 0.f, 1.f);
  monoT_ = s_.mono ? 1.f : 0.f;
  bypT_ = s_.bypass ? 1.f : 0.f;
}

void Engine::snapToTargets() {
  cur_ = target_;
  gain_ = gainT_;
  mix_ = mixT_;
  mono_ = monoT_;
  byp_ = bypT_;
  force_ = true;   // push the values into the blocks on the next sample
  counter_ = 0;
}

void Engine::setSettings(const Settings& in) {
  if (!finite(in)) return;  // keep the last valid settings; never feed NaN/inf to the DSP
  Settings s = in;
  applyCharacterIn(s);
  s_ = s;
  computeTargets();
  if (!init_) {
    snapToTargets();
    init_ = true;
  }
}

void Engine::reset() {
  for (int c = 0; c < 2; ++c) {
    clearChannel(c);
    ch_[c].noise.prepare(fs_, kSeed[c]);  // same sequence as a fresh instance
    ch_[c].bandOn = false;
  }
  dcOn_ = false;
  snapToTargets();
  clearClips();
  display_ = DisplayValues{{-120.f, -120.f}, {-120.f, -120.f}, false, false, false};
}

void Engine::clearChannel(int c) {
  Channel& ch = ch_[c];
  ch.band.clear();
  ch.dcX = ch.dcY = 0.f;
  ch.cone.clear();
  ch.crush.clear();
  ch.comp.clear();
  ch.room.clear();
}

void Engine::updateBlocks() {
  const double k = smoothK_;
  BlockParams& p = cur_;
  const BlockParams& t = target_;
  bool changed = force_;
  force_ = false;
  changed |= approachLog(p.hpHz, t.hpHz, k, 1e-6);
  changed |= approachLog(p.lpHz, t.lpHz, k, 1e-6);
  changed |= approach(p.drive, t.drive, k, 1e-6);
  changed |= approachLog(p.coneHz, t.coneHz, k, 1e-6);
  changed |= approach(p.coneGainDb, t.coneGainDb, k, 1e-5);
  changed |= approach(p.coneQ, t.coneQ, k, 1e-6);
  changed |= approach(p.hornGainDb, t.hornGainDb, k, 1e-5);
  changed |= approachLog(p.crushRateHz, t.crushRateHz, k, 1e-6);
  changed |= approach(p.crushBits, t.crushBits, k, 1e-6);
  changed |= approach(p.compRatio, t.compRatio, k, 1e-6);
  changed |= approach(p.squelchDb, t.squelchDb, k, 1e-4);
  changed |= approach(p.noiseDb, t.noiseDb, k, 1e-4);
  changed |= approach(p.room, t.room, k, 1e-6);
  if (!changed) return;

  // Band weight: 0 exactly at the neutral corners, 1 from half an octave away. It hides the cold
  // start of the filters where the band leaves neutral (a step of ~0.3..0.5 on a loud tone during
  // slow Morph automation) and the band's own neutral threshold on the way back.
  const double lp = std::clamp(p.lpHz, kMinHz, kMaxHz), hp = std::clamp(p.hpHz, kMinHz, kMaxHz);
  const double dist = std::max({0.0, std::log2(kMaxHz / lp), std::log2(hp / kMinHz)});
  bandW_ = (float)std::min(1.0, dist / kBandFadeOctaves);

  drive_ = (float)p.drive;
  const bool dcOn = drive_ > 0.f;
  if (dcOn && !dcOn_)
    for (auto& ch : ch_) ch.dcX = ch.dcY = 0.f;  // frozen while off: drop it on re-activation
  dcOn_ = dcOn;

  for (auto& ch : ch_) {
    const bool on = bandW_ > 0.f;
    if (on && !ch.bandOn) ch.band.clear();  // skipped while off: its state is stale
    ch.bandOn = on;
    ch.band.set(p.hpHz, p.lpHz);
    ch.cone.set(p.coneHz, p.coneGainDb, p.coneQ, p.hornGainDb);
    ch.crush.set(p.crushRateHz, p.crushBits);
    ch.comp.set(p.compRatio, p.squelchDb);
    ch.noise.set(p.noiseDb);
    ch.room.set(p.room);
  }
}

float Engine::chain(int c, float x) {
  Channel& ch = ch_[c];
  float y = x;
  if (ch.bandOn) {
    const float f = ch.band.process(y);
    y = bandW_ >= 1.f ? f : y + bandW_ * (f - y);
  }
  if (dcOn_) {
    y = DSP::shape(y, drive_);
    drivePeak_ = std::max(drivePeak_, std::fabs(y));
    const float o = y - ch.dcX + dcR_ * ch.dcY;
    ch.dcX = y;
    ch.dcY = std::fabs(o) < 1e-20f ? 0.f : o;
    y = ch.dcY;
  } else {
    drivePeak_ = std::max(drivePeak_, std::fabs(y));
  }
  y = ch.cone.process(y);
  y = ch.crush.process(y);
  y = ch.comp.process(y);
  y += ch.noise.next();
  return ch.room.process(y);
}

void Engine::process(const float* inL, const float* inR, float* outL, float* outR, int numSamples) {
  if (numSamples <= 0 || !inL || !outL) return;
  const bool monoHost = !inR || inR == inL;
  if (!inR) inR = inL;
  if (maxBlock_ == 0) {  // not prepared: pass through
    std::memmove(outL, inL, sizeof(float) * (size_t)numSamples);
    if (outR) std::memmove(outR, inR, sizeof(float) * (size_t)numSamples);
    return;
  }
  inPeak_[0] = inPeak_[1] = outPeak_[0] = outPeak_[1] = drivePeak_ = 0.f;
  for (int off = 0; off < numSamples; off += maxBlock_) {
    const int n = std::min(maxBlock_, numSamples - off);
    processChunk(inL + off, inR + off, outL + off, outR ? outR + off : nullptr, n, monoHost);
  }
  for (int c = 0; c < 2; ++c) {
    display_.inPeakDb[c] = toDb(inPeak_[c]);
    display_.outPeakDb[c] = toDb(outPeak_[c]);
  }
  const auto hold = [&](long long& left, float peak) {
    left = peak >= 1.f ? clipHoldSamples_ : std::max(0LL, left - numSamples);
  };
  hold(clipInLeft_, std::max(inPeak_[0], inPeak_[1]));
  hold(clipDriveLeft_, drivePeak_);
  hold(clipOutLeft_, std::max(outPeak_[0], outPeak_[1]));
}

DisplayValues Engine::lastDisplay() const {
  DisplayValues d = display_;
  d.clipIn = clipInLeft_ > 0;
  d.clipDrive = clipDriveLeft_ > 0;
  d.clipOut = clipOutLeft_ > 0;
  return d;
}

void Engine::processChunk(const float* inL, const float* inR, float* outL, float* outR, int n,
                          bool monoHost) {
  // Copy (and sanitise) everything we read before writing any output: buffers may alias.
  // A non-finite sample becomes 0 and clears its channel's chain, at most once per chunk (a
  // Room clear is a ~100 KB fill; a stream of NaNs must not turn into one per sample).
  int firstBad[2] = {-1, -1};
  for (int i = 0; i < n; ++i) {
    float l = inL[i], r = inR[i];
    if (!std::isfinite(l)) { l = 0.f; if (firstBad[0] < 0) firstBad[0] = i; }
    if (!std::isfinite(r)) { r = 0.f; if (firstBad[1] < 0) firstBad[1] = i; }
    xL_[(size_t)i] = l;
    xR_[(size_t)i] = r;
    inPeak_[0] = std::max(inPeak_[0], std::fabs(l));
    inPeak_[1] = std::max(inPeak_[1], std::fabs(r));
  }
  if (monoHost) firstBad[1] = firstBad[0];
  bool cleared[2] = {false, false};

  for (int i = 0; i < n; ++i) {
    if (counter_ == 0) updateBlocks();
    if (++counter_ == kUpdateInterval) counter_ = 0;
    ramp(mono_, monoT_, monoStep_);
    ramp(byp_, bypT_, bypStep_);
    glide(gain_, gainT_, smoothC_);
    glide(mix_, mixT_, smoothC_);
    for (int c = 0; c < 2; ++c)
      if (i == firstBad[c] && !cleared[c]) { clearChannel(c); cleared[c] = true; }

    const float xl = xL_[(size_t)i], xr = xR_[(size_t)i];
    // Mono sum, then the dry tap for Mix.
    float dl = xl, dr = xr;
    if (mono_ > 0.f) {
      const float mid = 0.5f * (xl + xr);
      if (mono_ >= 1.f) dl = dr = mid;
      else { dl = xl + mono_ * (mid - xl); dr = xr + mono_ * (mid - xr); }
    }
    float w[2] = {chain(0, dl), chain(1, dr)};
    for (int c = 0; c < 2; ++c)
      if (!std::isfinite(w[c])) {  // never expected; keep the output and the state sane anyway
        w[c] = 0.f;
        if (!cleared[c]) { clearChannel(c); cleared[c] = true; }
      }
    const float d[2] = {dl, dr};
    float y[2];
    for (int c = 0; c < 2; ++c) {
      const float m = mix_ >= 1.f ? w[c] : mix_ <= 0.f ? d[c] : d[c] + mix_ * (w[c] - d[c]);
      y[c] = std::clamp(m * gain_, -kOutputCeiling, kOutputCeiling);
    }
    // Mono also on the output: the R chain's own noise and room are faded out with the ramp.
    if (monoHost || mono_ >= 1.f) y[1] = y[0];
    else if (mono_ > 0.f) y[1] += mono_ * (y[0] - y[1]);
    // Bypass: ramp to the (sanitised) input, then an exact copy of it.
    const float x[2] = {xl, monoHost ? xl : xr};
    for (int c = 0; c < 2; ++c) {
      if (byp_ >= 1.f) y[c] = x[c];
      else if (byp_ > 0.f) y[c] += byp_ * (x[c] - y[c]);
    }
    outL[i] = y[0];
    if (outR) outR[i] = y[1];
    outPeak_[0] = std::max(outPeak_[0], std::fabs(y[0]));
    outPeak_[1] = std::max(outPeak_[1], std::fabs(y[1]));
  }
}

} // namespace FairyTapes::Hotline
