// Fairy Tapes · Deep Throat — RBJ cookbook biquad (transposed direct form II, double state).
// High-pass, peaking and high-shelf. Header-only, no VST3 SDK dependency. Setters compute
// coefficients only (they keep the filter state, so they can be called while running).
#pragma once

#include <cmath>

namespace FairyTapes::DeepThroat::DSP {

struct Biquad {
  double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
  double z1 = 0.0, z2 = 0.0;

  void setHighPass(double sr, double hz, double q) {
    const double w = omega(sr, hz), c = std::cos(w), al = std::sin(w) / (2.0 * q);
    set((1.0 + c) / 2.0, -(1.0 + c), (1.0 + c) / 2.0, 1.0 + al, -2.0 * c, 1.0 - al);
  }

  void setPeaking(double sr, double hz, double q, double gainDb) {
    const double w = omega(sr, hz), c = std::cos(w), al = std::sin(w) / (2.0 * q);
    const double A = std::pow(10.0, gainDb / 40.0);
    set(1.0 + al * A, -2.0 * c, 1.0 - al * A, 1.0 + al / A, -2.0 * c, 1.0 - al / A);
  }

  // Shelf slope S = 1.
  void setHighShelf(double sr, double hz, double gainDb) {
    const double w = omega(sr, hz), c = std::cos(w), s = std::sin(w);
    const double A = std::pow(10.0, gainDb / 40.0);
    const double al = s / 2.0 * std::sqrt(2.0);
    const double t = 2.0 * std::sqrt(A) * al;
    set(A * ((A + 1.0) + (A - 1.0) * c + t), -2.0 * A * ((A - 1.0) + (A + 1.0) * c),
        A * ((A + 1.0) + (A - 1.0) * c - t), (A + 1.0) - (A - 1.0) * c + t,
        2.0 * ((A - 1.0) - (A + 1.0) * c), (A + 1.0) - (A - 1.0) * c - t);
  }

  float process(float x) {
    const double in = x;
    const double out = b0 * in + z1;
    z1 = b1 * in - a1 * out + z2;
    z2 = b2 * in - a2 * out;
    // Flush denormal state so a decaying tail never slows the audio thread.
    if (std::fabs(z1) < 1e-30) z1 = 0.0;
    if (std::fabs(z2) < 1e-30) z2 = 0.0;
    return static_cast<float>(out);
  }

  void reset() { z1 = z2 = 0.0; }

private:
  static double omega(double sr, double hz) {
    const double f = hz < 1.0 ? 1.0 : (hz > 0.49 * sr ? 0.49 * sr : hz);
    return 2.0 * 3.14159265358979323846 * f / sr;
  }
  void set(double nb0, double nb1, double nb2, double na0, double na1, double na2) {
    b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0;
    a1 = na1 / na0; a2 = na2 / na0;
  }
};

} // namespace FairyTapes::DeepThroat::DSP
