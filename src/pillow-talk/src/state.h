/**
 * Fairy Tapes · Pillow Talk — Versioned plugin state (pure, no SDK).
 *
 * Layout, little endian:
 *   v1: int32 version, kNumParams doubles (normalized values).
 *   v2: v1, then the decor: uint32 seed, 4 x float32 screw angles (degrees, user rotation added to the seeded angle),
 *       uint32 text count, then per text uint32 length + bytes.
 *   v3: v2, then the behaviour settings of the back panel: uint32 display window (index: 3 / 6 / 12 s), uint32 display
 *       peaks (index: off / 1.5 / 3 s), float32 tempo fallback (BPM, used when the host sends no tempo).
 * v1 still loads: the decor gets its defaults (seed 0 = "none yet": the caller generates one with ensureSeed, never
 * on the audio thread, and saves it with the next getState); v1 and v2 get the default behaviour (6 s, 1.5 s, 120 BPM).
 * Everything read is sanitized: parameters clamped to [0,1] (a non-finite parameter refuses the whole state, as in
 * v1); non-finite angles become 0 and the others wrap to [0,360); tape texts keep printable ASCII only, trimmed,
 * at most kMaxTapeChars each and kMaxTapeTexts in all (one per tape slot, ui/layout.h kNumTapeSlots); a behaviour index
 * out of range and a tempo fallback that is not finite or outside 20-300 BPM take their defaults.
 */

#pragma once

#include "engine.h"   // loadDefaults
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

namespace FairyTapes::PillowTalk {

constexpr int32_t kStateVersion = 3;
constexpr size_t kStateSizeV1 = sizeof(int32_t) + sizeof(double) * kNumParams;

constexpr int kNumScrews = 4;
constexpr int kMaxTapeTexts = 6;           // == Ui::kNumTapeSlots (checked in ui/editor.cpp)
constexpr int kMaxTapeChars = 14;
static_assert(kMaxTapeChars == static_cast<int>(FairyTapes::Ui::kTapeTextMaxChars), "the suite-wide tape text length");
constexpr uint32_t kMaxStoredTexts = 64;   // more announced texts: corrupt state
constexpr uint32_t kMaxStoredTextBytes = 256;
constexpr size_t kBehaviourBytes = 4 + 4 + 4;
// Upper bound of a valid v3 blob (readers stop there).
constexpr size_t kMaxStateBytes =
    kStateSizeV1 + 4 + 4 * kNumScrews + 4 + kMaxStoredTexts * (4 + kMaxStoredTextBytes) + kBehaviourBytes;

// Behaviour settings of the back panel (SETTINGS > BEHAVIOUR, "saved with the project"), as in pillow_rack.html.
constexpr int kDisplayWindowCount = 3;   // DISPLAY WINDOW: 3 S / 6 S / 12 S (time shown by the ducking display)
constexpr int kDisplayPeaksCount = 3;    // DISPLAY PEAKS: OFF / 1.5 S / 3 S (peak hold of the LEVEL meters)
constexpr float kMinFallbackBpm = 20.f, kMaxFallbackBpm = 300.f;
struct Behaviour {
  uint32_t displayWindow = 1;                         // 6 s
  uint32_t displayPeaks = 1;                          // 1.5 s
  float tempoFallbackBpm = (float)kFallbackBpm;       // TEMPO FALLBACK: when the host sends no tempo
};
inline double displayWindowSeconds(uint32_t index) {
  static const double s[kDisplayWindowCount] = {3.0, 6.0, 12.0};
  return index < (uint32_t)kDisplayWindowCount ? s[index] : s[Behaviour{}.displayWindow];
}
inline double displayPeakHoldSeconds(uint32_t index) {
  static const double s[kDisplayPeaksCount] = {0.0, 1.5, 3.0};
  return index < (uint32_t)kDisplayPeaksCount ? s[index] : s[Behaviour{}.displayPeaks];
}
inline void sanitizeBehaviour(Behaviour& b) {
  const Behaviour d;
  if (b.displayWindow >= (uint32_t)kDisplayWindowCount) b.displayWindow = d.displayWindow;
  if (b.displayPeaks >= (uint32_t)kDisplayPeaksCount) b.displayPeaks = d.displayPeaks;
  if (!std::isfinite(b.tempoFallbackBpm) || b.tempoFallbackBpm < kMinFallbackBpm || b.tempoFallbackBpm > kMaxFallbackBpm)
    b.tempoFallbackBpm = d.tempoFallbackBpm;
}

// Per-instance data saved with the project that is not a parameter (no automation): the wear seed, the user's screw
// rotations and tape texts (the "decor"), and the behaviour settings of the back panel. One blob, one message.
struct Decor {
  uint32_t seed = 0;
  float screwAngles[kNumScrews] = {0.f, 0.f, 0.f, 0.f};
  std::vector<std::string> tapeTexts;
  Behaviour behaviour;
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
  for (float& a : d.screwAngles) a = sanitizeScrewAngle(a);
  if (d.tapeTexts.size() > (size_t)kMaxTapeTexts) d.tapeTexts.resize(kMaxTapeTexts);
  for (auto& t : d.tapeTexts) t = sanitizeTapeText(t);
  sanitizeBehaviour(d.behaviour);
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
  detail::putLE(out, d.behaviour.displayWindow, 4);
  detail::putLE(out, d.behaviour.displayPeaks, 4);
  uint32_t bpmBits;
  std::memcpy(&bpmBits, &d.behaviour.tempoFallbackBpm, sizeof bpmBits);
  detail::putLE(out, bpmBits, 4);
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
  if (withBehaviour &&
      (!r.u32(d.behaviour.displayWindow) || !r.u32(d.behaviour.displayPeaks) || !r.f32(d.behaviour.tempoFallbackBpm)))
    return false;
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

// Reads v1, v2 or v3. Returns false (and leaves `out` untouched) on an unknown version, short data or a non-finite
// parameter. Bytes after the known layout are ignored.
inline bool deserializeState(const uint8_t* data, size_t size, PluginState& out) {
  if (!data) return false;
  detail::Reader r{data, size};
  uint32_t version = 0;
  if (!r.u32(version) || version < 1 || version > 3) return false;
  PluginState tmp;
  defaultState(tmp);
  for (int i = 0; i < (int)kNumParams; ++i) {
    if (!r.f64(tmp.params[i]) || !std::isfinite(tmp.params[i])) return false;
    tmp.params[i] = std::clamp(tmp.params[i], 0.0, 1.0);
  }
  if (version >= 2 && !detail::readDecor(r, tmp.decor, version >= 3)) return false;
  out = std::move(tmp);
  return true;
}

} // namespace FairyTapes::PillowTalk
