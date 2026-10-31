/**
 * Fairy Tapes · Happy Ending — Versioned plug-in state (pure, no SDK libs).
 *
 * Layout, little endian: int32 version (1), then the kNumParams normalized values as float64 in
 * ParamID order. Nothing else: LISTEN and SET (kListenBand, kSetActive) are session parameters with
 * IDs beyond the table and are never stored, so a project always opens with LISTEN off and SET off.
 * OVERSAMPLING (kOversampling) is stored: it is a project setting.
 *
 * The table is frozen and only ever grows at the end (params.h), so an older blob is simply shorter:
 * what is there loads, the parameters beyond it keep their defaults. A version other than 1 or a blob
 * that cannot hold the version is refused. Values are sanitized: clamped to [0, 1], a non-finite value
 * keeps its default. Bytes after the known layout are ignored (a newer build may append data).
 */

#pragma once

#include "params.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace FairyTapes::HappyEnding {

constexpr std::int32_t kStateVersion = 1;
constexpr std::size_t kStateHeaderBytes = sizeof(std::int32_t);
constexpr std::size_t kStateBytes = kStateHeaderBytes + sizeof(double) * kNumParams;
constexpr std::size_t kMaxStateBytes = kStateBytes;  // readers stop here: a bigger blob is not ours

namespace detail {
inline void putLE(std::vector<std::uint8_t>& out, std::uint64_t v, int bytes) {
  for (int i = 0; i < bytes; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}
inline std::uint64_t getLE(const std::uint8_t* p, int bytes) {
  std::uint64_t v = 0;
  for (int i = 0; i < bytes; ++i) v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
  return v;
}
} // namespace detail

// norm: kNumParams normalized values in ParamID order. Clears `out` first.
inline void serializeState(const double* norm, std::vector<std::uint8_t>& out) {
  out.clear();
  out.reserve(kStateBytes);
  detail::putLE(out, static_cast<std::uint32_t>(kStateVersion), 4);
  for (int i = 0; i < static_cast<int>(kNumParams); ++i) {
    std::uint64_t bits;
    std::memcpy(&bits, &norm[i], sizeof bits);
    detail::putLE(out, bits, 8);
  }
}

// Fills `norm` (kNumParams values): the defaults, then every whole value in the blob. Returns false
// and leaves `norm` untouched on an unknown version or when there is no room for the version.
inline bool deserializeState(const std::uint8_t* data, std::size_t size, double* norm) {
  if (!data || !norm || size < kStateHeaderBytes) return false;
  if (detail::getLE(data, 4) != static_cast<std::uint32_t>(kStateVersion)) return false;
  double tmp[kNumParams];
  loadDefaults(tmp);
  const std::size_t stored = std::min<std::size_t>((size - kStateHeaderBytes) / sizeof(double), kNumParams);
  for (std::size_t i = 0; i < stored; ++i) {
    const std::uint64_t bits = detail::getLE(data + kStateHeaderBytes + i * sizeof(double), 8);
    double v;
    std::memcpy(&v, &bits, sizeof v);
    if (std::isfinite(v)) tmp[i] = std::clamp(v, 0.0, 1.0);
  }
  std::memcpy(norm, tmp, sizeof tmp);
  return true;
}

} // namespace FairyTapes::HappyEnding
