/**
 * Fairy Tapes · Pillow Talk — Versioned plugin state (pure, no SDK).
 * Layout, little endian: int32 version, then kNumParams doubles (normalized values).
 */

#pragma once

#include "params.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace FairyTapes::PillowTalk {

constexpr int32_t kStateVersion = 1;
constexpr size_t kStateSize = sizeof(int32_t) + sizeof(double) * kNumParams;

namespace detail {
inline void putLE(std::vector<uint8_t>& out, uint64_t v, int bytes) {
  for (int i = 0; i < bytes; ++i) out.push_back((uint8_t)(v >> (8 * i)));
}
inline uint64_t getLE(const uint8_t* p, int bytes) {
  uint64_t v = 0;
  for (int i = 0; i < bytes; ++i) v |= (uint64_t)p[i] << (8 * i);
  return v;
}
} // namespace detail

inline void serializeState(const double* values, std::vector<uint8_t>& out) {
  out.clear();
  out.reserve(kStateSize);
  detail::putLE(out, (uint32_t)kStateVersion, 4);
  for (int i = 0; i < (int)kNumParams; ++i) {
    uint64_t bits;
    std::memcpy(&bits, &values[i], sizeof bits);
    detail::putLE(out, bits, 8);
  }
}

// Returns false (and leaves `values` untouched) on unknown version or short data.
inline bool deserializeState(const uint8_t* data, size_t size, double* values) {
  if (!data || size < kStateSize) return false;
  if ((int32_t)(uint32_t)detail::getLE(data, 4) != kStateVersion) return false;
  for (int i = 0; i < (int)kNumParams; ++i) {
    const uint64_t bits = detail::getLE(data + 4 + 8 * i, 8);
    std::memcpy(&values[i], &bits, sizeof bits);
  }
  return true;
}

} // namespace FairyTapes::PillowTalk
