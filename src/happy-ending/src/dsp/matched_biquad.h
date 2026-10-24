/**
 * Fairy Tapes · Happy Ending — analog-matched biquad coefficient design, header-only, no JUCE.
 *
 * Ported from AnalogMatchedBiquad.h of Multi-Q (Dusk Audio, GPL-3.0), submodule path
 * vendors/dusk-audio-plugins/plugins/multi-q/AnalogMatchedBiquad.h. Only the peaking, the two
 * shelves and the 2nd-order high pass are kept. Changes: namespace, double precision
 * coefficients (the 20 Hz high pass needs them at 192 kHz), a five-value coefficient struct.
 *
 * Peaking pre-warps the BANDWIDTH (kbw = tan(pi * (fc / Q) / sr)) and places the centre with
 * cos(2 pi fc / sr): exact gain at fc and no cramping near Nyquist, with no oversampling.
 * Shelves and the high pass use k = tan(pi * fc / sr), so the turnover sits exactly at fc.
 *
 * References: S. J. Orfanidis, JAES 53(11), 2005; R. Bristow-Johnson, "Audio EQ Cookbook".
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <complex>

namespace FairyTapes::HappyEnding::DSP {

// Normalized biquad: y = b0 x + b1 x[-1] + b2 x[-2] - a1 y[-1] - a2 y[-2].
struct BiquadCoeffs {
  double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

  static constexpr int kCount = 5;
  double& at(int i) { return (&b0)[i]; }
  double at(int i) const { return (&b0)[i]; }

  bool isIdentity() const { return b0 == 1.0 && b1 == 0.0 && b2 == 0.0 && a1 == 0.0 && a2 == 0.0; }

  // |H(e^jw)| in dB at `hz`.
  double magnitudeDb(double hz, double sr) const {
    const double w = 2.0 * kPi * hz / sr;
    const std::complex<double> z1 = std::polar(1.0, -w), z2 = std::polar(1.0, -2.0 * w);
    const std::complex<double> num = b0 + b1 * z1 + b2 * z2;
    const std::complex<double> den = 1.0 + a1 * z1 + a2 * z2;
    const double d = std::norm(den);
    if (d < 1e-30) return 0.0;
    return 10.0 * std::log10(std::max(std::norm(num), 1e-30) / d);
  }

  static constexpr double kPi = 3.14159265358979323846;
};

namespace matched {

inline double clampFreq(double fc, double sr) { return std::max(1.0, std::min(fc, sr * 0.4998)); }

namespace detail {
inline void store(BiquadCoeffs& c, double b0, double b1, double b2, double a0, double a1, double a2) {
  c.b0 = b0 / a0;
  c.b1 = b1 / a0;
  c.b2 = b2 / a0;
  c.a1 = a1 / a0;
  c.a2 = a2 / a0;
}
} // namespace detail

// Peaking filter, bandwidth = fc / Q Hz at every sample rate.
inline void computePeaking(BiquadCoeffs& c, double fc, double sr, double gainDb, double q) {
  fc = clampFreq(fc, sr);
  q = std::max(0.01, q);
  if (std::abs(gainDb) < 0.01) { c = BiquadCoeffs{}; return; }

  const double kbw = std::tan(BiquadCoeffs::kPi * std::min(fc / q, sr * 0.4998) / sr);
  const double a = std::pow(10.0, gainDb / 40.0);
  const double cosW = std::cos(2.0 * BiquadCoeffs::kPi * fc / sr);
  // H(fc) = A^2 = 10^(gainDb/20) for boosts and cuts alike.
  detail::store(c, 1.0 + kbw * a, -2.0 * cosW, 1.0 - kbw * a, 1.0 + kbw / a, -2.0 * cosW, 1.0 - kbw / a);
}

inline void computeLowShelf(BiquadCoeffs& c, double fc, double sr, double gainDb, double q) {
  fc = clampFreq(fc, sr);
  q = std::max(0.01, q);
  if (std::abs(gainDb) < 0.01) { c = BiquadCoeffs{}; return; }

  const double a = std::pow(10.0, gainDb / 40.0);
  const double k = std::tan(BiquadCoeffs::kPi * fc / sr);
  const double k2 = k * k;
  const double sqA = std::sqrt(a);
  const double cosW = (1.0 - k2) / (1.0 + k2);  // from the pre-warped k: no double warping
  const double sinW = 2.0 * k / (1.0 + k2);
  const double alpha = sinW / 2.0 * std::sqrt((a + 1.0 / a) * (1.0 / q - 1.0) + 2.0);

  detail::store(c,
      a * ((a + 1.0) - (a - 1.0) * cosW + 2.0 * sqA * alpha),
      2.0 * a * ((a - 1.0) - (a + 1.0) * cosW),
      a * ((a + 1.0) - (a - 1.0) * cosW - 2.0 * sqA * alpha),
      (a + 1.0) + (a - 1.0) * cosW + 2.0 * sqA * alpha,
      -2.0 * ((a - 1.0) + (a + 1.0) * cosW),
      (a + 1.0) + (a - 1.0) * cosW - 2.0 * sqA * alpha);
}

inline void computeHighShelf(BiquadCoeffs& c, double fc, double sr, double gainDb, double q) {
  fc = clampFreq(fc, sr);
  q = std::max(0.01, q);
  if (std::abs(gainDb) < 0.01) { c = BiquadCoeffs{}; return; }

  const double a = std::pow(10.0, gainDb / 40.0);
  const double sqA = std::sqrt(a);
  const double k = std::tan(BiquadCoeffs::kPi * fc / sr);
  const double k2 = k * k;
  const double cosW = (1.0 - k2) / (1.0 + k2);
  const double sinW = 2.0 * k / (1.0 + k2);
  const double alpha = sinW / 2.0 * std::sqrt((a + 1.0 / a) * (1.0 / q - 1.0) + 2.0);

  detail::store(c,
      a * ((a + 1.0) + (a - 1.0) * cosW + 2.0 * sqA * alpha),
      -2.0 * a * ((a - 1.0) + (a + 1.0) * cosW),
      a * ((a + 1.0) + (a - 1.0) * cosW - 2.0 * sqA * alpha),
      (a + 1.0) - (a - 1.0) * cosW + 2.0 * sqA * alpha,
      2.0 * ((a - 1.0) - (a + 1.0) * cosW),
      (a + 1.0) - (a - 1.0) * cosW - 2.0 * sqA * alpha);
}

// 2nd-order high pass (12 dB/octave), -3 dB point at fc for Q = 1/sqrt(2).
inline void computeHighPass(BiquadCoeffs& c, double fc, double sr, double q) {
  fc = clampFreq(fc, sr);
  q = std::max(0.01, q);
  const double k = std::tan(BiquadCoeffs::kPi * fc / sr);
  const double norm = 1.0 / (k * k + k / q + 1.0);
  c.b0 = norm;
  c.b1 = -2.0 * norm;
  c.b2 = norm;
  c.a1 = 2.0 * (k * k - 1.0) * norm;
  c.a2 = (k * k - k / q + 1.0) * norm;
}

} // namespace matched
} // namespace FairyTapes::HappyEnding::DSP
