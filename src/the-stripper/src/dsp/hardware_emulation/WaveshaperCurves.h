/*
 * Waveshaper curves (Transformer) — from Dusk Audio Multi-Comp (GPL-3.0).
 * Fairy Tapes · The Stripper. Original: HardwareEmulation/WaveshaperCurves.h
 */

#pragma once

#include <array>
#include <algorithm>
#include <cmath>

namespace HardwareEmulation {

class WaveshaperCurves {
public:
  static constexpr int TABLE_SIZE = 4096;
  static constexpr float TABLE_RANGE = 4.0f;

  enum class CurveType { Transformer, Linear };

  WaveshaperCurves() {
    initializeTransformerCurve();
    initializeLinearCurve();
  }

  float process(float input, CurveType curve) const {
    float normalized = (input + TABLE_RANGE / 2.0f) / TABLE_RANGE;
    normalized = std::clamp(normalized, 0.0f, 0.9999f);
    float indexFloat = normalized * (TABLE_SIZE - 1);
    int index0 = static_cast<int>(indexFloat);
    int index1 = std::min(index0 + 1, TABLE_SIZE - 1);
    float frac = indexFloat - static_cast<float>(index0);
    const auto& table = (curve == CurveType::Transformer) ? transformerCurve_ : linearCurve_;
    return table[index0] * (1.0f - frac) + table[index1] * frac;
  }

private:
  std::array<float, TABLE_SIZE> transformerCurve_;
  std::array<float, TABLE_SIZE> linearCurve_;

  static float indexToInput(int index) {
    return (static_cast<float>(index) / (TABLE_SIZE - 1)) * TABLE_RANGE - TABLE_RANGE / 2.0f;
  }

  void initializeTransformerCurve() {
    for (int i = 0; i < TABLE_SIZE; ++i) {
      float x = indexToInput(i);
      float absX = std::abs(x);
      float sign = (x >= 0.0f) ? 1.0f : -1.0f;
      if (absX < 0.7f) {
        float harmonic2 = x * absX * 0.05f;
        transformerCurve_[i] = x + harmonic2;
      } else if (absX < 1.2f) {
        float excess = absX - 0.7f;
        float compressed = 0.7f + excess * (1.0f - excess * 0.25f);
        float harmonic2 = (sign * compressed) * compressed * 0.08f;
        transformerCurve_[i] = sign * compressed + harmonic2;
      } else {
        float excess = absX - 1.2f;
        float hard = 1.05f + std::tanh(excess * 1.5f) * 0.15f;
        transformerCurve_[i] = sign * hard;
      }
    }
  }

  void initializeLinearCurve() {
    for (int i = 0; i < TABLE_SIZE; ++i)
      linearCurve_[i] = indexToInput(i);
  }
};

inline WaveshaperCurves& getWaveshaperCurves() {
  static WaveshaperCurves instance;
  return instance;
}

}  // namespace HardwareEmulation
