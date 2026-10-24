#pragma once
#include <cmath>

namespace FairyTapes::Hotline::DSP {

/**
 * Asymmetric tanh waveshaper, drive 0..1. y = tanh(k(x+b)) - tanh(kb), k = 1 + 9*drive,
 * b = 0.1*drive. Normalised by the largest possible excursion (1 + tanh(kb), reached on the
 * negative side) so |y| <= 1 for any input. Exact identity at drive == 0.
 */
inline float shape(float x, float drive) {
  if (drive <= 0.f) return x;
  const float k = 1.f + 9.f * drive, b = 0.1f * drive;
  const float off = std::tanh(k * b);
  return (std::tanh(k * (x + b)) - off) / (1.f + off);
}

}  // namespace FairyTapes::Hotline::DSP
