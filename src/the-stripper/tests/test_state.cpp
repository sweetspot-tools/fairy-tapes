/**
 * Fairy Tapes · The Stripper — state and frozen parameter table tests.
 * Header-only (state.h) and independent of the VST3 SDK: an in-memory stream stands in for IBStreamer.
 */

#include "state.h"
#include "param_table_golden.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace FairyTapes::TheStripper;

namespace {
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

/// Minimal little-endian stream with the interface state.h needs.
struct MemStream {
  std::vector<unsigned char> buf;
  std::size_t pos = 0;
  bool writeInt32(std::int32_t v) { put(&v, sizeof v); return true; }
  bool writeDouble(double v) { put(&v, sizeof v); return true; }
  long long writeRaw(const void* p, std::int32_t n) { put(p, static_cast<std::size_t>(n)); return n; }
  bool readInt32(std::int32_t& v) { return take(&v, sizeof v); }
  bool readDouble(double& v) { return take(&v, sizeof v); }
  long long readRaw(void* p, std::int32_t n) { return take(p, static_cast<std::size_t>(n)) ? n : 0; }
  void put(const void* p, std::size_t n) { const auto* c = static_cast<const unsigned char*>(p); buf.insert(buf.end(), c, c + n); }
  bool take(void* p, std::size_t n) { if (pos + n > buf.size()) return false; std::memcpy(p, buf.data() + pos, n); pos += n; return true; }
};

// ---- the parameter ids never move -------------------------------------------------------
#define X(name, id, def) static_assert(static_cast<int>(name) == (id), "parameter id changed: " #name);
GOLDEN_PARAMS(X)
#undef X

void test_new_params_appended_at_end() {
  CHECK(kLimiterPound == kNumParams - 3);
  CHECK(kGateThreshold == kNumParams - 2);
  CHECK(kDriveNamOn == kNumParams - 1);
  CHECK(kNumParamsV8 == kGateThreshold + 1);
  CHECK(kDriveAmount == kNumParams - 4);
  CHECK(kNumParamsV7 == kDriveAmount + 1);
}

void test_param_table_matches_golden() {
  double d[kNumParams];
  fillDefaultParams(d);
  int rows = 0;
#define X(name, id, def) \
  ++rows; \
  if (std::fabs(d[(id)] - (def)) > 1e-12) { std::fprintf(stderr, "  default of " #name " changed: %.17g vs %.17g\n", d[(id)], (double)(def)); ++g_failed; }
  GOLDEN_PARAMS(X)
#undef X
  CHECK(rows == kNumParams);   // a parameter added without extending the golden table fails here
}

void test_hpf_default_is_20_hz() {
  CHECK(std::fabs(kDefaultEqHPFFreq - 0.0) < 1e-12);
  CHECK(std::fabs(eqNormToHPFFreq(kDefaultEqHPFFreq) - 20.0f) < 1e-4f);
  CHECK(std::fabs(eqNormToLPFFreq(kDefaultEqLPFFreq) - 20000.0f) < 1e-2f);
}

// ---- state v8 ---------------------------------------------------------------------------
void test_state_v8_roundtrip() {
  PluginState in;
  for (int i = 0; i < kNumParams; ++i) in.params[i] = (i + 1) / 40.0;
  in.modelPath = "/home/utente/Modelli/Chitarra \xC3\xA8 \xE2\x82\xAC/amp.nam";   // non-ASCII UTF-8
  MemStream s;
  CHECK(writeState(s, in));
  PluginState out;
  CHECK(readState(s, out));
  for (int i = 0; i < kNumParams; ++i) CHECK(out.params[i] == in.params[i]);
  CHECK(out.modelPath == in.modelPath);
  CHECK(s.pos == s.buf.size());   // everything consumed
}

void test_state_v8_empty_path() {
  PluginState in; fillDefaultParams(in.params);
  MemStream s; CHECK(writeState(s, in));
  PluginState out; CHECK(readState(s, out));
  CHECK(out.modelPath.empty());
}

// ---- state v9: the NAM / Saturate switch ------------------------------------------------
void test_state_v8_loads_with_nam_on() {
  MemStream s;
  s.writeInt32(8);
  for (int i = 0; i < kNumParamsV8; ++i) s.writeDouble(0.01 * (i + 1));
  s.writeInt32(0);   // empty model path
  PluginState out; CHECK(readState(s, out));
  for (int i = 0; i < kNumParamsV8; ++i) CHECK(out.params[i] == 0.01 * (i + 1));
  CHECK(out.params[kDriveNamOn] == 1.0);   // NAM on, as before the switch existed
  CHECK(s.pos == s.buf.size());
}

void test_state_v9_keeps_the_switch() {
  PluginState in; fillDefaultParams(in.params);
  in.params[kDriveNamOn] = 0.0;
  MemStream s; CHECK(writeState(s, in));
  PluginState out; CHECK(readState(s, out));
  CHECK(out.params[kDriveNamOn] == 0.0);
}

// ---- state v10: wear seed and the writing on the tape pieces ---------------------------------
void test_state_v10_roundtrip_with_seed_and_tape() {
  PluginState in; fillDefaultParams(in.params);
  in.modelPath = "/m/amp.nam";
  in.wearSeed = 0xDEADBEEFu;
  in.tapeTexts = {{true, "TAKE 3"}, {false, ""}, {true, ""}, {true, "keep! \xC3\xA8"}};
  MemStream s; CHECK(writeState(s, in));
  PluginState out; CHECK(readState(s, out));
  CHECK(out.modelPath == in.modelPath);
  CHECK(out.wearSeed == 0xDEADBEEFu);
  CHECK(out.tapeTexts.size() == 4);
  CHECK(out.tapeTexts[0].set && out.tapeTexts[0].text == "TAKE 3");
  CHECK(!out.tapeTexts[1].set);              // never written: the default text is shown
  CHECK(out.tapeTexts[2].set && out.tapeTexts[2].text.empty());   // erased on purpose: stays empty, not the default
  CHECK(out.tapeTexts[3].set && out.tapeTexts[3].text == "keep! \xC3\xA8");
  CHECK(s.pos == s.buf.size());
}

void test_state_v9_loads_without_seed_or_tape() {
  MemStream s;
  s.writeInt32(9);
  for (int i = 0; i < kNumParams; ++i) s.writeDouble(0.25);
  s.writeInt32(0);
  PluginState out; CHECK(readState(s, out));
  CHECK(out.wearSeed == 0 && out.tapeTexts.empty());   // 0 = "none yet": the controller makes one
  CHECK(s.pos == s.buf.size());
}

void test_state_v10_limits_and_corruption() {
  PluginState in; fillDefaultParams(in.params);
  in.tapeTexts.assign(20, {true, "x"});                                   // too many pieces: only the first kMaxTapePieces are kept
  in.tapeTexts[0].text = std::string(500, 'a');                           // too long: cut when writing
  MemStream s; CHECK(writeState(s, in));
  PluginState out; CHECK(readState(s, out));
  CHECK(out.tapeTexts.size() == static_cast<std::size_t>(kMaxTapePieces));
  CHECK(out.tapeTexts[0].text.size() <= static_cast<std::size_t>(kMaxTapeTextBytes));
  // a stream that claims more than the limit is rejected
  { MemStream b; b.writeInt32(10); for (int i = 0; i < kNumParams; ++i) b.writeDouble(0.5); b.writeInt32(0); b.writeInt32(7); b.writeInt32(1000); PluginState o; CHECK(!readState(b, o)); }
  { MemStream b; b.writeInt32(10); for (int i = 0; i < kNumParams; ++i) b.writeDouble(0.5); b.writeInt32(0); b.writeInt32(7); b.writeInt32(1); b.writeInt32(9999); PluginState o; CHECK(!readState(b, o)); }
  { MemStream b; b.writeInt32(10); for (int i = 0; i < kNumParams; ++i) b.writeDouble(0.5); b.writeInt32(0); b.writeInt32(7); b.writeInt32(-3); PluginState o; CHECK(!readState(b, o)); }
  { MemStream b; b.writeInt32(10); for (int i = 0; i < kNumParams; ++i) b.writeDouble(0.5); b.writeInt32(0); PluginState o; CHECK(!readState(b, o)); }   // v10 must have the seed
}

// ---- state v11: screw angles, recall marks, recall mode ---------------------------------------
void test_state_v11_roundtrip() {
  PluginState in; fillDefaultParams(in.params);
  in.hasScrewAngles = true;
  for (int i = 0; i < 4; ++i) in.screwAngles[i] = 10.5 + 100 * i;
  in.recallValid = true;
  for (int i = 0; i < kNumParams; ++i) in.recall[i] = (i % 7) / 7.0;
  in.recallMode = 1;
  MemStream s; CHECK(writeState(s, in));
  PluginState out; CHECK(readState(s, out));
  CHECK(out.hasScrewAngles);
  for (int i = 0; i < 4; ++i) CHECK(out.screwAngles[i] == 10.5 + 100 * i);
  CHECK(out.recallValid && out.recallMode == 1);
  for (int i = 0; i < kNumParams; ++i) CHECK(out.recall[i] == (i % 7) / 7.0);
  CHECK(s.pos == s.buf.size());
}

void test_state_v11_defaults_and_old_states() {
  PluginState in; fillDefaultParams(in.params);   // nothing set
  MemStream s; CHECK(writeState(s, in));
  PluginState out; CHECK(readState(s, out));
  CHECK(!out.hasScrewAngles && !out.recallValid && out.recallMode == 0);   // Auto is the default
  // a v10 state (seed and tape, nothing after) loads with the new fields at their defaults
  MemStream t; t.writeInt32(10); for (int i = 0; i < kNumParams; ++i) t.writeDouble(0.5); t.writeInt32(0); t.writeInt32(77); t.writeInt32(0);
  PluginState old; CHECK(readState(t, old));
  CHECK(old.wearSeed == 77 && !old.hasScrewAngles && !old.recallValid && old.recallMode == 0);
  CHECK(t.pos == t.buf.size());
}

void test_state_v11_rejects_corruption_and_clamps() {
  auto head = [](MemStream& b) { b.writeInt32(11); for (int i = 0; i < kNumParams; ++i) b.writeDouble(0.5); b.writeInt32(0); b.writeInt32(1); b.writeInt32(0); };
  { MemStream b; head(b); b.writeInt32(3); PluginState o; CHECK(!readState(b, o)); }            // 3 screws: neither 0 nor 4
  { MemStream b; head(b); b.writeInt32(4); b.writeDouble(1); PluginState o; CHECK(!readState(b, o)); }   // truncated
  { MemStream b; head(b); b.writeInt32(0); b.writeInt32(2); PluginState o; CHECK(!readState(b, o)); }    // recall flag must be 0 or 1
  { MemStream b; head(b); b.writeInt32(0); b.writeInt32(0); b.writeInt32(5); PluginState o; CHECK(!readState(b, o)); }   // unknown recall mode
  { MemStream b; head(b); PluginState o; CHECK(!readState(b, o)); }                               // v11 needs its tail
  // non-finite angles are replaced, recall values are clamped to 0..1
  MemStream b; head(b); b.writeInt32(4);
  b.writeDouble(std::nan("")); b.writeDouble(400.0); b.writeDouble(-20.0); b.writeDouble(1e300);
  b.writeInt32(1); for (int i = 0; i < kNumParams; ++i) b.writeDouble(i == 0 ? 7.0 : i == 1 ? -3.0 : i == 2 ? std::nan("") : 0.25);
  b.writeInt32(0);
  PluginState o; CHECK(readState(b, o));
  CHECK(o.hasScrewAngles);
  for (int i = 0; i < 4; ++i) CHECK(std::isfinite(o.screwAngles[i]) && o.screwAngles[i] >= 0.0 && o.screwAngles[i] < 360.0);
  CHECK(o.recallValid && o.recall[0] == 1.0 && o.recall[1] == 0.0 && o.recall[2] == 0.0 && o.recall[3] == 0.25);
}

// ---- older states keep loading ----------------------------------------------------------
void test_state_v7_loads_with_defaults() {
  MemStream s;
  s.writeInt32(7);
  for (int i = 0; i < kNumParamsV7; ++i) s.writeDouble(0.01 * (i + 1));
  PluginState out; CHECK(readState(s, out));
  for (int i = 0; i < kNumParamsV7; ++i) CHECK(out.params[i] == 0.01 * (i + 1));
  CHECK(out.params[kLimiterPound] == kDefaultLimiterPound);     // new parameters take their defaults
  CHECK(out.params[kGateThreshold] == kDefaultGateThreshold);
  CHECK(out.modelPath.empty());
  CHECK(s.pos == s.buf.size());
}

void test_state_v6_skips_drive_output_and_mix() {
  MemStream s;
  s.writeInt32(6);
  for (int i = 0; i < 35; ++i) s.writeDouble(0.01 * (i + 1));
  PluginState out; CHECK(readState(s, out));
  CHECK(out.params[kDriveAmount] == 0.01 * (kDriveAmount + 1));    // slot 32 survives
  CHECK(out.params[kLimiterPound] == kDefaultLimiterPound);
}

void test_state_v5_gets_default_drive() {
  MemStream s;
  s.writeInt32(5);
  for (int i = 0; i < 32; ++i) s.writeDouble(0.02 * (i + 1));
  PluginState out; CHECK(readState(s, out));
  CHECK(out.params[0] == 0.02);
  CHECK(out.params[kDriveAmount] == kDefaultDriveAmount);
}

void test_state_v4_drops_old_eq_type() {
  MemStream s;
  s.writeInt32(4);
  for (int i = 0; i < 33; ++i) s.writeDouble(0.01 * (i + 1));
  PluginState out; CHECK(readState(s, out));
  CHECK(out.params[30] == 0.01 * 31);       // slot 31 (old kEqType) is skipped
  CHECK(out.params[31] == 0.01 * 33);
}

void test_state_unknown_or_truncated_rejected() {
  { MemStream s; s.writeInt32(99); PluginState o; CHECK(!readState(s, o)); }
  { MemStream s; s.writeInt32(0);  PluginState o; CHECK(!readState(s, o)); }
  { MemStream s; s.writeInt32(8); s.writeDouble(0.5); PluginState o; CHECK(!readState(s, o)); }                 // truncated params
  { MemStream s; s.writeInt32(9); for (int i = 0; i < kNumParams; ++i) s.writeDouble(0.5); s.writeInt32(100); PluginState o; CHECK(!readState(s, o)); }   // path length beyond the data
  { MemStream s; s.writeInt32(9); for (int i = 0; i < kNumParams; ++i) s.writeDouble(0.5); s.writeInt32(-5); PluginState o; CHECK(!readState(s, o)); }
  { MemStream s; PluginState o; CHECK(!readState(s, o)); }                                                       // empty stream
}

void test_model_path_is_capped_when_written() {
  PluginState in; fillDefaultParams(in.params);
  in.modelPath.assign(kMaxModelPathBytes + 500, 'a');
  MemStream s; CHECK(writeState(s, in));
  PluginState out; CHECK(readState(s, out));
  CHECK(static_cast<int>(out.modelPath.size()) == kMaxModelPathBytes);
}
} // namespace

int main() {
  std::printf("The Stripper state tests\n");
  test_new_params_appended_at_end();
  test_param_table_matches_golden();
  test_hpf_default_is_20_hz();
  test_state_v8_roundtrip();
  test_state_v8_empty_path();
  test_state_v11_roundtrip();
  test_state_v11_defaults_and_old_states();
  test_state_v11_rejects_corruption_and_clamps();
  test_state_v10_roundtrip_with_seed_and_tape();
  test_state_v9_loads_without_seed_or_tape();
  test_state_v10_limits_and_corruption();
  test_state_v8_loads_with_nam_on();
  test_state_v9_keeps_the_switch();
  test_state_v7_loads_with_defaults();
  test_state_v6_skips_drive_output_and_mix();
  test_state_v5_gets_default_drive();
  test_state_v4_drops_old_eq_type();
  test_state_unknown_or_truncated_rejected();
  test_model_path_is_capped_when_written();
  std::printf(g_failed ? "FAILED (%d)\n" : "All state tests passed\n", g_failed);
  return g_failed ? 1 : 0;
}
