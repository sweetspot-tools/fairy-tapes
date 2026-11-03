/**
 * Fairy Tapes · Happy Ending — Versioned plug-in state (pure, no SDK libs).
 *
 * Layout, little endian:
 *   v1: int32 version, then the kNumParams normalized values as float64 in ParamID order.
 *   v2: int32 version, uint32 parameter count N, N normalized values as float64 in ParamID order, then the instance
 *       decor: uint32 seed, 4 x float32 screw angles (degrees, the user's rotation added to the seeded one), uint32
 *       text count, then per text uint32 length + bytes. The count makes the decor findable whatever the table size
 *       of the build that wrote it.
 * LISTEN and SET (kListenBand, kSetActive) are session parameters with IDs beyond the table and are never stored,
 * so a project always opens with LISTEN off and SET off. OVERSAMPLING (kOversampling) is stored: it is a project
 * setting.
 *
 * Parameters: the table is frozen and only ever grows at the end (params.h). A v2 blob written by an older build holds
 * fewer parameters (N < kNumParams): they load, the others keep their defaults; one written by a newer build holds
 * more (N > kNumParams): the extra ones are skipped and the decor is read after them. A v1 blob has no count: it
 * loads what it holds (v1 was never written by a build with a larger table). A version other than 1 or 2 or a blob
 * that cannot hold the version is refused. Values are sanitized: clamped to [0, 1], a non-finite value keeps its
 * default. Bytes after the known layout are ignored (a newer build may append data).
 *
 * Decor: v1 gives the default decor (seed 0 = "none yet": the caller generates one with ensureSeed, never on the
 * audio thread, and saves it with the next getState). Everything read is sanitized: non-finite angles become 0 and
 * the others wrap to [0,360); tape texts keep printable ASCII only, trimmed, at most kMaxTapeChars each and
 * kMaxTapeTexts in all (one per tape slot). A v2 blob that is corrupt in the decor part (cut, a huge text count, a
 * length beyond the blob) is NOT refused: its parameters load and the decor is the default one (unlike Deep Throat,
 * which refuses the whole blob: here the parameters are the user's work, the decor is only dressing).
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
#include <utility>
#include <vector>

namespace FairyTapes::HappyEnding {

constexpr std::int32_t kStateVersion = 2;
constexpr std::size_t kStateHeaderBytes = sizeof(std::int32_t);
constexpr std::size_t kStateSizeV1 = kStateHeaderBytes + sizeof(double) * kNumParams;
constexpr std::size_t kStateHeaderBytesV2 = kStateHeaderBytes + sizeof(std::uint32_t);   // version + parameter count
// Size of the version and parameter block of a v2 blob that stores `count` parameters.
constexpr std::size_t stateParamBlockBytesV2(std::size_t count) { return kStateHeaderBytesV2 + sizeof(double) * count; }
constexpr std::size_t kStateSizeV2 = stateParamBlockBytesV2(kNumParams);
// Room for the parameters a newer build may add (readers stop at kMaxStateBytes).
constexpr std::size_t kMaxFutureParams = 64;

constexpr int kNumScrews = 4;
constexpr int kMaxTapeTexts = 6;
constexpr int kMaxTapeChars = 14;
static_assert(kMaxTapeChars == static_cast<int>(FairyTapes::Ui::kTapeTextMaxChars), "the suite-wide tape text length");
constexpr std::uint32_t kMaxStoredTexts = 64;   // more announced texts: corrupt decor
constexpr std::uint32_t kMaxStoredTextBytes = 256;
// Upper bound of a valid v2 blob (readers stop there).
constexpr std::size_t kMaxStateBytes =
    stateParamBlockBytesV2(kNumParams + kMaxFutureParams) + 4 + 4 * kNumScrews + 4 + kMaxStoredTexts * (4 + kMaxStoredTextBytes);

// Per-instance data saved with the project that is not a parameter (no automation): the wear seed, the user's screw
// rotations and tape texts. One blob, one message.
struct Decor {
  std::uint32_t seed = 0;
  float screwAngles[kNumScrews] = {0.f, 0.f, 0.f, 0.f};
  std::vector<std::string> tapeTexts;
};

namespace detail {
inline void putLE(std::vector<std::uint8_t>& out, std::uint64_t v, int bytes) {
  for (int i = 0; i < bytes; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}
inline std::uint64_t getLE(const std::uint8_t* p, int bytes) {
  std::uint64_t v = 0;
  for (int i = 0; i < bytes; ++i) v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
  return v;
}

// Bounds-checked little-endian reader.
struct Reader {
  const std::uint8_t* p;
  std::size_t size, pos = 0;
  bool take(std::size_t n, const std::uint8_t*& at) {
    if (n > size - pos) return false;
    at = p + pos;
    pos += n;
    return true;
  }
  bool u32(std::uint32_t& v) {
    const std::uint8_t* at = nullptr;
    if (!take(4, at)) return false;
    v = static_cast<std::uint32_t>(getLE(at, 4));
    return true;
  }
  bool f32(float& v) {
    std::uint32_t bits = 0;
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
  if (d.tapeTexts.size() > static_cast<std::size_t>(kMaxTapeTexts)) d.tapeTexts.resize(kMaxTapeTexts);
  for (auto& t : d.tapeTexts) t = sanitizeTapeText(t);
}

// A non-zero seed from the OS entropy source mixed with the clock (std::random_device may be deterministic on some
// platforms). One generator per thread (no shared state). Never on the audio thread.
inline std::uint32_t newRandomSeed() {
  thread_local std::mt19937 mix = [] {
    std::random_device rd;
    const auto t = static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    std::seed_seq seq{rd(), rd(), static_cast<std::uint32_t>(t), static_cast<std::uint32_t>(t >> 32)};
    return std::mt19937(seq);
  }();
  std::uint32_t s = 0;
  for (int i = 0; i < 8 && s == 0; ++i) s = static_cast<std::uint32_t>(mix());
  return s ? s : 0x9E3779B9u;
}

// Seed 0 means "none yet": generates one (true). A generator that answers 0 still yields a non-zero seed.
template <class Gen>
bool ensureSeed(Decor& d, Gen gen) {
  if (d.seed != 0) return false;
  d.seed = static_cast<std::uint32_t>(gen());
  if (d.seed == 0) d.seed = 0x9E3779B9u;
  return true;
}
inline bool ensureSeed(Decor& d) { return ensureSeed(d, newRandomSeed); }

inline void serializeDecor(const Decor& in, std::vector<std::uint8_t>& out) {
  Decor d = in;
  sanitizeDecor(d);
  detail::putLE(out, d.seed, 4);
  for (float a : d.screwAngles) {
    std::uint32_t bits;
    std::memcpy(&bits, &a, sizeof bits);
    detail::putLE(out, bits, 4);
  }
  detail::putLE(out, static_cast<std::uint32_t>(d.tapeTexts.size()), 4);
  for (const auto& t : d.tapeTexts) {
    detail::putLE(out, static_cast<std::uint32_t>(t.size()), 4);
    out.insert(out.end(), t.begin(), t.end());
  }
}

namespace detail {
inline bool readDecor(Reader& r, Decor& out) {
  Decor d;
  if (!r.u32(d.seed)) return false;
  for (float& a : d.screwAngles)
    if (!r.f32(a)) return false;
  std::uint32_t count = 0;
  if (!r.u32(count) || count > kMaxStoredTexts) return false;
  for (std::uint32_t i = 0; i < count; ++i) {
    std::uint32_t len = 0;
    const std::uint8_t* at = nullptr;
    if (!r.u32(len) || len > kMaxStoredTextBytes || !r.take(len, at)) return false;
    if (d.tapeTexts.size() < static_cast<std::size_t>(kMaxTapeTexts))
      d.tapeTexts.emplace_back(reinterpret_cast<const char*>(at), len);
  }
  sanitizeDecor(d);
  out = std::move(d);
  return true;
}
} // namespace detail

// The decor alone (the payload of the "Decor" message, messages.h). False leaves `out` untouched.
inline bool deserializeDecor(const std::uint8_t* data, std::size_t size, Decor& out) {
  if (!data) return false;
  detail::Reader r{data, size};
  return detail::readDecor(r, out);
}

// norm: kNumParams normalized values in ParamID order. Clears `out` first. Always writes v2.
inline void serializeState(const double* norm, const Decor& decor, std::vector<std::uint8_t>& out) {
  out.clear();
  out.reserve(kStateSizeV2 + 64);
  detail::putLE(out, static_cast<std::uint32_t>(kStateVersion), 4);
  detail::putLE(out, static_cast<std::uint32_t>(kNumParams), 4);
  for (int i = 0; i < static_cast<int>(kNumParams); ++i) {
    std::uint64_t bits;
    std::memcpy(&bits, &norm[i], sizeof bits);
    detail::putLE(out, bits, 8);
  }
  serializeDecor(decor, out);
}

// Reads v1 or v2. Fills `norm` (kNumParams values): the defaults, then every whole value in the blob; fills `decor`
// with the stored decor, or the default one (v1, or a v2 whose decor part is corrupt). Returns false and leaves
// `norm` and `decor` untouched on an unknown version or when there is no room for the version.
inline bool deserializeState(const std::uint8_t* data, std::size_t size, double* norm, Decor& decor) {
  if (!data || !norm || size < kStateHeaderBytes) return false;
  const std::uint64_t version = detail::getLE(data, 4);
  if (version < 1 || version > static_cast<std::uint64_t>(kStateVersion)) return false;
  double tmp[kNumParams];
  loadDefaults(tmp);
  // v1 has no count: the values are whatever follows the version. v2 announces them.
  std::size_t announced = (size - kStateHeaderBytes) / sizeof(double);
  std::size_t first = kStateHeaderBytes;
  if (version >= 2) {
    if (size < kStateHeaderBytesV2) return false;
    announced = static_cast<std::size_t>(detail::getLE(data + kStateHeaderBytes, 4));
    first = kStateHeaderBytesV2;
  }
  const std::size_t present = std::min<std::size_t>(announced, (size - first) / sizeof(double));   // a cut blob
  const std::size_t stored = std::min<std::size_t>(present, kNumParams);
  for (std::size_t i = 0; i < stored; ++i) {
    const std::uint64_t bits = detail::getLE(data + first + i * sizeof(double), 8);
    double v;
    std::memcpy(&v, &bits, sizeof v);
    if (std::isfinite(v)) tmp[i] = std::clamp(v, 0.0, 1.0);
  }
  Decor d;   // default: seed 0, no angles, no texts
  if (version >= 2 && present == announced) {   // all the announced parameters are there: the decor follows them
    detail::Reader r{data, size, first + announced * sizeof(double)};
    detail::readDecor(r, d);   // corrupt: `d` stays the default decor (readDecor leaves it untouched)
  }
  std::memcpy(norm, tmp, sizeof tmp);
  decor = std::move(d);
  return true;
}

} // namespace FairyTapes::HappyEnding
