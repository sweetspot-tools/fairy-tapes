/**
 * Fairy Tapes · Hotline — Versioned plugin state (pure, no SDK).
 *
 * Layout, little endian: int32 version, kNumParams doubles (normalized values).
 * Reading refuses an unknown version, short data or a non-finite value (the output array stays untouched);
 * values are clamped to [0,1]. Bytes after the known layout are ignored.
 */

#pragma once

#include "params.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace FairyTapes::Hotline {

constexpr int32_t kStateVersion = 1;
constexpr size_t kStateSize = sizeof(int32_t) + sizeof(double) * kNumParams;

inline void serializeState(const double* values, std::vector<uint8_t>& out) {
  out.clear();
  out.reserve(kStateSize);
  const uint32_t version = static_cast<uint32_t>(kStateVersion);
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(version >> (8 * i)));
  for (int p = 0; p < static_cast<int>(kNumParams); ++p) {
    uint64_t bits;
    std::memcpy(&bits, &values[p], sizeof bits);
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>(bits >> (8 * i)));
  }
}

inline bool deserializeState(const uint8_t* data, size_t size, double* values) {
  if (!data || !values || size < kStateSize) return false;
  uint32_t version = 0;
  for (int i = 0; i < 4; ++i) version |= static_cast<uint32_t>(data[i]) << (8 * i);
  if (version != static_cast<uint32_t>(kStateVersion)) return false;
  double tmp[kNumParams];
  for (int p = 0; p < static_cast<int>(kNumParams); ++p) {
    uint64_t bits = 0;
    for (int i = 0; i < 8; ++i) bits |= static_cast<uint64_t>(data[4 + 8 * p + i]) << (8 * i);
    std::memcpy(&tmp[p], &bits, sizeof bits);
    if (!std::isfinite(tmp[p])) return false;
    tmp[p] = std::clamp(tmp[p], 0.0, 1.0);
  }
  std::memcpy(values, tmp, sizeof tmp);
  return true;
}

} // namespace FairyTapes::Hotline
