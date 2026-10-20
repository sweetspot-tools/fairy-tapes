#include "dsp/corrective_eq.h"

#include <algorithm>
#include <cmath>

namespace FairyTapes::DeepThroat::DSP {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kHpfQ = 0.70710678118654752;  // 12 dB/oct Butterworth
constexpr double kMudHz = 300.0;
constexpr double kMudQ = 1.0;
constexpr double kNotchQ = 8.0;
constexpr double kNotchLoHz = 1000.0;
constexpr double kNotchHiHz = 6000.0;
constexpr double kProminenceDb = 8.0;
constexpr double kPersistBins = 1.5;
constexpr double kPersistSec = 0.3;
constexpr int kRingSize = 1 << 16;  // per-channel analysis history (power of two)
constexpr double kSmoothMs = 20.0;
constexpr double kNotchAttackMs = 30.0;
constexpr double kNotchReleaseMs = 200.0;

double coeffFor(double ms, double dtSec) { return 1.0 - std::exp(-dtSec / (ms * 0.001)); }
}  // namespace

void CorrectiveEq::prepare(double sampleRate, int channels) {
  sr_ = sampleRate > 0.0 ? sampleRate : 48000.0;
  const int n = std::max(1, channels);
  ch_.assign(n, Channel{});
  for (auto& c : ch_) c.ring.assign(kRingSize, 0.0f);
  hop_ = std::max<long long>(1, std::llround(0.05 * sr_));
  fftSize_ = nextPow2((int)std::ceil(0.04 * sr_));
  fft_.prepare(fftSize_);
  window_.assign(fftSize_, 0.0f);
  for (int i = 0; i < fftSize_; ++i)
    window_[i] = (float)(0.5 - 0.5 * std::cos(2.0 * kPi * i / fftSize_));
  re_.assign(fftSize_, 0.0f);
  im_.assign(fftSize_, 0.0f);
  power_.assign(fftSize_ / 2 + 1, 0.0f);
  const double dt = refreshInterval_ / sr_;
  smoothCoeff_ = coeffFor(kSmoothMs, dt);
  attackCoeff_ = coeffFor(kNotchAttackMs, dt);
  releaseCoeff_ = coeffFor(kNotchReleaseMs, dt);
  updateTargets();
  reset();
}

void CorrectiveEq::reset() {
  notchTargetDb_ = 0.0;
  persistCount_ = 0;
  for (auto& c : ch_) {
    c.hpf.reset(); c.mud.reset(); c.notch.reset();
    c.counter = 0;
    c.samples = 0;
    std::fill(c.ring.begin(), c.ring.end(), 0.0f);
    snap(c);
  }
}

void CorrectiveEq::setMode(Mode mode) { mode_ = mode; updateTargets(); }

void CorrectiveEq::setAmount(float amount01) {
  amount_ = std::isfinite(amount01) ? std::clamp(amount01, 0.0f, 1.0f) : 0.5f;
  updateTargets();
}

void CorrectiveEq::updateTargets() {
  double hpf = 80.0, mud = -2.0;
  switch (mode_) {
    case Mode::Speak: hpf = 80.0; mud = -2.0; break;
    case Mode::Sing:  hpf = 70.0; mud = -3.0; break;
    case Mode::Puke:  hpf = 60.0; mud = -4.0; break;
  }
  targetHpfHz_ = hpf * (0.5 + amount_);
  targetMudDb_ = mud * 2.0 * amount_;
  maxNotchDb_ = 6.0 * 2.0 * amount_;
}

void CorrectiveEq::snap(Channel& c) {
  c.hpfHz = targetHpfHz_;
  c.mudDb = targetMudDb_;
  c.notchHz = notchTargetHz_;
  c.notchDb = 0.0;
  c.hpf.setHighPass(sr_, c.hpfHz, kHpfQ);
  c.mud.setPeaking(sr_, kMudHz, kMudQ, c.mudDb);
  c.notch.setPeaking(sr_, c.notchHz, kNotchQ, 0.0);
}

// Moves the applied values toward their targets and recomputes the coefficients.
void CorrectiveEq::refresh(Channel& c) {
  // Frequencies glide in the log domain; gains in dB.
  const double lf = std::log(c.hpfHz), lt = std::log(targetHpfHz_);
  c.hpfHz = std::fabs(lt - lf) < 1e-4 ? targetHpfHz_ : std::exp(lf + (lt - lf) * smoothCoeff_);
  c.mudDb = std::fabs(targetMudDb_ - c.mudDb) < 1e-3
                ? targetMudDb_ : c.mudDb + (targetMudDb_ - c.mudDb) * smoothCoeff_;
  c.hpf.setHighPass(sr_, c.hpfHz, kHpfQ);
  c.mud.setPeaking(sr_, kMudHz, kMudQ, c.mudDb);

  const double goal = std::min(notchTargetDb_, maxNotchDb_);  // max follows Amount at once...
  const double k = goal > c.notchDb ? attackCoeff_ : releaseCoeff_;
  c.notchDb = std::fabs(goal - c.notchDb) < 1e-3 ? goal : c.notchDb + (goal - c.notchDb) * k;
  // ...but the applied depth is smoothed, so there is no step. A retarget while the notch is
  // (nearly) inaudible jumps; otherwise it glides.
  if (c.notchDb < 0.05) {
    c.notchHz = notchTargetHz_;
  } else {
    const double a = std::log(c.notchHz), b = std::log(notchTargetHz_);
    c.notchHz = std::exp(a + (b - a) * attackCoeff_);
  }
  c.notch.setPeaking(sr_, c.notchHz, kNotchQ, -c.notchDb);
}

float CorrectiveEq::process(int channel, float x) {
  if (channel < 0 || channel >= (int)ch_.size()) return x;
  const int idx = channel;
  Channel& c = ch_[idx];
  if (!std::isfinite(x)) x = 0.0f;

  c.ring[(size_t)(c.samples & (kRingSize - 1))] = x;
  if (++c.counter >= refreshInterval_) {
    c.counter = 0;
    refresh(c);
  }
  float y = c.hpf.process(x);
  y = c.mud.process(y);
  y = c.notch.process(y);

  ++c.samples;
  // The last channel finishing sample n triggers the analysis; every channel consumes the
  // result from sample n + 1, so identical channels stay identical.
  if (idx == (int)ch_.size() - 1 && c.samples % hop_ == 0) analyze();
  return y;
}

void CorrectiveEq::analyze() {
  const long long end = ch_.back().samples;  // exclusive
  if (end < fftSize_) return;
  const float inv = 1.0f / (float)ch_.size();
  for (int i = 0; i < fftSize_; ++i) {
    const size_t pos = (size_t)((end - fftSize_ + i) & (kRingSize - 1));
    float s = 0.0f;
    for (const auto& c : ch_) s += c.ring[pos];
    re_[i] = s * inv * window_[i];
    im_[i] = 0.0f;
  }
  fft_.forward(re_.data(), im_.data());
  const int half = fftSize_ / 2;
  for (int k = 0; k <= half; ++k) power_[k] = re_[k] * re_[k] + im_[k] * im_[k];

  const double binHz = sr_ / fftSize_;
  const int lo = std::max(2, (int)std::ceil(kNotchLoHz / binHz));
  const int hi = std::min(half - 1, (int)std::floor(kNotchHiHz / binHz));
  // Among the local maxima in range, take the one that stands out most from its neighbours
  // (mean power 5..12 bins either side), not the loudest.
  int best = -1;
  double bestProm = kProminenceDb;
  for (int k = lo; k <= hi; ++k) {
    if (power_[k] < power_[k - 1] || power_[k] < power_[k + 1]) continue;  // local maxima only
    double sum = 0.0;
    int cnt = 0;
    for (int d = 5; d <= 12; ++d) {
      if (k - d >= 1) { sum += power_[k - d]; ++cnt; }
      if (k + d <= half) { sum += power_[k + d]; ++cnt; }
    }
    if (cnt == 0 || sum <= 0.0) continue;
    const double mean = sum / cnt;
    if (mean < 1e-14) continue;  // silence
    const double sPeak = (power_[k - 1] + power_[k] + power_[k + 1]) / 3.0;
    const double prom = 10.0 * std::log10(sPeak / mean);
    if (prom > bestProm) { bestProm = prom; best = k; }
  }
  notchTargetDb_ = 0.0;
  if (best < 0) { persistCount_ = 0; return; }

  const double w0 = power_[best - 1], w1 = power_[best], w2 = power_[best + 1];
  const double centroidBin = ((best - 1) * w0 + best * w1 + (best + 1) * w2) / (w0 + w1 + w2);
  // Persistence: the winning peak must stay within +-1.5 bins of where its run started for
  // 300 ms of consecutive analyses, so moving harmonics (vibrato, pitch) never engage the notch.
  if (persistCount_ == 0 || std::fabs(centroidBin - persistBin_) > kPersistBins) {
    persistCount_ = 1;
    persistBin_ = centroidBin;
  } else {
    ++persistCount_;
  }
  if (persistCount_ * hop_ < (long long)std::llround(kPersistSec * sr_)) return;
  const double centroid = centroidBin;
  notchTargetHz_ = std::clamp(centroid * binHz, kNotchLoHz, kNotchHiHz);
  // Depth grows with the prominence above the threshold; the cap (6 dB x 2 x Amount) is
  // applied per channel in refresh().
  notchTargetDb_ = std::clamp(bestProm - kProminenceDb, 0.0, 6.0 * 2.0);
}

}  // namespace FairyTapes::DeepThroat::DSP
