#include "dsp/glue.h"
#include "tuning.h"
#include <algorithm>
#include <cmath>

namespace FairyTapes::HappyEnding::DSP {

namespace {

// Constants of the source (multicomp.cpp, namespace Constants and BusCompressor).
constexpr float kBusMaxReductionDb = 20.0f;
constexpr float kBusKneeWidthDb = 10.0f;     // over-easy knee
constexpr float kBusRmsTime = 0.005f;        // 5 ms RMS averaging of the sidechain
constexpr float kOutputHardLimit = 2.0f;
constexpr float kConsoleK2 = 0.004f;         // 2nd harmonic of the console path
constexpr float kConsoleK3 = 0.003f;         // 3rd harmonic
constexpr float kAutoReleaseMin = 0.15f;     // Auto release: 150-450 ms
constexpr float kAutoReleaseMax = 0.45f;
constexpr float kTwoPi = 6.283185f;

// Console Bus profile (HardwareMeasurements.h createConsoleBus): saturationAmount, lowFreqSaturation,
// hysteresis, h2, h3, h4 (HarmonicProfile::create(h2, h3, evenOdd) leaves h4 at 0), rolloff, DC block.
struct TransformerSpec {
  float saturation, lowFreqSat, hysteresis, h2, h3, h4, rolloffHz, dcHz;
};
constexpr TransformerSpec kInput{0.03f, 1.0f, 0.008f, 0.002f, 0.004f, 0.0f, 100000.0f, 10.0f};
constexpr TransformerSpec kOutput{0.02f, 1.0f, 0.008f, 0.002f, 0.003f, 0.0f, 100000.0f, 8.0f};

// Calibration of the hardware gain compensation (source: calibrateHardwareGain).
constexpr int kCalibrationSamples = 4800;
constexpr float kCalibrationAmp = 0.126f;
constexpr float kCalibrationHz = 1000.0f;

// Magnitudes below this are flushed to zero in the recursive states (no denormals in the tail).
constexpr float kTiny = 1.0e-18f;
inline void flush(float& v) { if (std::fabs(v) < kTiny) v = 0.0f; }

// Transformer waveshaper of the source (WaveshaperCurves.h), evaluated directly instead of from
// a 4096-point table over -2..2.
inline float transformerCurve(float x) {
  x = std::clamp(x, -2.0f, 2.0f);
  const float a = std::fabs(x);
  const float sign = x >= 0.0f ? 1.0f : -1.0f;
  if (a < 0.7f) return x + x * a * 0.05f;
  if (a < 1.2f) {
    const float excess = a - 0.7f;
    const float compressed = 0.7f + excess * (1.0f - excess * 0.25f);
    return sign * compressed + (sign * compressed) * compressed * 0.08f;
  }
  const float excess = a - 1.2f;
  return sign * (1.05f + std::tanh(excess * 1.5f) * 0.15f);
}

} // namespace

// ---- Transformer -----------------------------------------------------------------------------

void Glue::Transformer::prepare(double sampleRate, float saturation, float lowFreqSat, float hysteresisAmt,
                                float harmonic2, float harmonic3, float harmonic4, float hfRolloffHz, float dcHz) {
  saturationAmount = saturation;
  lowFreqSaturation = lowFreqSat;
  hysteresisAmount = hysteresisAmt;
  h2 = harmonic2;
  h3 = harmonic3;
  h4 = harmonic4;
  const float sr = static_cast<float>(sampleRate);
  dcCoeff = 1.0f - kTwoPi * std::max(0.5f, dcHz) / sr;
  hfEnabled = hfRolloffHz > 0.0f && hfRolloffHz < 0.45f * sr;  // a rolloff above Nyquist does nothing
  hfCoeff = hfEnabled ? 1.0f - std::exp(-kTwoPi * hfRolloffHz / sr) : 1.0f;
  reset();
}

void Glue::Transformer::reset() {
  for (int ch = 0; ch < 2; ++ch) {
    dcX1[ch] = dcY1[ch] = hfState[ch] = lastSample[ch] = hfEstimate[ch] = hysteresis[ch] = 0.0f;
  }
}

float Glue::Transformer::process(float input, int channel) {
  // High-frequency content estimate: LF saturates a little more.
  const float diff = std::fabs(input - lastSample[channel]);
  lastSample[channel] = input;
  hfEstimate[channel] = hfEstimate[channel] * 0.95f + diff * 0.05f;
  flush(hfEstimate[channel]);
  const float hfContent = std::clamp(hfEstimate[channel] * 3.0f, 0.0f, 1.0f);
  const float lfMultiplier = lowFreqSaturation * (1.0f - hfContent * 0.15f);

  const float saturated = transformerCurve(input * lfMultiplier);
  float out = input + (saturated - input) * saturationAmount;

  if (hysteresisAmount > 0.0f) {
    out += hysteresisAmount * hysteresis[channel];
    hysteresis[channel] = hysteresis[channel] * 0.95f + (out - input) * 0.05f;
    flush(hysteresis[channel]);
  }

  const float x2 = out * out;
  out += h2 * x2 + h3 * x2 * out;
  if (h4 > 0.0f) out += h4 * x2 * x2;

  if (hfEnabled) {
    hfState[channel] += hfCoeff * (out - hfState[channel]);
    flush(hfState[channel]);
    out = hfState[channel];
  }

  const float y = out - dcX1[channel] + dcCoeff * dcY1[channel];
  dcX1[channel] = out;
  dcY1[channel] = y;
  flush(dcY1[channel]);
  return y;
}

// ---- Ramp ------------------------------------------------------------------------------------

void Glue::Ramp::to(float v, int samples) {
  if (v == target) return;
  target = v;
  left = std::max(1, samples);
  step = (v - cur) / static_cast<float>(left);
}

float Glue::Ramp::next() {
  if (left > 0) {
    cur += step;
    if (--left == 0) cur = target;
  }
  return cur;
}

// ---- Glue ------------------------------------------------------------------------------------

void Glue::prepare(double sampleRate, int /*maxBlock*/) {
  sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
  rampSamples_ = std::max(1, static_cast<int>(Tuning::kGlueRampMs * 0.001 * sampleRate_));
  const float sr = static_cast<float>(sampleRate_);
  rmsCoeff_ = std::exp(-1.0f / std::max(1.0f, kBusRmsTime * sr));
  inT_.prepare(sampleRate_, kInput.saturation, kInput.lowFreqSat, kInput.hysteresis, kInput.h2, kInput.h3,
               kInput.h4, kInput.rolloffHz, kInput.dcHz);
  outT_.prepare(sampleRate_, kOutput.saturation, kOutput.lowFreqSat, kOutput.hysteresis, kOutput.h2,
                kOutput.h3, kOutput.h4, kOutput.rolloffHz, kOutput.dcHz);
  calibrate();
  reset();
  primed_ = false;
  set(s_);
  primed_ = false;  // the first set() after prepare() snaps MAKEUP, MIX and on/off instead of gliding
}

void Glue::calibrate() {
  // Gain of the transformer pair at 1 kHz, -18 dBFS: the stage is brought back to unity there.
  const float sr = static_cast<float>(sampleRate_);
  const float step = kTwoPi * kCalibrationHz / sr;
  inT_.reset();
  outT_.reset();
  const int warmup = static_cast<int>(sr * 0.05f);
  for (int i = 0; i < warmup; ++i)
    outT_.process(inT_.process(kCalibrationAmp * std::sin(step * static_cast<float>(i)), 0), 0);
  double inSq = 0.0, outSq = 0.0;
  for (int i = 0; i < kCalibrationSamples; ++i) {
    const float x = kCalibrationAmp * std::sin(step * static_cast<float>(warmup + i));
    const float y = outT_.process(inT_.process(x, 0), 0);
    inSq += static_cast<double>(x) * x;
    outSq += static_cast<double>(y) * y;
  }
  inSq /= kCalibrationSamples;
  outSq /= kCalibrationSamples;
  hardwareGain_ = (outSq > 1e-12 && inSq > 1e-12) ? static_cast<float>(1.0 / std::sqrt(outSq / inSq)) : 1.0f;
}

void Glue::reset() {
  inT_.reset();
  outT_.reset();
  det_[0] = Detector{};
  det_[1] = Detector{};
  envelope_ = 1.0f;
  previousLevel_ = 0.0f;
  grDb_ = netGrDb_ = 0.0f;
}

void Glue::set(const GlueSettings& settings) {
  s_ = settings;
  s_.ratio = std::max(1.0f, s_.ratio);
  s_.mix = std::clamp(s_.mix, 0.0f, 1.0f);
  const float sr = static_cast<float>(sampleRate_);

  const float attackSec = std::max(0.0f, s_.attackMs) * 0.001f *
                          (s_.warm ? static_cast<float>(Tuning::kGlueWarmAttackFactor) : 1.0f);
  attackCoeff_ = std::exp(-1.0f / std::max(1.0f, attackSec * sr));
  releaseCoeff_ = s_.releaseS > 0.0f ? std::exp(-1.0f / std::max(1.0f, s_.releaseS * sr)) : 0.0f;
  hpOn_ = s_.scHpfHz > 0.0f;
  hpAlpha_ = hpOn_ ? 1.0f / (1.0f + kTwoPi * s_.scHpfHz / sr) : 1.0f;

  const float makeup = std::pow(10.0f, s_.makeupDb * 0.05f);
  const float engage = s_.on ? 1.0f : 0.0f;
  if (!primed_) {
    makeup_.snap(makeup);
    mix_.snap(s_.mix);
    engage_.snap(engage);
    primed_ = true;
  } else {
    makeup_.to(makeup, rampSamples_);
    mix_.to(s_.mix, rampSamples_);
    engage_.to(engage, rampSamples_);
  }
}

// Over-easy (soft-knee) gain computer: reduction in dB for a detector level.
float Glue::reduction(float detection) const {
  const float overDb = 20.0f * std::log10(std::max(detection, 1.0e-9f)) - s_.threshDb;
  const float slope = 1.0f - 1.0f / s_.ratio;
  float r;
  if (overDb <= -kBusKneeWidthDb * 0.5f) {
    r = 0.0f;
  } else if (overDb >= kBusKneeWidthDb * 0.5f) {
    r = overDb * slope;
  } else {
    const float x = overDb + kBusKneeWidthDb * 0.5f;
    r = slope * (x * x) / (2.0f * kBusKneeWidthDb);
  }
  return std::min(r, kBusMaxReductionDb);
}

void Glue::process(float* left, float* right, int n) {
  if (engage_.target == 0.0f && engage_.left == 0 && engage_.cur == 0.0f) {
    reset();  // off and settled: identity, and the compressor starts clean when switched on
    return;
  }
  const float sr = static_cast<float>(sampleRate_);
  float mixNow = mix_.cur;
  float engageNow = engage_.cur;
  for (int i = 0; i < n; ++i) {
    const float dry[2] = {left[i], right[i]};
    float in[2], rms[2];
    for (int ch = 0; ch < 2; ++ch) {
      in[ch] = inT_.process(dry[ch], ch);

      // Feedback sidechain: the previous compressed output, high-passed (2 poles) and rectified.
      Detector& d = det_[ch];
      const float fb = d.prevCompressed;
      float rectified;
      if (hpOn_) {
        d.hp1 = hpAlpha_ * (d.hp1 + fb - d.prev1);
        d.prev1 = fb;
        d.hp2 = hpAlpha_ * (d.hp2 + d.hp1 - d.prev2);
        d.prev2 = d.hp1;
        flush(d.hp1);
        flush(d.hp2);
        rectified = std::fabs(d.hp2);
      } else {
        // Filter parked on the signal, so that switching it on later does not step.
        d.hp1 = d.hp2 = 0.0f;
        d.prev1 = fb;
        d.prev2 = 0.0f;
        rectified = std::fabs(fb);
      }
      d.rms = rmsCoeff_ * d.rms + (1.0f - rmsCoeff_) * rectified * rectified;
      flush(d.rms);
      rms[ch] = std::sqrt(std::max(0.0f, d.rms));
    }

    // Linked stereo: both channels see the louder detector, one gain for the pair.
    const float detection = std::max(rms[0], rms[1]);
    const float red = reduction(detection);

    float releaseCoeff = releaseCoeff_;
    if (s_.releaseS <= 0.0f) {  // Auto: faster on transient-dense material, slower on sustained compression
      const float signalDelta = std::fabs(detection - previousLevel_);
      previousLevel_ = previousLevel_ * 0.95f + detection * 0.05f;
      flush(previousLevel_);
      const float transientDensity = std::clamp(signalDelta * 20.0f, 0.0f, 1.0f);
      const float compressionFactor = std::clamp(red / 12.0f, 0.0f, 1.0f);
      const float releaseTime = kAutoReleaseMin +
                                (1.0f - transientDensity) * compressionFactor * (kAutoReleaseMax - kAutoReleaseMin);
      releaseCoeff = std::exp(-1.0f / std::max(1.0f, releaseTime * sr));
    }

    const float targetGain = std::pow(10.0f, -red * 0.05f);
    const float coeff = targetGain < envelope_ ? attackCoeff_ : releaseCoeff;
    envelope_ = targetGain + (envelope_ - targetGain) * coeff;
    if (!std::isfinite(envelope_)) envelope_ = 1.0f;

    const float makeup = makeup_.next();
    mixNow = mix_.next();
    engageNow = engage_.next();

    float wet[2];
    for (int ch = 0; ch < 2; ++ch) {
      const float compressed = in[ch] * envelope_;
      det_[ch].prevCompressed = compressed;  // feedback tap

      // Console harmonics, output transformer, gain compensation, MAKEUP (compressed branch only).
      float p = compressed;
      const float x2 = p * p;
      p += kConsoleK2 * x2 + kConsoleK3 * x2 * p;
      p = outT_.process(p, ch) * hardwareGain_ * makeup;
      wet[ch] = std::clamp(p, -kOutputHardLimit, kOutputHardLimit);
    }

    float* io[2] = {&left[i], &right[i]};
    for (int ch = 0; ch < 2; ++ch) {
      const float blend = wet[ch] * mixNow + dry[ch] * (1.0f - mixNow);
      *io[ch] = engageNow >= 1.0f ? blend : dry[ch] * (1.0f - engageNow) + blend * engageNow;
    }
  }

  const float env = std::min(envelope_, 1.0f);
  grDb_ = env >= 1.0f ? 0.0f : -20.0f * std::log10(std::max(env, 1.0e-4f));
  const float net = engageNow * (mixNow * env + (1.0f - mixNow)) + (1.0f - engageNow);
  netGrDb_ = net >= 1.0f ? 0.0f : -20.0f * std::log10(std::max(net, 1.0e-4f));
  if (engageNow <= 0.0f) grDb_ = 0.0f;
}

float Glue::grDb() const { return grDb_; }
float Glue::netGrDb() const { return netGrDb_; }

} // namespace FairyTapes::HappyEnding::DSP
