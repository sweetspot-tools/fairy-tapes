#include "dsp/distressor_comp.h"

#include <algorithm>
#include <cmath>

namespace FairyTapes::DeepThroat::DSP {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kButterworthQ = 0.70710678118654752;
constexpr double kKneeDb = 6.0;
constexpr double kRefPeakDb = -15.0;      // makeup reference: -18 dBFS RMS sine
constexpr double kMakeupBelowRefDb = 2.0; // makeup leaves the reference 2 dB below the input
constexpr double kFastReleaseMs = 50.0;
constexpr double kEnvMs = 100.0;          // crest-factor envelopes (peak release, RMS window)
constexpr double kCrestSlowDb = 6.0, kCrestFastDb = 12.0;
constexpr double kSmoothMs = 20.0;        // threshold, makeup, mix, distortion crossfade
constexpr double kDistFullGrDb = 12.0;
constexpr double kAttackFastMs = 0.5, kAttackSlowMs = 10.0;
// Tuned so 12 dB of reduction on a -12 dBFS 1 kHz sine gives ~3 % 2nd harmonic (Sing, Dist 2)
// and ~2.4 % 3rd harmonic with Dist 3 (Puke, attack factor 1).
constexpr double kK2 = 0.19;
constexpr double kK3 = 1.0;
constexpr double kShaperLimit = 2.0;      // shaper input clamp (keeps huge inputs finite)
constexpr double kDist3HpHz = 30.0, kDist3LpHz = 12000.0;
constexpr double kDcBlockHz = 10.0;

double coeffFor(double ms, double sr) { return 1.0 - std::exp(-1.0 / (ms * 0.001 * sr)); }

// Static gain reduction (dB, >= 0) of the soft-knee gain computer.
double staticGrDb(double xDb, double thresholdDb, double ratio) {
  const double over = xDb - thresholdDb;
  const double slope = 1.0 - 1.0 / ratio;
  if (2.0 * over <= -kKneeDb) return 0.0;
  if (2.0 * over >= kKneeDb) return over * slope;
  const double t = over + kKneeDb / 2.0;
  return slope * t * t / (2.0 * kKneeDb);
}

double modeRatio(Mode m) { return m == Mode::Puke ? 8.0 : (m == Mode::Sing ? 3.0 : 2.0); }
int modeDist(Mode m) { return m == Mode::Puke ? 3 : (m == Mode::Sing ? 2 : 1); }

// Identity up to 0 dBFS, then a tanh knee that never exceeds 2 (C1-continuous at 1). Only the
// onset of a loud transient after silence with high makeup gets here (no lookahead).
double safetyClip(double y) {
  const double a = std::fabs(y);
  return a <= 1.0 ? y : std::copysign(1.0 + std::tanh(a - 1.0), y);
}

void glide(double& v, double target, double k) {
  v += (target - v) * k;
  if (std::fabs(target - v) < 1e-7) v = target;
}
}  // namespace

void DistressorComp::prepare(double sampleRate, int channels) {
  sr_ = sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate : 48000.0;
  ch_.assign((size_t)std::max(1, channels), Channel{});
  smoothCoeff_ = coeffFor(kSmoothMs, sr_);
  envCoeff_ = coeffFor(kEnvMs, sr_);
  dcCoeff_ = 1.0 - 2.0 * kPi * kDcBlockHz / sr_;
  for (auto& c : ch_) {
    c.hp3.setHighPass(sr_, kDist3HpHz, kButterworthQ);
    c.lp3.setLowPass(sr_, std::min(kDist3LpHz, 0.45 * sr_), kButterworthQ);
  }
  setDetectorHpf(hpfChoice_);
  updateTargets();
  reset();
}

void DistressorComp::reset() {
  for (auto& c : ch_) {
    c.det.reset(); c.hp3.reset(); c.lp3.reset();
    c.dcX = c.dcY = 0.0;
  }
  peakEnv_ = msEnv_ = gr_ = 0.0;
  threshold_ = thresholdTarget_;
  makeup_ = makeupTarget_;
  mixS_ = mix_;
  w2_ = w2Target_;
  w3_ = w3Target_;
}

void DistressorComp::setMode(Mode mode) { mode_ = mode; updateTargets(); }

void DistressorComp::setAmount(float amount01) {
  amount_ = std::isfinite(amount01) ? std::clamp(amount01, 0.0f, 1.0f) : 0.5f;
  updateTargets();
}

void DistressorComp::setMix(float mix01) {
  mix_ = std::isfinite(mix01) ? std::clamp(mix01, 0.0f, 1.0f) : 1.0f;
}

void DistressorComp::setRatioChoice(int c) {
  ratioChoice_ = std::clamp(c, 0, kCompRatioCount - 1);
  updateTargets();
}

void DistressorComp::setDistChoice(int c) {
  distChoice_ = std::clamp(c, 0, kCompDistCount - 1);
  updateTargets();
}

void DistressorComp::setDetectorHpf(int c) {
  hpfChoice_ = std::clamp(c, 0, kCompHpfCount - 1);
  const double hz = compHpfHzFromChoice(hpfChoice_);
  if (hz > 0.0)
    for (auto& ch : ch_) ch.det.setHighPass(sr_, hz, kButterworthQ);
}

void DistressorComp::updateTargets() {
  ratio_ = ratioChoice_ == 0 ? modeRatio(mode_) : compRatioFromChoice(ratioChoice_);
  thresholdTarget_ = -6.0 - 24.0 * amount_;
  makeupTarget_ =
      std::max(0.0, staticGrDb(kRefPeakDb, thresholdTarget_, ratio_) - kMakeupBelowRefDb);

  const double attackMs = std::max(0.05, 6.0 / ratio_ * (mode_ == Mode::Puke ? 0.5 : 1.0));
  const double slowMs = std::clamp(100.0 * ratio_, 200.0, 3500.0);
  attackCoeff_ = coeffFor(attackMs, sr_);
  fastCoeff_ = coeffFor(kFastReleaseMs, sr_);
  slowCoeff_ = coeffFor(slowMs, sr_);
  // 1 at <= 0.5 ms, 0 at >= 10 ms, linear in log(time) between.
  attackFactor_ = std::clamp(std::log(kAttackSlowMs / attackMs) /
                             std::log(kAttackSlowMs / kAttackFastMs), 0.0, 1.0);

  const int dist = distChoice_ == 0 ? modeDist(mode_) : distChoice_;
  w2Target_ = dist == 2 ? 1.0 : 0.0;
  w3Target_ = dist == 3 ? 1.0 : 0.0;
}

void DistressorComp::processFrame(float* samples) {
  const int n = (int)ch_.size();
  const bool hpfOn = hpfChoice_ != 0;

  // Detector: per-channel high-pass, linked peak.
  double d = 0.0;
  for (int i = 0; i < n; ++i) {
    float x = samples[i];
    if (!std::isfinite(x)) x = 0.0f;
    samples[i] = x;
    const float h = ch_[(size_t)i].det.process(x);
    d = std::max(d, (double)std::fabs(hpfOn ? h : x));
  }

  glide(threshold_, thresholdTarget_, smoothCoeff_);
  glide(makeup_, makeupTarget_, smoothCoeff_);
  glide(mixS_, (double)mix_, smoothCoeff_);
  glide(w2_, w2Target_, smoothCoeff_);
  glide(w3_, w3Target_, smoothCoeff_);

  // Crest factor -> sustain weight for the release.
  peakEnv_ = d > peakEnv_ ? d : peakEnv_ * (1.0 - envCoeff_);
  msEnv_ += (d * d - msEnv_) * envCoeff_;
  if (peakEnv_ < 1e-15) peakEnv_ = 0.0;
  if (msEnv_ < 1e-30) msEnv_ = 0.0;
  const double crestDb = 20.0 * std::log10(std::max(peakEnv_, 1e-10)) -
                         10.0 * std::log10(std::max(msEnv_, 1e-20));
  const double sustain =
      std::clamp((kCrestFastDb - crestDb) / (kCrestFastDb - kCrestSlowDb), 0.0, 1.0);

  // Gain computer, then smoothing of the reduction in dB.
  const double xDb = 20.0 * std::log10(std::max(d, 1e-10));
  const double target = staticGrDb(xDb, threshold_, ratio_);
  const double k =
      target > gr_ ? attackCoeff_ : fastCoeff_ + (slowCoeff_ - fastCoeff_) * sustain;
  gr_ += (target - gr_) * k;
  if (gr_ < 1e-6 && target <= 0.0) gr_ = 0.0;

  const double gain = gr_ > 0.0 || makeup_ > 0.0 ? dbToGain(makeup_ - gr_) : 1.0;
  const double drive = std::min(gr_ / kDistFullGrDb, 1.0) * (1.0 + 0.5 * attackFactor_);
  const double k2 = kK2 * drive, k3 = kK3 * drive;
  const double mix = mixS_;

  for (int i = 0; i < n; ++i) {
    Channel& c = ch_[(size_t)i];
    const double x = samples[i];
    const double xs = std::clamp(x, -kShaperLimit, kShaperLimit);
    // Dist 2: DC-blocked even term.
    const double e = k2 * xs * xs;
    c.dcY = e - c.dcX + dcCoeff_ * c.dcY;
    c.dcX = e;
    if (std::fabs(c.dcY) < 1e-30) c.dcY = 0.0;
    // Dist 3: band-limited odd shaper.
    const double v = std::clamp((double)c.hp3.process((float)x), -kShaperLimit, kShaperLimit);
    const double d3 = c.lp3.process((float)(v + k3 * v * v * v));
    const double shaped = x + w2_ * c.dcY + w3_ * (d3 - x);
    const double wet = safetyClip(shaped * gain);
    samples[i] = (float)(x * (1.0 - mix) + wet * mix);
  }
}

}  // namespace FairyTapes::DeepThroat::DSP
