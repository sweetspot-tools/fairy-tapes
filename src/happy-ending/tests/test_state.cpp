/**
 * Fairy Tapes · Happy Ending — Unit tests for the versioned plug-in state (pure, no SDK libs).
 * Run via CTest; build target: happy_ending_state_tests.
 */

#include "messages.h"
#include "params.h"
#include "state.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace FairyTapes::HappyEnding;

namespace {

int g_failed = 0;
int g_passed = 0;

#define ASSERT_TRUE(expr) do { \
  if (!(expr)) { \
    std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); \
    ++g_failed; return; \
  } \
} while(0)

#define RUN_TEST(fn) do { \
  const int before = g_failed; \
  fn(); \
  if (g_failed == before) { ++g_passed; std::printf("PASS %s\n", #fn); } \
  else std::printf("FAIL %s\n", #fn); \
} while(0)

constexpr std::size_t kHeaderBytes = 4;
constexpr std::size_t kV1Bytes = kHeaderBytes + sizeof(double) * kNumParams;
constexpr std::size_t kEmptyDecorBytes = 4 + 4 * 4 + 4;   // seed + 4 angles + text count

// A version-1 blob built by hand (the layout older builds wrote): int32 1, then the values as float64.
std::vector<uint8_t> v1Blob(const double* norm) {
  std::vector<uint8_t> out;
  detail::putLE(out, 1u, 4);
  for (int i = 0; i < kNumParams; ++i) {
    uint64_t bits;
    std::memcpy(&bits, &norm[i], sizeof bits);
    detail::putLE(out, bits, 8);
  }
  return out;
}

// A v2 blob with the given decor.
std::vector<uint8_t> v2Blob(const double* norm, const Decor& d) {
  std::vector<uint8_t> out;
  serializeState(norm, d, out);
  return out;
}

Decor sampleDecor() {
  Decor d;
  d.seed = 0xC0FFEE12u;
  d.screwAngles[0] = 0.f;
  d.screwAngles[1] = 90.5f;
  d.screwAngles[2] = 180.25f;
  d.screwAngles[3] = 359.f;
  d.tapeTexts = {"VOX", "LEAD 2"};
  return d;
}

bool sameDecor(const Decor& a, const Decor& b) {
  if (a.seed != b.seed || a.tapeTexts != b.tapeTexts) return false;
  for (int i = 0; i < kNumScrews; ++i)
    if (a.screwAngles[i] != b.screwAngles[i]) return false;
  return true;
}

bool isDefaultDecor(const Decor& d) { return sameDecor(d, Decor{}); }

// Every parameter one step away from its default (wrapping), as a normalized value.
void nonDefaultNorms(double* norm) {
  for (int i = 0; i < kNumParams; ++i) {
    const auto id = static_cast<ParamID>(i);
    norm[i] = normFromIndex(id, (kParams[id].defaultIndex + 1) % kParams[id].steps);
  }
}

bool sameNorms(const double* a, const double* b) {
  return std::memcmp(a, b, sizeof(double) * kNumParams) == 0;
}

void testLoadDefaults() {
  double d[kNumParams];
  loadDefaults(d);
  for (int i = 0; i < kNumParams; ++i) {
    const auto id = static_cast<ParamID>(i);
    ASSERT_TRUE(d[i] == normFromIndex(id, kParams[id].defaultIndex));
    ASSERT_TRUE(indexFromNorm(id, d[i]) == kParams[id].defaultIndex);
  }
}

void testRoundTrip() {
  double in[kNumParams], out[kNumParams];
  nonDefaultNorms(in);
  for (int i = 0; i < kNumParams; ++i) {
    const auto id = static_cast<ParamID>(i);
    ASSERT_TRUE(indexFromNorm(id, in[i]) != kParams[id].defaultIndex);  // really not the default
  }
  std::vector<uint8_t> bytes;
  serializeState(in, Decor{}, bytes);
  ASSERT_TRUE(bytes.size() == kV1Bytes + kEmptyDecorBytes);
  loadDefaults(out);
  Decor d;
  ASSERT_TRUE(deserializeState(bytes.data(), bytes.size(), out, d));
  ASSERT_TRUE(sameNorms(in, out));
}

void testRoundTripKeepsOversamplingAndService() {
  // OVERSAMPLING is a project setting: it is stored (spec 9)
  double in[kNumParams], out[kNumParams];
  loadDefaults(in);
  in[kOversampling] = normFromIndex(kOversampling, 2);
  in[kCeiling] = normFromIndex(kCeiling, 3);
  std::vector<uint8_t> bytes;
  serializeState(in, Decor{}, bytes);
  Decor d;
  ASSERT_TRUE(deserializeState(bytes.data(), bytes.size(), out, d));
  ASSERT_TRUE(indexFromNorm(kOversampling, out[kOversampling]) == 2);
  ASSERT_TRUE(indexFromNorm(kCeiling, out[kCeiling]) == 3);
}

void testRejectsUnknownVersion() {
  double in[kNumParams];
  nonDefaultNorms(in);
  std::vector<uint8_t> bytes;
  serializeState(in, Decor{}, bytes);
  for (uint32_t bad : {0u, 3u, 99u, 0xFFFFFFFFu}) {
    std::vector<uint8_t> b = bytes;
    for (int k = 0; k < 4; ++k) b[static_cast<std::size_t>(k)] = static_cast<uint8_t>(bad >> (8 * k));
    double out[kNumParams];
    loadDefaults(out);
    double before[kNumParams];
    std::memcpy(before, out, sizeof out);
    Decor keep = sampleDecor();
    ASSERT_TRUE(!deserializeState(b.data(), b.size(), out, keep));
    ASSERT_TRUE(sameNorms(before, out));  // untouched on refusal
    ASSERT_TRUE(sameDecor(keep, sampleDecor()));
  }
  double out[kNumParams];
  Decor d;
  ASSERT_TRUE(!deserializeState(nullptr, 0, out, d));
  ASSERT_TRUE(!deserializeState(bytes.data(), 3, out, d));  // not even a version
}

void testListenAndSetNotStored() {
  // The blob is exactly the version, kNumParams values and the decor: nothing for the hidden session parameters,
  // whose IDs lie beyond the table.
  ASSERT_TRUE(static_cast<int>(kListenBand) >= kNumParams && static_cast<int>(kSetActive) >= kNumParams);
  double in[kNumParams];
  loadDefaults(in);
  std::vector<uint8_t> bytes;
  serializeState(in, Decor{}, bytes);
  ASSERT_TRUE(bytes.size() == kHeaderBytes + sizeof(double) * kNumParams + kEmptyDecorBytes);
  // The session parameters start at LISTEN off (-1) and SET off.
  ASSERT_TRUE(listenFromNorm(normFromListen(-1)) == -1);
  ASSERT_TRUE(kDefaultListenNorm == normFromListen(-1) && kDefaultSetNorm == 0.0);
  for (int band = -1; band <= 2; ++band) ASSERT_TRUE(listenFromNorm(normFromListen(band)) == band);
}

void test_state_v1_short_blob_still_loads() {
  // An older build had fewer parameters (the table only grows at the end): what is there loads,
  // the rest keeps its default, the decor is the default one.
  double in[kNumParams];
  nonDefaultNorms(in);
  const std::vector<uint8_t> bytes = v1Blob(in);
  const int kept = 10;
  double out[kNumParams], defaults[kNumParams];
  loadDefaults(defaults);
  loadDefaults(out);
  const std::size_t cut = kHeaderBytes + sizeof(double) * kept + 3;  // also cut inside a value
  Decor d = sampleDecor();
  ASSERT_TRUE(deserializeState(bytes.data(), cut, out, d));
  for (int i = 0; i < kNumParams; ++i) ASSERT_TRUE(out[i] == (i < kept ? in[i] : defaults[i]));
  ASSERT_TRUE(isDefaultDecor(d));

  // only the version: everything default
  for (int i = 0; i < kNumParams; ++i) out[i] = 0.123;
  d = sampleDecor();
  ASSERT_TRUE(deserializeState(bytes.data(), kHeaderBytes, out, d));
  ASSERT_TRUE(sameNorms(defaults, out));
  ASSERT_TRUE(isDefaultDecor(d));
}

void testExtraBytesIgnoredAndValuesSanitized() {
  double in[kNumParams], out[kNumParams], defaults[kNumParams];
  loadDefaults(defaults);
  loadDefaults(in);
  in[kInputGain] = 7.5;                   // out of range: clamped to 1
  in[kOutputGain] = -3.0;                 // clamped to 0
  in[kCeiling] = std::nan("");            // not finite: that parameter keeps its default
  in[kLoudness] = std::numeric_limits<double>::infinity();
  std::vector<uint8_t> bytes = v1Blob(in);
  bytes.insert(bytes.end(), 16, 0xAB);    // future data after the known layout (a v1 blob has no decor)
  Decor d;
  ASSERT_TRUE(deserializeState(bytes.data(), bytes.size(), out, d));
  ASSERT_TRUE(isDefaultDecor(d));
  ASSERT_TRUE(out[kInputGain] == 1.0);
  ASSERT_TRUE(out[kOutputGain] == 0.0);
  ASSERT_TRUE(out[kCeiling] == defaults[kCeiling]);
  ASSERT_TRUE(out[kLoudness] == defaults[kLoudness]);
  for (int i = 0; i < kNumParams; ++i) ASSERT_TRUE(std::isfinite(out[i]) && out[i] >= 0.0 && out[i] <= 1.0);

  // the same after a v2 blob: bytes after the decor are ignored, the decor is read
  const Decor sample = sampleDecor();
  bytes = v2Blob(in, sample);
  bytes.insert(bytes.end(), 16, 0xAB);
  Decor got;
  ASSERT_TRUE(deserializeState(bytes.data(), bytes.size(), out, got));
  ASSERT_TRUE(sameDecor(got, sample));
  ASSERT_TRUE(out[kInputGain] == 1.0 && out[kCeiling] == defaults[kCeiling]);
}

void testLayoutIsLittleEndianVersionTwo() {
  double in[kNumParams];
  loadDefaults(in);
  std::vector<uint8_t> bytes;
  serializeState(in, Decor{}, bytes);
  ASSERT_TRUE(kStateVersion == 2);
  ASSERT_TRUE(bytes[0] == 2 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 0);
  double first;
  std::memcpy(&first, bytes.data() + kHeaderBytes, sizeof first);  // little-endian host assumed by the test only
  ASSERT_TRUE(first == in[0]);
  // the decor follows the parameters: uint32 seed (here 0x01020304), 4 float32 angles, uint32 text count, texts
  Decor d;
  d.seed = 0x01020304u;
  d.screwAngles[0] = 1.f;
  d.tapeTexts = {"AB"};
  serializeState(in, d, bytes);
  const std::size_t at = kV1Bytes;
  ASSERT_TRUE(bytes.size() == kV1Bytes + kEmptyDecorBytes + 4 + 2);
  ASSERT_TRUE(bytes[at] == 4 && bytes[at + 1] == 3 && bytes[at + 2] == 2 && bytes[at + 3] == 1);
  float angle;
  std::memcpy(&angle, bytes.data() + at + 4, sizeof angle);
  ASSERT_TRUE(angle == 1.f);
  const std::size_t count = at + 4 + 16;
  ASSERT_TRUE(bytes[count] == 1 && bytes[count + 4] == 2 && bytes[count + 8] == 'A' && bytes[count + 9] == 'B');
}

void test_state_v2_roundtrip() {
  double in[kNumParams], out[kNumParams];
  nonDefaultNorms(in);
  const Decor decor = sampleDecor();
  std::vector<uint8_t> bytes;
  serializeState(in, decor, bytes);
  ASSERT_TRUE(bytes.size() > kV1Bytes + kEmptyDecorBytes);
  ASSERT_TRUE(bytes.size() <= kMaxStateBytes);
  loadDefaults(out);
  Decor got;
  ASSERT_TRUE(deserializeState(bytes.data(), bytes.size(), out, got));
  ASSERT_TRUE(sameNorms(in, out));
  ASSERT_TRUE(sameDecor(got, decor));
  // and through the decor alone
  std::vector<uint8_t> db;
  serializeDecor(decor, db);
  Decor g2;
  ASSERT_TRUE(deserializeDecor(db.data(), db.size(), g2));
  ASSERT_TRUE(sameDecor(g2, decor));
  ASSERT_TRUE(db.size() + kV1Bytes == bytes.size());
}

void test_state_v1_loads_with_default_decor() {
  double in[kNumParams], out[kNumParams];
  nonDefaultNorms(in);
  const std::vector<uint8_t> blob = v1Blob(in);
  ASSERT_TRUE(blob.size() == kV1Bytes && blob.size() == kStateSizeV1);
  loadDefaults(out);
  Decor d = sampleDecor();   // must be replaced by the default
  ASSERT_TRUE(deserializeState(blob.data(), blob.size(), out, d));
  ASSERT_TRUE(sameNorms(in, out));
  ASSERT_TRUE(d.seed == 0 && d.tapeTexts.empty());
  for (float a : d.screwAngles) ASSERT_TRUE(a == 0.f);
  ASSERT_TRUE(ensureSeed(d));
  ASSERT_TRUE(d.seed != 0);
  const uint32_t first = d.seed;
  ASSERT_TRUE(!ensureSeed(d));   // stable once there
  ASSERT_TRUE(d.seed == first);
  Decor z;   // a generator answering 0 still gives a non-zero seed
  ASSERT_TRUE(ensureSeed(z, [] { return 0u; }) && z.seed != 0);
}

void test_state_v2_corrupt_decor() {
  // A v2 blob whose decor part is damaged still loads the parameters and gives the default decor.
  double in[kNumParams], out[kNumParams];
  nonDefaultNorms(in);
  const Decor decor = sampleDecor();
  const std::vector<uint8_t> blob = v2Blob(in, decor);
  const auto loadsParamsWithDefaultDecor = [&](const std::vector<uint8_t>& b, std::size_t n) {
    loadDefaults(out);
    Decor d = sampleDecor();
    return deserializeState(b.data(), n, out, d) && sameNorms(in, out) && isDefaultDecor(d);
  };

  // decor cut at every length, from none of it to one byte short
  for (std::size_t n = kV1Bytes; n < blob.size(); ++n) ASSERT_TRUE(loadsParamsWithDefaultDecor(blob, n));

  const std::size_t countAt = kV1Bytes + 4 + 16;
  std::vector<uint8_t> bad(blob.begin(), blob.begin() + static_cast<std::ptrdiff_t>(countAt));   // huge text count
  detail::putLE(bad, 0xFFFFFFFFull, 4);
  bad.insert(bad.end(), blob.begin() + static_cast<std::ptrdiff_t>(countAt) + 4, blob.end());
  ASSERT_TRUE(loadsParamsWithDefaultDecor(bad, bad.size()));

  bad.assign(blob.begin(), blob.begin() + static_cast<std::ptrdiff_t>(countAt) + 4);   // a length beyond the blob
  detail::putLE(bad, 100u, 4);
  bad.push_back('a');
  ASSERT_TRUE(loadsParamsWithDefaultDecor(bad, bad.size()));

  bad.assign(blob.begin(), blob.begin() + static_cast<std::ptrdiff_t>(countAt) + 4);   // a length over the stored limit
  detail::putLE(bad, 100000u, 4);
  bad.insert(bad.end(), 100000, 'a');
  ASSERT_TRUE(loadsParamsWithDefaultDecor(bad, bad.size()));

  // a v2 blob cut inside the parameters: those that are there load, the others keep their defaults, decor default
  double defaults[kNumParams];
  loadDefaults(defaults);
  loadDefaults(out);
  Decor d = sampleDecor();
  const int kept = 10;
  ASSERT_TRUE(deserializeState(blob.data(), kHeaderBytes + sizeof(double) * kept + 3, out, d));
  for (int i = 0; i < kNumParams; ++i) ASSERT_TRUE(out[i] == (i < kept ? in[i] : defaults[i]));
  ASSERT_TRUE(isDefaultDecor(d));

  // the decor alone is refused, and leaves its target untouched
  std::vector<uint8_t> db;
  serializeDecor(decor, db);
  Decor dk;
  dk.seed = 5;
  for (std::size_t n = 0; n < db.size(); ++n) ASSERT_TRUE(!deserializeDecor(db.data(), n, dk) && dk.seed == 5);
  ASSERT_TRUE(!deserializeDecor(nullptr, 0, dk));
}

void test_decor_sanitised() {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  Decor d;
  d.seed = 3;
  d.screwAngles[0] = nan;
  d.screwAngles[1] = inf;
  d.screwAngles[2] = 1000.f;
  d.screwAngles[3] = -1000.f;
  d.tapeTexts = {std::string("a\x01" "b\n\xE9z"), std::string(40, 'x'), "  pad  ", "4", "5", "6", "7", "8"};
  std::vector<uint8_t> bytes;
  serializeDecor(d, bytes);
  Decor got;
  ASSERT_TRUE(deserializeDecor(bytes.data(), bytes.size(), got));
  ASSERT_TRUE(got.seed == 3);
  ASSERT_TRUE(got.screwAngles[0] == 0.f && got.screwAngles[1] == 0.f);
  ASSERT_TRUE(std::fabs(got.screwAngles[2] - 280.f) < 1e-3f);
  ASSERT_TRUE(std::fabs(got.screwAngles[3] - 80.f) < 1e-3f);
  for (float a : got.screwAngles) ASSERT_TRUE(a >= 0.f && a < 360.f);
  ASSERT_TRUE(got.tapeTexts.size() == static_cast<std::size_t>(kMaxTapeTexts));   // 8 texts -> 6
  ASSERT_TRUE(got.tapeTexts[0] == "abz");   // control bytes and the non-ASCII byte are dropped
  for (const auto& t : got.tapeTexts) {
    ASSERT_TRUE(static_cast<int>(t.size()) <= kMaxTapeChars);
    for (char c : t) ASSERT_TRUE(c >= 0x20 && c <= 0x7E);
  }
  ASSERT_TRUE(got.tapeTexts[1] == std::string(14, 'x'));
  ASSERT_TRUE(got.tapeTexts[2] == "pad");

  // A hand-made blob with raw junk is sanitised on reading too (the writer is not trusted).
  std::vector<uint8_t> raw;
  detail::putLE(raw, 9u, 4);
  const float rawAngles[4] = {nan, -90.f, 720.f, 45.f};
  for (float a : rawAngles) {
    uint32_t bits;
    std::memcpy(&bits, &a, 4);
    detail::putLE(raw, bits, 4);
  }
  detail::putLE(raw, 7u, 4);
  for (int i = 0; i < 7; ++i) {
    detail::putLE(raw, 40u, 4);
    raw.insert(raw.end(), 40, i == 0 ? '\x07' : 'q');
  }
  ASSERT_TRUE(deserializeDecor(raw.data(), raw.size(), got));
  ASSERT_TRUE(got.screwAngles[0] == 0.f && got.screwAngles[1] == 270.f && got.screwAngles[2] == 0.f);
  ASSERT_TRUE(got.tapeTexts.size() == static_cast<std::size_t>(kMaxTapeTexts) && got.tapeTexts[0].empty());
  ASSERT_TRUE(got.tapeTexts[1] == std::string(14, 'q'));
}

// Stand-in for Steinberg::Vst::IMessage / IAttributeList.
struct FakeAttrs {
  std::vector<uint8_t> blob;
  bool has = false;
  int setBinary(const char* id, const void* d, unsigned n) {
    if (std::strcmp(id, "decor") != 0) return 1;
    blob.assign(static_cast<const uint8_t*>(d), static_cast<const uint8_t*>(d) + n);
    has = true;
    return 0;
  }
  int getBinary(const char* id, const void*& d, unsigned& n) {
    if (std::strcmp(id, "decor") != 0 || !has) return 1;
    d = blob.data();
    n = static_cast<unsigned>(blob.size());
    return 0;
  }
};
struct FakeMsg {
  std::string id;
  FakeAttrs attrs;
  void setMessageID(const char* s) { id = s; }
  const char* getMessageID() { return id.c_str(); }
  FakeAttrs* getAttributes() { return &attrs; }
};

void test_decor_message_roundtrip() {
  Decor d = sampleDecor();
  FakeMsg m;
  encodeDecorMessage(&m, d);
  ASSERT_TRUE(m.id == kMsgDecor && m.id == "Decor");
  Decor got;
  ASSERT_TRUE(decodeDecorMessage(&m, got));
  ASSERT_TRUE(sameDecor(got, d));

  Decor keep;
  keep.seed = 1;
  FakeMsg other = m;
  other.id = "Oversampling";   // the other message of this plug-in is not a decor
  ASSERT_TRUE(!decodeDecorMessage(&other, keep) && keep.seed == 1);
  FakeMsg corrupt = m;
  corrupt.attrs.blob.resize(corrupt.attrs.blob.size() - 1);   // cuts the last text
  ASSERT_TRUE(!decodeDecorMessage(&corrupt, keep) && keep.seed == 1);
  FakeMsg empty;
  empty.id = kMsgDecor;   // attribute missing
  ASSERT_TRUE(!decodeDecorMessage(&empty, keep) && keep.seed == 1);
  FakeMsg huge = m;
  huge.attrs.blob.assign(kMaxStateBytes + 1, 0);
  ASSERT_TRUE(!decodeDecorMessage(&huge, keep) && keep.seed == 1);
  ASSERT_TRUE(!decodeDecorMessage<FakeMsg>(nullptr, keep));
}

} // namespace

int main() {
  RUN_TEST(testLoadDefaults);
  RUN_TEST(testRoundTrip);
  RUN_TEST(testRoundTripKeepsOversamplingAndService);
  RUN_TEST(testRejectsUnknownVersion);
  RUN_TEST(testListenAndSetNotStored);
  RUN_TEST(test_state_v1_short_blob_still_loads);
  RUN_TEST(testExtraBytesIgnoredAndValuesSanitized);
  RUN_TEST(testLayoutIsLittleEndianVersionTwo);
  RUN_TEST(test_state_v2_roundtrip);
  RUN_TEST(test_state_v1_loads_with_default_decor);
  RUN_TEST(test_state_v2_corrupt_decor);
  RUN_TEST(test_decor_sanitised);
  RUN_TEST(test_decor_message_roundtrip);
  std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
  return g_failed ? 1 : 0;
}
