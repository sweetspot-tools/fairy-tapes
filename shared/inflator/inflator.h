/**
 * Fairy Tapes · shared Inflator DSP: waveshaper maths, header-only, no JUCE.
 *
 * Shaper curves, mixer and wet/dry ported from ZLInflator (zsliu98, GPL-3.0):
 * https://github.com/ZL-Audio/ZLInflator (Source/DSP/ShaperFunctions.h, WaveShaper.h).
 * Oversampling and the band split are not ported here.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace inflator {

enum class Shape : std::size_t { identity, quadratic, cubic, quartic, sin };

/** One shaper curve on the normalised input range [0, 1]. Coefficients are precomputed in setParameters(). */
template<typename T>
class Shaper {
public:
  void setParameters(Shape type, T curve, bool compensation) {
    type_ = type;
    switch (type_) {
      case Shape::identity:
        scale_ = T(1);
        break;
      case Shape::quadratic:
      case Shape::cubic:
        scale_ = compensation ? 1 / compensationFor(type_) : T(1);
        break;
      case Shape::quartic: {
        const T c = T(-6) + T(6) * curve;
        a_ = (T(4) + c) / 2;
        b_ = T(-5) - c;
        c_ = (T(6) + c) / 2;
        scale_ = compensation ? 1 / (T(0.029832404608718992) * c + T(1.2211563052435235)) : T(1);
        break;
      }
      case Shape::sin: {
        trueCurve_ = (std::pow(curve, T(0.427)) * T(0.999) + T(0.001)) * std::numbers::pi_v<T> / 2;
        b_ = -std::sin(T(0) * trueCurve_);
        k_ = T(1) / (std::sin(trueCurve_) + b_);
        scale_ = compensation ? 1 / (T(0.48339138157922157) * curve + T(0.9999698009251106)) : T(1);
        break;
      }
    }
  }

  T operator()(T x) const {
    switch (type_) {
      case Shape::identity:
        return x;
      case Shape::quadratic:
        return scale_ * x * (2 - x);
      case Shape::cubic:
        return scale_ * x * (1 + x * (1 - x));
      case Shape::quartic:
        return scale_ * x * (1 + x * (c_ + x * (b_ + a_ * x)));
      case Shape::sin:
        return scale_ * k_ * (std::sin(x * trueCurve_) + b_);
    }
    return x;
  }

private:
  static T compensationFor(Shape type) {
    return type == Shape::quadratic ? T(1.7619606880293588) : T(1.0982883051371357);
  }

  Shape type_ = Shape::identity;
  T a_ = 0, b_ = 0, c_ = 0, k_ = 1, trueCurve_ = 0, scale_ = 1;
};

/** Two shaper curves blended by weight (0 = curve 1 only, 1 = curve 2 only). */
template<typename T>
class ShaperMixer {
public:
  ShaperMixer() {
    setShapes(T(0.25), T(0.25), T(0.5), false);
  }

  void setShapes(T curve1, T curve2, T weight, bool compensation) {
    weight1_ = T(1) - weight;
    weight2_ = weight;
    for (std::size_t i = 0; i < numShapes; ++i) {
      shaper1_[i].setParameters(static_cast<Shape>(i), curve1, compensation);
      shaper2_[i].setParameters(static_cast<Shape>(i), curve2, compensation);
    }
  }

  void setTypes(std::size_t type1, std::size_t type2) {
    type1_ = type1;
    type2_ = type2;
  }

  T operator()(T x) const {
    return shaper1_[type1_](x) * weight1_ + shaper2_[type2_](x) * weight2_;
  }

private:
  static constexpr std::size_t numShapes = 5;
  std::array<Shaper<T>, numShapes> shaper1_{}, shaper2_{};
  T weight1_ = 0.5, weight2_ = 0.5;
  std::size_t type1_ = static_cast<std::size_t>(Shape::quartic);
  std::size_t type2_ = static_cast<std::size_t>(Shape::quadratic);
};

/** Symmetric waveshaper: the shaped signal is mixed with the dry one by wet; the sign of x is kept. */
template<typename T>
class Inflator {
public:
  void setWet(T wet) {
    wet_ = wet;
    dry_ = T(1) - wet;
  }

  void setShapes(T curve1, T curve2, T weight, bool compensation) {
    mixer_.setShapes(curve1, curve2, weight, compensation);
  }

  void setTypes(std::size_t type1, std::size_t type2) {
    mixer_.setTypes(type1, type2);
  }

  T operator()(T x) const {
    const T sign = x > 0 ? T(1) : T(-1);
    return mixer_(std::min(T(1), std::abs(x))) * wet_ * sign + x * dry_;
  }

private:
  ShaperMixer<T> mixer_;
  T wet_ = 1, dry_ = 0;
};

}  // namespace inflator
