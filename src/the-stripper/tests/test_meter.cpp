/**
 * Fairy Tapes · The Stripper — metering tests (peak meter, clip detector, gain reduction, update rate, ids).
 * Linked into the_stripper_tests; run_meter_tests() returns the number of failed checks.
 */

#include "dsp/meter.h"
#include "dsp/compression_stub.h"
#include "state.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace FairyTapes::TheStripper;
using namespace FairyTapes::TheStripper::DSP;

namespace {
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)
#define CHECK_NEAR(a, b, eps) do { if (std::fabs((double)(a) - (double)(b)) > (eps)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s ~ %s (got %.4f vs %.4f)\n", __FILE__, __LINE__, #a, #b, (double)(a), (double)(b)); ++g_failed; } } while (0)

constexpr double kSr = 48000.0;
constexpr int kBlock = 64;

/// Feed `seconds` of constant-value blocks (silence by default).
void feedSilence(PeakMeter& m, double seconds) {
  std::vector<float> z(kBlock, 0.0f);
  for (int i = 0; i < static_cast<int>(seconds * kSr / kBlock); ++i) m.process(z.data(), kBlock);
}

void test_peakmeter_instant_attack_and_release() {
  PeakMeter m; m.prepare(kSr);
  std::vector<float> b(kBlock, 0.0f); b[10] = 0.5f;
  m.process(b.data(), kBlock);
  CHECK_NEAR(m.levelDb(), -6.02, 0.05);                    // instant attack
  feedSilence(m, 1.0);
  CHECK_NEAR(m.levelDb(), -6.02 - 20.0, 1.0);              // release 20 dB/s
}

void test_peakmeter_hold_1_5s() {
  PeakMeter m; m.prepare(kSr);
  std::vector<float> b(kBlock, 0.0f); b[0] = 0.5f;
  m.process(b.data(), kBlock);
  feedSilence(m, 1.4);
  CHECK_NEAR(m.holdDb(), -6.02, 0.05);                     // still held after 1.4 s
  feedSilence(m, 0.2);                                     // 1.6 s in total
  CHECK(m.holdDb() < -8.0);                                // has started to fall
}

void test_peakmeter_reset() {
  PeakMeter m; m.prepare(kSr);
  std::vector<float> b(kBlock, 0.9f);
  m.process(b.data(), kBlock);
  m.reset();
  CHECK(m.levelDb() <= -100.0f);
  CHECK(m.holdDb() <= -100.0f);
}

void test_clip_detects_at_exactly_one() {
  ClipDetector c; c.prepare(kSr);
  std::vector<float> b(kBlock, 0.0f); b[3] = 0.9999f;
  c.process(b.data(), kBlock); CHECK(!c.lit());
  b[3] = 1.0f;
  c.process(b.data(), kBlock); CHECK(c.lit());
  ClipDetector n; n.prepare(kSr);
  b[3] = -1.2f;
  n.process(b.data(), kBlock); CHECK(n.lit());             // absolute value
}

void test_clip_hold_2s_and_clear() {
  ClipDetector c; c.prepare(kSr);
  std::vector<float> hit(kBlock, 0.0f); hit[0] = 1.0f;
  std::vector<float> zero(kBlock, 0.0f);
  c.process(hit.data(), kBlock);
  for (int i = 0; i < static_cast<int>(1.9 * kSr / kBlock); ++i) c.process(zero.data(), kBlock);
  CHECK(c.lit());                                          // still lit at 1.9 s
  for (int i = 0; i < static_cast<int>(0.2 * kSr / kBlock); ++i) c.process(zero.data(), kBlock);
  CHECK(!c.lit());                                         // off after 2.1 s
  c.process(hit.data(), kBlock); CHECK(c.lit());
  c.clear(); CHECK(!c.lit());
}

void test_ssl_gain_reduction_encoding() {
  // SSL 360 Link: 0 = no reduction, 1 = full scale reduced to silence, dB = 20 * log10(1 - value)
  CHECK_NEAR(grValueFromDb(0.0), 0.0, 1e-12);
  CHECK_NEAR(grValueFromDb(-5.0), 0.0, 1e-12);          // negative reduction is none
  CHECK_NEAR(grValueFromDb(6.0206), 0.5, 1e-4);         // half the amplitude
  CHECK_NEAR(grValueFromDb(20.0), 0.9, 1e-12);
  CHECK(grValueFromDb(300.0) < 1.0);                    // never exactly 1: the dB conversion stays finite
  CHECK(grValueFromDb(std::nan("")) == 0.0);
  for (double db : {0.5, 3.0, 12.0, 20.0}) CHECK_NEAR(grDbFromValue(grValueFromDb(db)), db, 1e-9);
  CHECK_NEAR(grDbFromValue(0.0), 0.0, 1e-12);
  CHECK(std::isfinite(grDbFromValue(1.0)) && grDbFromValue(1.0) > 100.0);
  CHECK_NEAR(grDbFromValue(std::nan("")), 0.0, 1e-12);
}

void test_gain_reduction_math() {
  GainReductionMeter g; g.prepare(kSr);
  CHECK_NEAR(g.update(0.5f, 0.5f, kBlock), 0.0, 1e-6);     // nothing removed
  CHECK_NEAR(g.update(0.5f, 0.25f, kBlock), 6.02, 0.05);   // half the peak = 6 dB
  CHECK_NEAR(g.update(0.5f, 1.0f, kBlock), 6.02 - 60.0 * kBlock / kSr, 0.1);   // gain > 0 dB: never negative, released slowly
  for (int i = 0; i < 400; ++i) g.update(0.5f, 1.0f, kBlock);
  CHECK_NEAR(g.value(), 0.0, 1e-6);
  CHECK_NEAR(g.update(0.0f, 0.0f, kBlock), 0.0, 1e-6);     // silence
}

/// Mean gain reduction (dB) an engine reports on white-ish noise at `levelDb`.
double compressorGr(int mode, double levelDb) {
  CompressionStub c; c.setSampleRate(kSr); c.prepare(256); c.setMode(mode); c.setParams(0.8f, 1.0f);
  GainReductionMeter gr; gr.prepare(kSr);
  const float amp = static_cast<float>(std::pow(10.0, levelDb / 20.0));
  unsigned seed = 12345u; double acc = 0; int n = 0;
  std::vector<float> L(256), R(256), iL(256), iR(256);
  for (int blk = 0; blk < 400; ++blk) {
    for (int i = 0; i < 256; ++i) { seed = seed * 1664525u + 1013904223u; float v = ((seed >> 8) & 0xFFFF) / 32768.0f - 1.0f; iL[i] = L[i] = amp * v; iR[i] = R[i] = amp * v; }
    c.process(L.data(), R.data(), 256);
    const double v = gr.updateDb(c.currentGainReductionDb(), 256);
    if (blk >= 200) { acc += v; ++n; }
  }
  return acc / n;
}

void test_gr_zero_when_not_compressing_and_positive_when_compressing() {
  for (int mode = 0; mode < 4; ++mode) {
    const double quiet = compressorGr(mode, -50.0);
    const double loud = compressorGr(mode, -6.0);
    std::fprintf(stdout, "    [info] mode %d: GR at -50 dBFS = %.2f dB, at -6 dBFS = %.2f dB\n", mode, quiet, loud);
    CHECK(quiet < 0.5);
    CHECK(loud > 1.0);
  }
}

void test_meter_ids_do_not_collide_with_params() {
  CHECK(kMeterBase >= kNumParams);
  CHECK(kNumMeterIDs - kMeterBase == 13);
  CHECK(kClearClips == kMeterBase + 12);
}

void test_meters_not_in_state() {
  struct Count { std::size_t n = 0; bool writeInt32(std::int32_t) { n += 4; return true; } bool writeDouble(double) { n += 8; return true; } long long writeRaw(const void*, std::int32_t k) { n += static_cast<std::size_t>(k); return k; } } c;
  PluginState st; fillDefaultParams(st.params);
  CHECK(writeState(c, st));
  CHECK(c.n == 4 + 8 * static_cast<std::size_t>(kNumParams) + 4);   // version + parameters + empty path length: no meter values
}

void test_meter_update_rate_limit() {
  UpdateLimiter u; u.prepare(kSr, 16.0);
  int ticks = 0;
  for (int i = 0; i < static_cast<int>(kSr / kBlock); ++i) if (u.advance(kBlock)) ++ticks;   // one second
  CHECK(ticks <= 63);     // at most one write per 16 ms
  CHECK(ticks >= 55);     // and it does keep updating (about 60 Hz)
}

void test_meter_norm_from_db() {
  CHECK_NEAR(meterNormFromDb(-60.0, -60.0, 6.0), 0.0, 1e-9);
  CHECK_NEAR(meterNormFromDb(6.0, -60.0, 6.0), 1.0, 1e-9);
  CHECK_NEAR(meterNormFromDb(-27.0, -60.0, 6.0), 0.5, 1e-9);
  CHECK_NEAR(meterNormFromDb(-100.0, -60.0, 6.0), 0.0, 1e-9);   // clamped
  CHECK_NEAR(meterNormFromDb(20.0, -60.0, 6.0), 1.0, 1e-9);
}
} // namespace

int run_meter_tests() {
  std::printf("Meter tests\n");
  test_peakmeter_instant_attack_and_release();
  test_peakmeter_hold_1_5s();
  test_peakmeter_reset();
  test_clip_detects_at_exactly_one();
  test_clip_hold_2s_and_clear();
  test_ssl_gain_reduction_encoding();
  test_gain_reduction_math();
  test_gr_zero_when_not_compressing_and_positive_when_compressing();
  test_meter_ids_do_not_collide_with_params();
  test_meters_not_in_state();
  test_meter_update_rate_limit();
  test_meter_norm_from_db();
  return g_failed;
}
