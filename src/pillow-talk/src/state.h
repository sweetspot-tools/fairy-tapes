/**
 * Fairy Tapes · Pillow Talk — Versioned plugin state (pure, no SDK).
 *
 * Layout, little endian:
 *   v1: int32 version, kNumParams doubles (normalized values).
 *   v2: v1, then the decor: uint32 seed, 4 x float32 screw angles (degrees, user rotation added to the seeded angle),
 *       uint32 text count, then per text uint32 length + bytes.
 * v1 still loads: the decor gets its defaults (seed 0 = "none yet": the caller generates one with ensureSeed, never
 * on the audio thread, and saves it with the next getState).
 * Everything read is sanitized: parameters clamped to [0,1] (a non-finite parameter refuses the whole state, as in
 * v1); non-finite angles become 0 and the others wrap to [0,360); tape texts keep printable ASCII only, trimmed,
 * at most kMaxTapeChars each and kMaxTapeTexts in all (one per tape slot, ui/layout.h kNumTapeSlots).
 */

#pragma once

#include "engine.h"   // loadDefaults
#include "params.h"
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

namespace FairyTapes::PillowTalk {

constexpr int32_t kStateVersion = 2;
constexpr size_t kStateSizeV1 = sizeof(int32_t) + sizeof(double) * kNumParams;

constexpr int kNumScrews = 4;
constexpr int kMaxTapeTexts = 6;           // == Ui::kNumTapeSlots (checked in ui/editor.cpp)
constexpr int kMaxTapeChars = 14;
constexpr uint32_t kMaxStoredTexts = 64;   // more announced texts: corrupt state
constexpr uint32_t kMaxStoredTextBytes = 256;
// Upper bound of a valid v2 blob (readers stop there).
constexpr size_t kMaxStateBytes =
    kStateSizeV1 + 4 + 4 * kNumScrews + 4 + kMaxStoredTexts * (4 + kMaxStoredTextBytes);

// Instance decor: the wear seed, the user's screw rotations and the tape texts. Not parameters: no automation.
struct Decor {
  uint32_t seed = 0;
  float screwAngles[kNumScrews] = {0.f, 0.f, 0.f, 0.f};
  std::vector<std::string> tapeTexts;
};

struct PluginState {
  double params[kNumParams];
  Decor decor;
};

inline void defaultState(PluginState& s) {
  loadDefaults(s.params);
  s.decor = Decor{};
}

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

// Printable ASCII only (0x20-0x7E), spaces trimmed at both ends, at most kMaxTapeChars characters.
inline std::string sanitizeTapeText(std::string_view in) {
  std::string out;
  out.reserve(std::min<size_t>(in.size(), 64));
  for (char ch : in) {
    const auto c = static_cast<unsigned char>(ch);
    if (c >= 0x20 && c < 0x7f) out.push_back(ch);
  }
  const auto trim = [](std::string& s) {
    const size_t a = s.find_first_not_of(' ');
    if (a == std::string::npos) { s.clear(); return; }
    s = s.substr(a, s.find_last_not_of(' ') - a + 1);
  };
  trim(out);
  if (out.size() > (size_t)kMaxTapeChars) {
    out.resize(kMaxTapeChars);
    trim(out);
  }
  return out;
}

// Degrees in [0,360); non-finite -> 0.
inline float sanitizeScrewAngle(float deg) {
  if (!std::isfinite(deg)) return 0.f;
  float a = std::fmod(deg, 360.f);
  if (a < 0.f) a += 360.f;
  if (!(a >= 0.f && a < 360.f)) a = 0.f;   // -1e-8 + 360 rounds to 360
  return a;
}

inline void sanitizeDecor(Decor& d) {
  for (float& a : d.screwAngles) a = sanitizeScrewAngle(a);
  if (d.tapeTexts.size() > (size_t)kMaxTapeTexts) d.tapeTexts.resize(kMaxTapeTexts);
  for (auto& t : d.tapeTexts) t = sanitizeTapeText(t);
}

// A non-zero seed from the OS entropy source mixed with the clock (std::random_device may be deterministic on some
// platforms). UI/main thread only.
inline uint32_t newRandomSeed() {
  static std::mt19937 mix = [] {
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
}

namespace detail {
inline bool readDecor(Reader& r, Decor& out) {
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
  sanitizeDecor(d);
  out = std::move(d);
  return true;
}
} // namespace detail

// The decor alone (the payload of the "Decor" message, messages.h). False leaves `out` untouched.
inline bool deserializeDecor(const uint8_t* data, size_t size, Decor& out) {
  if (!data) return false;
  detail::Reader r{data, size};
  return detail::readDecor(r, out);
}

inline void serializeState(const PluginState& s, std::vector<uint8_t>& out) {
  out.clear();
  out.reserve(kStateSizeV1 + 64);
  detail::putLE(out, (uint32_t)kStateVersion, 4);
  for (int i = 0; i < (int)kNumParams; ++i) {
    uint64_t bits;
    std::memcpy(&bits, &s.params[i], sizeof bits);
    detail::putLE(out, bits, 8);
  }
  serializeDecor(s.decor, out);
}

// Reads v1 or v2. Returns false (and leaves `out` untouched) on an unknown version, short data or a non-finite
// parameter. Bytes after the known layout are ignored.
inline bool deserializeState(const uint8_t* data, size_t size, PluginState& out) {
  if (!data) return false;
  detail::Reader r{data, size};
  uint32_t version = 0;
  if (!r.u32(version) || (version != 1 && version != 2)) return false;
  PluginState tmp;
  defaultState(tmp);
  for (int i = 0; i < (int)kNumParams; ++i) {
    if (!r.f64(tmp.params[i]) || !std::isfinite(tmp.params[i])) return false;
    tmp.params[i] = std::clamp(tmp.params[i], 0.0, 1.0);
  }
  if (version == 2 && !detail::readDecor(r, tmp.decor)) return false;
  out = std::move(tmp);
  return true;
}

} // namespace FairyTapes::PillowTalk
