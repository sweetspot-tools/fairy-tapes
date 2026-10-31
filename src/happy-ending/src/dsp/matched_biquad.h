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

// ---------------------------------------------------------------------------------------------
// Matched designs (M. Vicanek, "Matched Second Order Digital Filters", 2016): poles from the
// analog poles by the matched z-transform, zeros chosen so that the digital magnitude equals the
// analog one at DC, at Nyquist and at one more frequency. The curve keeps its analog shape up to
// Nyquist: no cramping and, unlike the bandwidth pre-warped computePeaking and the bilinear
// shelves, no widening or sagging near the top of the spectrum.
//
// A boost is designed directly; a cut is the exact reciprocal of the boost of the same |gain|
// (numerator and denominator swapped, renormalised so that a0 = 1), which is also the analog
// prototype of a cut. Degenerate boosts (never met over the whole range of the plugin, checked by
// the tests) fall back to the bilinear design; a degenerate cut stays identity.
// ---------------------------------------------------------------------------------------------
namespace detail {

// Biquad whose poles are the matched image of the analog poles (natural frequency `wpT` in
// radians per sample, damping `zeta`) and whose magnitude matches `analogPower` (|H|^2) at DC
// (1), Nyquist and `wMidT`. Returns false when the matched zeros do not exist.
inline bool matchedFromPoles(BiquadCoeffs& c, double wpT, double zeta, double nyquistPower,
                             double wMidT, double midPower) {
  const double decay = std::exp(-zeta * wpT);
  const double a1 = zeta <= 1.0 ? -2.0 * decay * std::cos(std::sqrt(1.0 - zeta * zeta) * wpT)
                                : -2.0 * decay * std::cosh(std::sqrt(zeta * zeta - 1.0) * wpT);
  const double a2 = decay * decay;

  const double bigA0 = (1.0 + a1 + a2) * (1.0 + a1 + a2);
  const double bigA1 = (1.0 - a1 + a2) * (1.0 - a1 + a2);
  const double bigA2 = -4.0 * a2;
  const double s2 = std::sin(wMidT / 2.0) * std::sin(wMidT / 2.0);
  const double phi0 = 1.0 - s2, phi1 = s2, phi2 = 4.0 * phi0 * phi1;

  const double bigB0 = bigA0;                          // 0 dB at DC
  const double bigB1 = bigA1 * nyquistPower;           // analog gain at Nyquist
  const double bigB2 = (midPower * (bigA0 * phi0 + bigA1 * phi1 + bigA2 * phi2) -
                        bigB0 * phi0 - bigB1 * phi1) / phi2;  // analog gain at wMidT

  const double sqrtB0 = std::sqrt(bigB0), sqrtB1 = std::sqrt(bigB1);
  const double w = 0.5 * (sqrtB0 + sqrtB1);
  const double disc = w * w + bigB2;
  if (!(disc > 0.0) || !std::isfinite(disc)) return false;
  const double b0 = 0.5 * (w + std::sqrt(disc));
  c.b0 = b0;
  c.b1 = 0.5 * (sqrtB0 - sqrtB1);
  c.b2 = -bigB2 / (4.0 * b0);
  c.a1 = a1;
  c.a2 = a2;
  return std::isfinite(c.b1) && std::isfinite(c.b2);
}

// Exact reciprocal: (1 + a1 z^-1 + a2 z^-2) / (b0 + b1 z^-1 + b2 z^-2), renormalised by b0.
inline BiquadCoeffs reciprocal(const BiquadCoeffs& boost) {
  BiquadCoeffs c;
  c.b0 = 1.0 / boost.b0;
  c.b1 = boost.a1 / boost.b0;
  c.b2 = boost.a2 / boost.b0;
  c.a1 = boost.b1 / boost.b0;
  c.a2 = boost.b2 / boost.b0;
  return c;
}

// Analog bell (peaking) boost of amplitude A^2 at fc: poles at w0 with zeta = 1/(2 A Q).
inline bool matchedBoost(BiquadCoeffs& c, double fc, double sr, double gainDb, double q) {
  const double amp = std::pow(10.0, gainDb / 40.0);
  const double w0 = 2.0 * BiquadCoeffs::kPi * fc / sr;
  const double x = 0.5 * sr / fc;  // Nyquist in units of fc
  const double d = 1.0 - x * x;
  const double nyq = (d * d + (x * amp / q) * (x * amp / q)) / (d * d + (x / (amp * q)) * (x / (amp * q)));
  return matchedFromPoles(c, w0, 1.0 / (2.0 * amp * q), nyq, w0, amp * amp * amp * amp);
}

// Analog high shelf A (A s^2 + sqrt(A) s / Q + 1) / (s^2 + sqrt(A) s / Q + A) in units of fc, with
// 1/Q = sqrt((A + 1/A)(1/S - 1) + 2) for the slope S: poles at sqrt(A) fc with zeta = 1/(2Q),
// gain A at fc.
inline bool matchedHighShelfBoost(BiquadCoeffs& c, double fc, double sr, double gainDb, double slope) {
  const double a = std::pow(10.0, gainDb / 40.0);
  const double invQ = std::sqrt((a + 1.0 / a) * (1.0 / slope - 1.0) + 2.0);
  const double w0 = 2.0 * BiquadCoeffs::kPi * fc / sr;
  auto analogPower = [&](double x) {  // |H(j x fc)|^2
    const std::complex<double> s(0.0, x);
    return std::norm(a * (a * s * s + std::sqrt(a) * invQ * s + 1.0) / (s * s + std::sqrt(a) * invQ * s + a));
  };
  // The third match point is the corner, but for a corner high in the spectrum it moves down to
  // 0.58 of Nyquist: a biquad cannot follow the analog curve at both the corner and Nyquist when
  // the two are close, and this keeps the error under 0.3 dB for every corner of the plugin.
  const double wMid = std::min(w0, 0.58 * BiquadCoeffs::kPi);
  return matchedFromPoles(c, std::sqrt(a) * w0, 0.5 * invQ, analogPower(0.5 * sr / fc), wMid,
                          analogPower(wMid / w0));
}

} // namespace detail

inline void computePeakingMatched(BiquadCoeffs& c, double fc, double sr, double gainDb, double q) {
  fc = clampFreq(fc, sr);
  q = std::max(0.01, q);
  if (std::abs(gainDb) < 0.01) { c = BiquadCoeffs{}; return; }

  BiquadCoeffs boost;
  if (!detail::matchedBoost(boost, fc, sr, std::abs(gainDb), q)) {
    if (gainDb > 0.0) computePeaking(c, fc, sr, gainDb, q);
    else c = BiquadCoeffs{};
    return;
  }
  c = gainDb > 0.0 ? boost : detail::reciprocal(boost);
}

// High shelf matched to the analog shelf (slope form, as computeHighShelf).
inline void computeHighShelfMatched(BiquadCoeffs& c, double fc, double sr, double gainDb, double slope) {
  fc = clampFreq(fc, sr);
  slope = std::max(0.01, slope);
  if (std::abs(gainDb) < 0.01) { c = BiquadCoeffs{}; return; }

  BiquadCoeffs boost;
  if (!detail::matchedHighShelfBoost(boost, fc, sr, std::abs(gainDb), slope)) {
    if (gainDb > 0.0) computeHighShelf(c, fc, sr, gainDb, slope);
    else c = BiquadCoeffs{};
    return;
  }
  c = gainDb > 0.0 ? boost : detail::reciprocal(boost);
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

// 2nd-order band-pass, 0 dB at fc (constant peak gain), bandwidth fc / Q. Used by the FINE EQ
// LISTEN to solo a bell. Not part of the Multi-Q port: written for this plugin.
inline void computeBandPass(BiquadCoeffs& c, double fc, double sr, double q) {
  fc = clampFreq(fc, sr);
  q = std::max(0.01, q);
  const double k = std::tan(BiquadCoeffs::kPi * fc / sr);
  const double norm = 1.0 / (k * k + k / q + 1.0);
  c.b0 = k / q * norm;
  c.b1 = 0.0;
  c.b2 = -c.b0;
  c.a1 = 2.0 * (k * k - 1.0) * norm;
  c.a2 = (k * k - k / q + 1.0) * norm;
}

} // namespace matched
} // namespace FairyTapes::HappyEnding::DSP
