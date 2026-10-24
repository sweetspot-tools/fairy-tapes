#pragma once
#include <algorithm>
#include <cmath>

namespace FairyTapes::Hotline::DSP {

/**
 * Asymmetric tanh waveshaper, drive 0..1. y = tanh(k(x+b)) - tanh(kb), k = 1 + 9*drive,
 * b = 0.1*drive. Normalised by the largest possible excursion (1 + tanh(kb), reached on the
 * negative side) so |y| <= 1 for any input. Cross-faded from the dry signal over drive 0..0.1
 * (w = min(1, drive / kFullBlendDrive), written as drive * kBlendSlope) so there is no level step as drive leaves 0; exact identity at drive <= 0.
 */
// Drive from which the cross-fade is complete (w == 1): the shaper is fully engaged.
constexpr float kFullBlendDrive = 0.1f;
// w = drive / kFullBlendDrive as a multiplication; the assert pins that it is exactly the 10.f used before.
constexpr float kBlendSlope = 1.f / kFullBlendDrive;
static_assert(kBlendSlope == 10.f, "drive blend slope must stay exactly 10");

inline float shape(float x, float drive) {
  if (drive <= 0.f) return x;
  const float k = 1.f + 9.f * drive, b = 0.1f * drive;
  const float off = std::tanh(k * b);
  const float shaped = (std::tanh(k * (x + b)) - off) / (1.f + off);
  const float w = std::min(1.f, drive * kBlendSlope);
  return x + w * (shaped - x);
}

}  // namespace FairyTapes::Hotline::DSP
