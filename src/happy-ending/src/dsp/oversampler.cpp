/**
 * Fairy Tapes · Happy Ending — linear-phase polyphase oversampler. See oversampler.h.
 *
 * Half-band design: h[n] = sin(pi n / 2) / (pi n) windowed by a Kaiser window, on 4k + 3 taps
 * centred at c = 2k + 1. Every tap at an even offset from the centre is zero, the centre is 0.5.
 * The taps at odd offsets (even indices) are normalised to sum to 0.5, so the DC gain is exactly 1.
 * The tap count follows the Kaiser rule N ~ (A - 7.95) / (14.36 * df) for stopband attenuation A
 * and transition width df (in cycles per sample at the filter's rate), rounded up to 4k + 3.
 * With the tuning values (A = 100 dB, passband edge 0.4535 fs = 20 kHz at 44.1 kHz) the 2x step has 139 taps and the
 * second 2x step of the 4x chain 27.
 */

#include "dsp/oversampler.h"
#include "tuning.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace FairyTapes::HappyEnding::DSP {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Modified Bessel function of the first kind, order 0 (power series).
double besselI0(double x) {
  double sum = 1.0, term = 1.0;
  const double q = 0.25 * x * x;
  for (int k = 1; k < 200; ++k) {
    term *= q / (static_cast<double>(k) * k);
    sum += term;
    if (term < 1e-18 * sum) break;
  }
  return sum;
}

} // namespace

void Oversampler::Stage::design(double passbandEdgeAtHighRate, double stopbandDb) {
  // Half-band: the stopband edge mirrors the passband edge around 0.25 (cycles per high-rate sample).
  const double df = 0.5 - 2.0 * passbandEdgeAtHighRate;
  const double taps = (stopbandDb - 7.95) / (14.36 * df) + 1.0;
  k = std::max(1, static_cast<int>(std::ceil((taps - 3.0) / 4.0)));
  const int c = 2 * k + 1;
  const int total = 4 * k + 3;
  const double beta = stopbandDb > 50.0 ? 0.1102 * (stopbandDb - 8.7)
                                        : 0.5842 * std::pow(stopbandDb - 21.0, 0.4) + 0.07886 * (stopbandDb - 21.0);
  const double i0Beta = besselI0(beta);

  std::vector<double> h(static_cast<std::size_t>(total), 0.0);
  double side = 0.0;
  for (int i = 0; i < total; ++i) {
    const int d = i - c;
    if (d == 0 || d % 2 == 0) continue; // centre set below, even offsets are exactly zero
    const double r = static_cast<double>(d) / c;
    const double w = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0Beta;
    h[static_cast<std::size_t>(i)] = std::sin(kPi * d / 2.0) / (kPi * d) * w;
    side += h[static_cast<std::size_t>(i)];
  }
  const double norm = 0.5 / side;
  even.assign(static_cast<std::size_t>(2 * k + 2), 0.0f);
  for (int m = 0; m < 2 * k + 2; ++m)
    even[static_cast<std::size_t>(m)] = static_cast<float>(h[static_cast<std::size_t>(2 * m)] * norm);
}

void Oversampler::Stage::clear() {
  std::fill(history.begin(), history.end(), 0.0f);
}

void Oversampler::prepare(int factor, int maxBlock) {
  factor_ = (factor == 2 || factor == 4) ? factor : 1;
  maxBlock_ = std::max(1, maxBlock);
  extraDelayState_ = 0.0f;
  up1_ = Stage{};
  up2_ = Stage{};
  down1_ = Stage{};
  down2_ = Stage{};
  tmp_.clear();
  latency_ = 0;
  if (factor_ == 1) return;

  const double passEdge = Tuning::kOversamplerPassbandEdge; // fraction of the base rate
  const double db = Tuning::kOversamplerStopbandDb;

  // Stage 1 runs base -> 2x; its input chunk is maxBlock, its decimator input 2 * maxBlock.
  up1_.design(passEdge / 2.0, db);
  down1_.k = up1_.k;
  down1_.even = up1_.even;
  up1_.history.assign(static_cast<std::size_t>(up1_.delay() + maxBlock_), 0.0f);
  down1_.history.assign(static_cast<std::size_t>(4 * down1_.k + 2 + 2 * maxBlock_), 0.0f);
  latency_ = up1_.delay();

  if (factor_ == 4) {
    up2_.design(passEdge / 4.0, db);
    down2_.k = up2_.k;
    down2_.even = up2_.even;
    up2_.history.assign(static_cast<std::size_t>(up2_.delay() + 2 * maxBlock_), 0.0f);
    down2_.history.assign(static_cast<std::size_t>(4 * down2_.k + 2 + 4 * maxBlock_), 0.0f);
    tmp_.assign(static_cast<std::size_t>(2 * maxBlock_), 0.0f);
    // Inner pair: 2 * c2 samples at 4x = c2 samples at 2x; plus the extra sample at 2x makes
    // c2 + 1 (even) samples at 2x = (c2 + 1) / 2 = k2 + 1 samples at the base rate.
    latency_ += up2_.k + 1;
  }
}

void Oversampler::reset() {
  up1_.clear();
  up2_.clear();
  down1_.clear();
  down2_.clear();
  extraDelayState_ = 0.0f;
}

// Interpolating half-band step: n input samples -> 2n output samples (gain 2 folded in).
void Oversampler::upStage(Stage& s, const float* in, int n, float* out) {
  const int hist = s.delay();      // 2k + 1 past samples
  const int taps = 2 * s.k + 2;
  float* buf = s.history.data();
  std::memcpy(buf + hist, in, sizeof(float) * static_cast<std::size_t>(n));
  const float* e = s.even.data();
  for (int i = 0; i < n; ++i) {
    const float* x = buf + hist + i;
    float acc = 0.0f;
    for (int m = 0; m < taps; ++m) acc += e[m] * x[-m];
    out[2 * i] = 2.0f * acc;
    out[2 * i + 1] = x[-s.k]; // the only odd-phase tap is the centre: a pure delay of k samples
  }
  std::memmove(buf, buf + n, sizeof(float) * static_cast<std::size_t>(hist));
}

// Decimating half-band step: nUp (even) input samples -> nUp / 2 output samples.
void Oversampler::downStage(Stage& s, const float* in, int nUp, float* out) {
  const int hist = 4 * s.k + 2;   // N - 1 past samples
  const int c = s.delay();
  const int taps = 2 * s.k + 2;
  float* buf = s.history.data();
  std::memcpy(buf + hist, in, sizeof(float) * static_cast<std::size_t>(nUp));
  const float* e = s.even.data();
  const int nOut = nUp / 2;
  for (int i = 0; i < nOut; ++i) {
    const float* v = buf + hist + 2 * i;
    float acc = 0.5f * v[-c];
    for (int m = 0; m < taps; ++m) acc += e[m] * v[-2 * m];
    out[i] = acc;
  }
  std::memmove(buf, buf + nUp, sizeof(float) * static_cast<std::size_t>(hist));
}

int Oversampler::up(const float* in, int n, float* out) {
  if (n <= 0) return 0;
  if (factor_ == 1) {
    if (out != in) std::memcpy(out, in, sizeof(float) * static_cast<std::size_t>(n));
    return n;
  }
  for (int pos = 0; pos < n; pos += maxBlock_) {
    const int len = std::min(maxBlock_, n - pos);
    const float* src = in + pos;
    float* dst = out + static_cast<std::size_t>(pos) * factor_;
    if (factor_ == 2) {
      upStage(up1_, src, len, dst);
    } else {
      upStage(up1_, src, len, tmp_.data());
      upStage(up2_, tmp_.data(), 2 * len, dst);
    }
  }
  return n * factor_;
}

void Oversampler::down(const float* in, int nUp, float* out) {
  if (nUp <= 0) return;
  if (factor_ == 1) {
    if (out != in) std::memcpy(out, in, sizeof(float) * static_cast<std::size_t>(nUp));
    return;
  }
  const int chunk = maxBlock_ * factor_;
  for (int pos = 0; pos < nUp; pos += chunk) {
    const int len = std::min(chunk, nUp - pos);
    const float* src = in + pos;
    float* dst = out + pos / factor_;
    if (factor_ == 2) {
      downStage(down1_, src, len, dst);
    } else {
      downStage(down2_, src, len, tmp_.data());
      // One extra sample of delay at 2x: keeps the round trip an exact integer at the base rate.
      float prev = extraDelayState_;
      for (int i = 0; i < len / 2; ++i) {
        const float cur = tmp_[static_cast<std::size_t>(i)];
        tmp_[static_cast<std::size_t>(i)] = prev;
        prev = cur;
      }
      extraDelayState_ = prev;
      downStage(down1_, tmp_.data(), len / 2, dst);
    }
  }
}

} // namespace FairyTapes::HappyEnding::DSP
