/**
 * Fairy Tapes · Deep Throat — Versioned plugin state (pure, no SDK).
 *
 * Layout, little endian:
 *   v1: int32 version, kNumParams doubles (normalized values).
 *   v2: v1, then the decor: uint32 seed, 4 x float32 screw angles (degrees, user rotation added to the seeded angle),
 *       uint32 text count, then per text uint32 length + bytes.
 *   v3: v2, then the behaviour settings of the back panel (SETTINGS > BEHAVIOUR, spec 9b): uint32 latency (0 MIX,
 *       1 LIVE). The decor blob of the "Decor" message always ends with this block.
 * v1 and v2 still load: the decor gets its defaults (seed 0 = "none yet": the caller generates one with ensureSeed, never
 * on the audio thread, and saves it with the next getState) and the behaviour gets MIX (the latency they were saved with).
 * Everything read is sanitized: parameters clamped to [0,1] (a non-finite parameter refuses the whole state);
 * non-finite angles become 0 and the others wrap to [0,360); tape texts keep printable ASCII only, trimmed, at most
 * kMaxTapeChars each and kMaxTapeTexts in all (one per tape slot); an unknown latency index becomes MIX. Corrupt input
 * is refused as a whole.
 */

#pragma once

#include "params.h"
#include "fairy_ui/tape_label.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace FairyTapes::DeepThroat {

constexpr int32_t kStateVersion = 3;
constexpr size_t kStateSizeV1 = sizeof(int32_t) + sizeof(double) * kNumParams;

constexpr int kNumScrews = 4;
constexpr int kMaxTapeTexts = 6;
constexpr int kMaxTapeChars = 14;
static_assert(kMaxTapeChars == static_cast<int>(FairyTapes::Ui::kTapeTextMaxChars), "the suite-wide tape text length");
constexpr uint32_t kMaxStoredTexts = 64;   // more announced texts: corrupt state
constexpr uint32_t kMaxStoredTextBytes = 256;
constexpr size_t kBehaviourBytes = 4;   // uint32 latency
// Upper bound of a valid v3 blob (readers stop there).
constexpr size_t kMaxStateBytes =
    kStateSizeV1 + 4 + 4 * kNumScrews + 4 + kMaxStoredTexts * (4 + kMaxStoredTextBytes) + kBehaviourBytes;

// Behaviour settings of the back panel (SETTINGS > BEHAVIOUR, "saved with the project"; Pillow Talk's pattern): not
// parameters, never automated. LATENCY (spec 9b) is applied by the processor when the host prepares it.
struct Behaviour {
  LatencyMode latency = LatencyMode::Mix;
};

inline LatencyMode latencyFromIndex(uint32_t index) {
  return index < (uint32_t)kLatencyModeCount ? static_cast<LatencyMode>(index) : LatencyMode::Mix;
}

// Per-instance data saved with the project that is not a parameter (no automation): the wear seed, the user's screw
// rotations and tape texts (the "decor"), and the behaviour settings of the back panel. One blob, one message.
struct Decor {
  uint32_t seed = 0;
  float screwAngles[kNumScrews] = {0.f, 0.f, 0.f, 0.f};
  std::vector<std::string> tapeTexts;
  Behaviour behaviour;
};

namespace detail {
inline void putLE(std::vector<uint8_t>& out, uint64_t v, int bytes) {
  for (int i = 0; i < bytes; ++i) out.push_back((uint8_t)(v >> (8 * i)));
}
inline uint64_t getLE(const uint8_t* p, int bytes) {
  uint64_t v = 0;
  for (int i = 0; i < bytes; ++i) v |= (uint64_t)p[i] << (8 * i);
  return v;
}

// Bounds-checked little-endian reader.
struct Reader {
  const uint8_t* p;
  size_t size, pos = 0;
  bool take(size_t n, const uint8_t*& at) {
    if (n > size - pos) return false;
    at = p + pos;
    pos += n;
    return true;
  }
  bool u32(uint32_t& v) {
    const uint8_t* at = nullptr;
    if (!take(4, at)) return false;
    v = (uint32_t)getLE(at, 4);
    return true;
  }
  bool f64(double& v) {
    const uint8_t* at = nullptr;
    if (!take(8, at)) return false;
    const uint64_t bits = getLE(at, 8);
    std::memcpy(&v, &bits, sizeof v);
    return true;
  }
  bool f32(float& v) {
    uint32_t bits = 0;
    if (!u32(bits)) return false;
    std::memcpy(&v, &bits, sizeof v);
    return true;
  }
};
} // namespace detail

// Printable ASCII only (0x20-0x7E), spaces trimmed at both ends, at most kMaxTapeChars characters (shared rule,
// fairy_ui/tape_label.h: what the tape fonts can draw).
inline std::string sanitizeTapeText(std::string_view in) { return FairyTapes::Ui::sanitizeTapeText(in, kMaxTapeChars); }

// Degrees in [0,360); non-finite -> 0.
inline float sanitizeScrewAngle(float deg) {
  if (!std::isfinite(deg)) return 0.f;
  float a = std::fmod(deg, 360.f);
  if (a < 0.f) a += 360.f;
  if (!(a >= 0.f && a < 360.f)) a = 0.f;   // -1e-8 + 360 rounds to 360
  return a;
}

inline void sanitizeDecor(Decor& d) {
  d.behaviour.latency = latencyFromIndex(static_cast<uint32_t>(d.behaviour.latency));
  for (float& a : d.screwAngles) a = sanitizeScrewAngle(a);
  if (d.tapeTexts.size() > (size_t)kMaxTapeTexts) d.tapeTexts.resize(kMaxTapeTexts);
  for (auto& t : d.tapeTexts) t = sanitizeTapeText(t);
}

// A non-zero seed from the OS entropy source mixed with the clock (std::random_device may be deterministic on some
// platforms). One generator per thread (no shared state). Never on the audio thread.
inline uint32_t newRandomSeed() {
  thread_local std::mt19937 mix = [] {
    std::random_device rd;
    const auto t = (uint64_t)std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::seed_seq seq{rd(), rd(), (uint32_t)t, (uint32_t)(t >> 32)};
    return std::mt19937(seq);
  }();
  uint32_t s = 0;
  for (int i = 0; i < 8 && s == 0; ++i) s = (uint32_t)mix();
  return s ? s : 0x9E3779B9u;
}

// Seed 0 means "none yet": generates one (true). A generator that answers 0 still yields a non-zero seed.
template <class Gen>
bool ensureSeed(Decor& d, Gen gen) {
  if (d.seed != 0) return false;
  d.seed = (uint32_t)gen();
  if (d.seed == 0) d.seed = 0x9E3779B9u;
  return true;
}
inline bool ensureSeed(Decor& d) { return ensureSeed(d, newRandomSeed); }

inline void serializeDecor(const Decor& in, std::vector<uint8_t>& out) {
  Decor d = in;
  sanitizeDecor(d);
  detail::putLE(out, d.seed, 4);
  for (float a : d.screwAngles) {
    uint32_t bits;
    std::memcpy(&bits, &a, sizeof bits);
    detail::putLE(out, bits, 4);
  }
  detail::putLE(out, (uint32_t)d.tapeTexts.size(), 4);
  for (const auto& t : d.tapeTexts) {
    detail::putLE(out, (uint32_t)t.size(), 4);
    out.insert(out.end(), t.begin(), t.end());
  }
  detail::putLE(out, static_cast<uint32_t>(d.behaviour.latency), 4);
}

namespace detail {
// `withBehaviour`: the blob ends with the behaviour block (state v3 and every message); otherwise it gets the defaults.
inline bool readDecor(Reader& r, Decor& out, bool withBehaviour) {
  Decor d;
  if (!r.u32(d.seed)) return false;
  for (float& a : d.screwAngles)
    if (!r.f32(a)) return false;
  uint32_t count = 0;
  if (!r.u32(count) || count > kMaxStoredTexts) return false;
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t len = 0;
    const uint8_t* at = nullptr;
    if (!r.u32(len) || len > kMaxStoredTextBytes || !r.take(len, at)) return false;
    if (d.tapeTexts.size() < (size_t)kMaxTapeTexts) d.tapeTexts.emplace_back(reinterpret_cast<const char*>(at), len);
  }
  if (withBehaviour) {
    uint32_t latency = 0;
    if (!r.u32(latency)) return false;
    d.behaviour.latency = latencyFromIndex(latency);
  }
  sanitizeDecor(d);
  out = std::move(d);
  return true;
}
} // namespace detail

// The decor alone (the payload of the "Decor" message, messages.h). False leaves `out` untouched.
inline bool deserializeDecor(const uint8_t* data, size_t size, Decor& out) {
  if (!data) return false;
  detail::Reader r{data, size};
  return detail::readDecor(r, out, true);
}

inline void serializeState(const double* values, const Decor& decor, std::vector<uint8_t>& out) {
  out.clear();
  out.reserve(kStateSizeV1 + 64);
  detail::putLE(out, (uint32_t)kStateVersion, 4);
  for (int i = 0; i < (int)kNumParams; ++i) {
    uint64_t bits;
    std::memcpy(&bits, &values[i], sizeof bits);
    detail::putLE(out, bits, 8);
  }
  serializeDecor(decor, out);
}

// Reads v1, v2 or v3. Returns false (and leaves `values` and `decor` untouched) on an unknown version, short data or a
// non-finite parameter. A v1 blob gives the default decor (seed 0); v1 and v2 give the default behaviour (MIX). Bytes
// after the known layout are ignored.
inline bool deserializeState(const uint8_t* data, size_t size, double* values, Decor& decor) {
  if (!data) return false;
  detail::Reader r{data, size};
  uint32_t version = 0;
  if (!r.u32(version) || version < 1 || version > (uint32_t)kStateVersion) return false;
  double tmp[kNumParams];
  for (int i = 0; i < (int)kNumParams; ++i) {
    if (!r.f64(tmp[i]) || !std::isfinite(tmp[i])) return false;
    tmp[i] = std::clamp(tmp[i], 0.0, 1.0);
  }
  Decor d;
  if (version >= 2 && !detail::readDecor(r, d, version >= 3)) return false;
  for (int i = 0; i < (int)kNumParams; ++i) values[i] = tmp[i];
  decor = std::move(d);
  return true;
}

} // namespace FairyTapes::DeepThroat
