/**
 * Fairy Tapes · Happy Ending — Unit tests for the linear-phase oversampler.
 * Run via CTest; build target: happy_ending_oversampler_tests.
 */

#include "dsp/oversampler.h"
#include "tuning.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <random>
#include <vector>

// Counts heap allocations while g_countAllocs is set (no-allocation-in-process checks).
static std::atomic<bool> g_countAllocs{false};
static std::atomic<int> g_allocs{0};
void* operator new(std::size_t n) {
  if (g_countAllocs) ++g_allocs;
  if (void* p = std::malloc(n ? n : 1)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

using namespace FairyTapes::HappyEnding;
using FairyTapes::HappyEnding::DSP::Oversampler;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kFs = 44100.0;
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

double dbOf(double lin) { return 20.0 * std::log10(std::max(lin, 1e-12)); }

// Whole signal through the oversampler in blocks of `block` samples; returns the base-rate output.
std::vector<float> roundTrip(Oversampler& os, const std::vector<float>& x, int block,
                             std::vector<float>* highRate = nullptr) {
  std::vector<float> y(x.size());
  std::vector<float> hi(static_cast<std::size_t>(block) * 4);
  std::size_t pos = 0;
  while (pos < x.size()) {
    const int n = static_cast<int>(std::min<std::size_t>(block, x.size() - pos));
    const int nUp = os.up(x.data() + pos, n, hi.data());
    if (highRate) highRate->insert(highRate->end(), hi.begin(), hi.begin() + nUp);
    os.down(hi.data(), nUp, y.data() + pos);
    pos += static_cast<std::size_t>(n);
  }
  return y;
}

// Band-limited noise: many sines with random frequencies and phases below `maxHz` (exactly limited).
std::vector<float> bandLimitedNoise(int n, double maxHz, unsigned seed = 7) {
  std::mt19937 gen(seed);
  std::uniform_real_distribution<double> fr(50.0, maxHz), ph(0.0, 2.0 * kPi);
  std::vector<double> f, p;
  for (int i = 0; i < 300; ++i) { f.push_back(fr(gen)); p.push_back(ph(gen)); }
  std::vector<float> x(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) {
    double s = 0.0;
    for (std::size_t j = 0; j < f.size(); ++j) s += std::sin(2.0 * kPi * f[j] * i / kFs + p[j]);
    x[static_cast<std::size_t>(i)] = static_cast<float>(0.5 * s / std::sqrt(static_cast<double>(f.size()) / 2.0));
  }
  return x;
}

// Amplitude of the component at `bin` cycles per `len` samples, measured over `len` samples.
double amplitudeAtBin(const float* x, int len, double bin) {
  double re = 0.0, im = 0.0;
  for (int i = 0; i < len; ++i) {
    re += x[i] * std::cos(2.0 * kPi * bin * i / len);
    im -= x[i] * std::sin(2.0 * kPi * bin * i / len);
  }
  return 2.0 * std::sqrt(re * re + im * im) / len;
}

// Same, with a Hann window (for components that do not fall on a bin, with far leakage).
double amplitudeAtHz(const float* x, int len, double hz, double fs) {
  double re = 0.0, im = 0.0, wsum = 0.0;
  for (int i = 0; i < len; ++i) {
    const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * (i + 0.5) / len);
    re += w * x[i] * std::cos(2.0 * kPi * hz * i / fs);
    im -= w * x[i] * std::sin(2.0 * kPi * hz * i / fs);
    wsum += w;
  }
  return 2.0 * std::sqrt(re * re + im * im) / wsum;
}

void testFactorOneIsPassThrough() {
  Oversampler os;
  os.prepare(1, 512);
  ASSERT_TRUE(os.latencySamples() == 0);
  const auto x = bandLimitedNoise(2000, 20000.0);
  std::vector<float> hi(x.size());
  ASSERT_TRUE(os.up(x.data(), static_cast<int>(x.size()), hi.data()) == static_cast<int>(x.size()));
  for (std::size_t i = 0; i < x.size(); ++i) ASSERT_TRUE(hi[i] == x[i]);
  std::vector<float> y(x.size());
  os.down(hi.data(), static_cast<int>(hi.size()), y.data());
  for (std::size_t i = 0; i < x.size(); ++i) ASSERT_TRUE(y[i] == x[i]);
}

void testLatencyIsInteger() {
  for (int factor : {2, 4}) {
    Oversampler os;
    os.prepare(factor, 256);
    const int lat = os.latencySamples();
    ASSERT_TRUE(lat > 0);
    // The declared value is the real one: the impulse comes back exactly `lat` samples later.
    std::vector<float> x(2048, 0.0f);
    x[600] = 1.0f;
    const auto y = roundTrip(os, x, 256);
    int peak = 0;
    for (int i = 0; i < static_cast<int>(y.size()); ++i)
      if (std::fabs(y[static_cast<std::size_t>(i)]) > std::fabs(y[static_cast<std::size_t>(peak)])) peak = i;
    ASSERT_TRUE(peak == 600 + lat);
    ASSERT_TRUE(y[static_cast<std::size_t>(peak)] > 0.8f); // the band-limited unit impulse
    // Linear phase: the impulse response is symmetric around the peak.
    for (int d = 1; d <= lat; ++d)
      ASSERT_TRUE(std::fabs(y[static_cast<std::size_t>(peak - d)] - y[static_cast<std::size_t>(peak + d)]) < 1e-6f);
  }
}

void testRoundTripIsPureDelay() {
  for (int factor : {2, 4}) {
    Oversampler os;
    os.prepare(factor, 512);
    const int lat = os.latencySamples();
    const int n = 16384;
    const auto x = bandLimitedNoise(n, 18000.0);
    const auto y = roundTrip(os, x, 512);
    double err = 0.0, sig = 0.0;
    const int skip = 2 * lat + 64;
    for (int i = skip; i < n; ++i) {
      const double d = static_cast<double>(y[static_cast<std::size_t>(i)]) - x[static_cast<std::size_t>(i - lat)];
      err += d * d;
      sig += static_cast<double>(x[static_cast<std::size_t>(i - lat)]) * x[static_cast<std::size_t>(i - lat)];
    }
    const double errDb = dbOf(std::sqrt(err / sig));
    std::printf("  round trip x%d: latency %d, error %.1f dB\n", factor, lat, errDb);
    ASSERT_TRUE(errDb < -90.0);
  }
}

void testPassbandFlat() {
  const int len = 8192;
  // Up to 0.4 fs (the brief) and on to the designed passband edge (0.4535 fs = 20 kHz at 44.1 kHz).
  const int bins[] = {8, 100, 500, 1000, 1500, 2000, 2500, 3000, 3276, 3400, 3600, 3700, 3715};
  for (int factor : {2, 4}) {
    for (int bin : bins) {
      ASSERT_TRUE(static_cast<double>(bin) / len <= Tuning::kOversamplerPassbandEdge);
      Oversampler os;
      os.prepare(factor, 1024);
      std::vector<float> x(static_cast<std::size_t>(2 * len));
      for (int i = 0; i < 2 * len; ++i)
        x[static_cast<std::size_t>(i)] = static_cast<float>(0.5 * std::sin(2.0 * kPi * bin * i / len));
      std::vector<float> hi;
      const auto y = roundTrip(os, x, 1024, &hi);
      // Round trip, steady state (the second period).
      const double gRound = amplitudeAtBin(y.data() + len, len, bin) / 0.5;
      // Up path alone: the same period at the high rate.
      const double gUp = amplitudeAtBin(hi.data() + static_cast<std::size_t>(len) * factor,
                                        len * factor, static_cast<double>(bin)) / 0.5;
      ASSERT_TRUE(std::fabs(dbOf(gRound)) < 0.1);
      ASSERT_TRUE(std::fabs(dbOf(gUp)) < 0.1);
    }
  }
}

void testImageRejection() {
  const double f0 = 15000.0;
  const int n = 16384;
  for (int factor : {2, 4}) {
    Oversampler os;
    os.prepare(factor, 1024);
    std::vector<float> x(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
      x[static_cast<std::size_t>(i)] = static_cast<float>(0.5 * std::sin(2.0 * kPi * f0 * i / kFs));
    std::vector<float> hi;
    roundTrip(os, x, 1024, &hi);
    const double fsHi = kFs * factor;
    const int seg = 8192 * factor, start = static_cast<int>(hi.size()) - seg;
    const double fund = amplitudeAtHz(hi.data() + start, seg, f0, fsHi);
    ASSERT_TRUE(std::fabs(fund - 0.5) < 0.01);
    // Images of the 15 kHz tone around every multiple of the base rate below the new Nyquist.
    for (int m = 1; m * kFs - f0 < fsHi / 2.0; ++m) {
      for (double img : {m * kFs - f0, m * kFs + f0}) {
        if (img >= fsHi / 2.0) continue;
        const double a = amplitudeAtHz(hi.data() + start, seg, img, fsHi);
        std::printf("  x%d image at %.1f Hz: %.1f dB\n", factor, img, dbOf(a / fund));
        ASSERT_TRUE(dbOf(a / fund) < -90.0);
      }
    }
  }
}

void testBlockSizeDoesNotMatter() {
  const auto x = bandLimitedNoise(5000, 18000.0);
  for (int factor : {2, 4}) {
    Oversampler a, b, c;
    a.prepare(factor, 512);
    b.prepare(factor, 512);
    c.prepare(factor, 64); // blocks longer than maxBlock are chunked internally
    const auto ya = roundTrip(a, x, 512);
    const auto yb = roundTrip(b, x, 37);
    const auto yc = roundTrip(c, x, 512);
    for (std::size_t i = 0; i < x.size(); ++i) {
      ASSERT_TRUE(ya[i] == yb[i]);
      ASSERT_TRUE(ya[i] == yc[i]);
    }
  }
}

void testResetAndReprepareClearState() {
  const auto x = bandLimitedNoise(3000, 18000.0);
  Oversampler os;
  os.prepare(4, 256);
  const auto first = roundTrip(os, x, 256);
  os.reset();
  const auto again = roundTrip(os, x, 256);
  for (std::size_t i = 0; i < x.size(); ++i) ASSERT_TRUE(first[i] == again[i]);
  // Changing the factor re-designs everything and starts from silence.
  os.prepare(2, 256);
  const auto y2 = roundTrip(os, x, 256);
  Oversampler fresh;
  fresh.prepare(2, 256);
  const auto yf = roundTrip(fresh, x, 256);
  for (std::size_t i = 0; i < x.size(); ++i) ASSERT_TRUE(y2[i] == yf[i]);
  ASSERT_TRUE(os.latencySamples() == fresh.latencySamples());
}

void testSilenceStaysSilent() {
  for (int factor : {1, 2, 4}) {
    Oversampler os;
    os.prepare(factor, 256);
    std::vector<float> x(1000, 0.0f);
    const auto y = roundTrip(os, x, 256);
    for (float v : y) ASSERT_TRUE(v == 0.0f);
  }
}

void testNoAllocationInProcess() {
  Oversampler os;
  os.prepare(4, 512);
  const auto x = bandLimitedNoise(512, 18000.0);
  std::vector<float> hi(512 * 4), y(512);
  g_allocs = 0;
  g_countAllocs = true;
  for (int i = 0; i < 20; ++i) {
    const int nUp = os.up(x.data(), 512, hi.data());
    os.down(hi.data(), nUp, y.data());
  }
  os.reset();
  g_countAllocs = false;
  ASSERT_TRUE(g_allocs == 0);
}

} // namespace

int main() {
  RUN_TEST(testFactorOneIsPassThrough);
  RUN_TEST(testLatencyIsInteger);
  RUN_TEST(testRoundTripIsPureDelay);
  RUN_TEST(testPassbandFlat);
  RUN_TEST(testImageRejection);
  RUN_TEST(testBlockSizeDoesNotMatter);
  RUN_TEST(testResetAndReprepareClearState);
  RUN_TEST(testSilenceStaysSilent);
  RUN_TEST(testNoAllocationInProcess);
  std::printf("%d passed, %d failed\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
