#pragma once
#include <cmath>

namespace FairyTapes::PillowTalk::DSP {

/** Transposed direct form II biquad; flushes tiny state to 0 (no denormals). */
struct Biquad {
  float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f;
  float z1 = 0.f, z2 = 0.f;
  float process(float x) {
    const float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    if (std::fabs(z1) < 1e-20f) z1 = 0.f;
    if (std::fabs(z2) < 1e-20f) z2 = 0.f;
    return y;
  }
  void clear() { z1 = z2 = 0.f; }
};

// RBJ cookbook designs. Coefficients only; filter state is left untouched.
inline void designHighpass(Biquad& b, double fs, double fc, double q) {
  constexpr double kPi = 3.14159265358979323846;
  const double w = 2.0 * kPi * fc / fs, c = std::cos(w), al = std::sin(w) / (2.0 * q);
  const double a0 = 1.0 + al;
  b.b0 = (float)(((1.0 + c) / 2.0) / a0);
  b.b1 = (float)(-(1.0 + c) / a0);
  b.b2 = b.b0;
  b.a1 = (float)(-2.0 * c / a0);
  b.a2 = (float)((1.0 - al) / a0);
}

inline void designLowpass(Biquad& b, double fs, double fc, double q) {
  constexpr double kPi = 3.14159265358979323846;
  const double w = 2.0 * kPi * fc / fs, c = std::cos(w), al = std::sin(w) / (2.0 * q);
  const double a0 = 1.0 + al;
  b.b0 = (float)(((1.0 - c) / 2.0) / a0);
  b.b1 = (float)((1.0 - c) / a0);
  b.b2 = b.b0;
  b.a1 = (float)(-2.0 * c / a0);
  b.a2 = (float)((1.0 - al) / a0);
}

} // namespace FairyTapes::PillowTalk::DSP
