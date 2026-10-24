// Deep Throat GUI: fixed-capacity history of the SCOPE traces and their conversion to pixel columns
// (no VSTGUI, no SDK; unit tested in tests/test_ui.cpp).
#pragma once

#include "ui/layout.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace FairyTapes::DeepThroat::Ui {

// One tick of the display parameters, as normalized by displayNorms (display.h): voice over -60..+6 dB, Rider gain as
// 0.5 + gain / 12 (-6..+6 dB), breath probability 0..1.
struct ScopePoint {
  float voice = 0.f, rider = 0.5f, breath = 0.f;
};

// Circular buffer; at(0) is the oldest point. Never allocates after construction (the array is inline).
template <std::size_t N>
class History {
  static_assert(N > 0, "History needs a capacity");

public:
  static constexpr int capacity() { return static_cast<int>(N); }
  int size() const { return size_; }

  void push(const ScopePoint& p) {
    data_[next_] = {clean(p.voice, 0.f), clean(p.rider, 0.5f), clean(p.breath, 0.f)};
    next_ = (next_ + 1) % N;
    if (size_ < capacity()) ++size_;
  }
  void clear() {
    next_ = 0;
    size_ = 0;
  }
  // A neutral point (silence, no gain change, no breath) when `i` is out of range.
  ScopePoint at(int i) const {
    if (i < 0 || i >= size_) return {};
    const std::size_t oldest = (next_ + N - static_cast<std::size_t>(size_)) % N;
    return data_[(oldest + static_cast<std::size_t>(i)) % N];
  }

private:
  static float clean(float v, float fallback) {
    if (!std::isfinite(v)) return fallback;
    return v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
  }

  std::array<ScopePoint, N> data_{};
  std::size_t next_ = 0;
  int size_ = 0;
};

// One pixel column of the scope, as absolute y in the panel.
struct ScopeColumn {
  float voiceTop = 0.f;   // top of the voice envelope (the bottom of the scope is silence)
  float riderY = 0.f;     // the Rider gain line
  bool breath = false;    // inside the breath band
};

constexpr float kScopeVoiceFloorDb = -60.f, kScopeVoiceCeilDb = 0.f, kScopeNormMinDb = -60.f, kScopeNormMaxDb = 6.f;

inline ScopeColumn scopeColumnOf(const ScopePoint& p, const Rect& r) {
  const auto unit = [](float v) { return std::isfinite(v) ? std::clamp(v, 0.f, 1.f) : 0.f; };
  const float db = kScopeNormMinDb + unit(p.voice) * (kScopeNormMaxDb - kScopeNormMinDb);
  const float v = std::clamp((db - kScopeVoiceFloorDb) / (kScopeVoiceCeilDb - kScopeVoiceFloorDb), 0.f, 1.f);
  const float g = std::isfinite(p.rider) ? std::clamp(p.rider, 0.f, 1.f) : 0.5f;   // 0 = -6 dB (bottom), 1 = +6 dB (top)
  ScopeColumn c;
  c.voiceTop = r.y + r.h * (1.f - v);
  c.riderY = r.y + r.h * (1.f - g);
  c.breath = std::isfinite(p.breath) && p.breath > 0.5f;
  return c;
}

// One column per stored point, oldest first.
template <std::size_t N>
std::vector<ScopeColumn> scopeColumns(const History<N>& h, const Rect& r) {
  std::vector<ScopeColumn> out;
  out.reserve(static_cast<std::size_t>(h.size()));
  for (int i = 0; i < h.size(); ++i) out.push_back(scopeColumnOf(h.at(i), r));
  return out;
}

} // namespace FairyTapes::DeepThroat::Ui
