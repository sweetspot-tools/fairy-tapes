/**
 * Fairy Tapes · The Stripper — Tests for parameter helpers (params.h).
 * Run via CTest; build target: the_stripper_tests.
 */

#include "params.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

namespace {

constexpr double kEps = 1e-9;

bool approx(double a, double b) {
  return std::fabs(a - b) <= kEps;
}

int runTests() {
  int failed = 0;

  using namespace FairyTapes::TheStripper;

  // --- normalizedToDb ---
  if (!approx(normalizedToDb(0.), kGainMinDb)) {
    std::fprintf(stderr, "FAIL: normalizedToDb(0) expected %.1f, got %.1f\n",
                 kGainMinDb, normalizedToDb(0.));
    ++failed;
  }
  if (!approx(normalizedToDb(1.), kGainMaxDb)) {
    std::fprintf(stderr, "FAIL: normalizedToDb(1) expected %.1f, got %.1f\n",
                 kGainMaxDb, normalizedToDb(1.));
    ++failed;
  }
  if (!approx(normalizedToDb(0.5), 0.0)) {
    std::fprintf(stderr, "FAIL: normalizedToDb(0.5) expected 0.0, got %.1f\n",
                 normalizedToDb(0.5));
    ++failed;
  }
  if (normalizedToDb(-0.1) != kGainMinDb) {
    std::fprintf(stderr, "FAIL: normalizedToDb(<0) should clamp to kGainMinDb\n");
    ++failed;
  }

  // --- dbToNormalized ---
  if (!approx(dbToNormalized(kGainMinDb), 0.)) {
    std::fprintf(stderr, "FAIL: dbToNormalized(-20) expected 0, got %.3f\n",
                 dbToNormalized(kGainMinDb));
    ++failed;
  }
  if (!approx(dbToNormalized(kGainMaxDb), 1.)) {
    std::fprintf(stderr, "FAIL: dbToNormalized(20) expected 1, got %.3f\n",
                 dbToNormalized(kGainMaxDb));
    ++failed;
  }
  if (!approx(dbToNormalized(0.0), 0.5)) {
    std::fprintf(stderr, "FAIL: dbToNormalized(0) expected 0.5, got %.3f\n",
                 dbToNormalized(0.0));
    ++failed;
  }
  if (dbToNormalized(-30.) != 0.) {
    std::fprintf(stderr, "FAIL: dbToNormalized(< min) should clamp to 0\n");
    ++failed;
  }
  if (dbToNormalized(30.) != 1.) {
    std::fprintf(stderr, "FAIL: dbToNormalized(> max) should clamp to 1\n");
    ++failed;
  }

  // --- roundtrip normalizedToDb / dbToNormalized ---
  for (double n : {0.0, 0.25, 0.5, 0.75, 1.0}) {
    double db = normalizedToDb(n);
    double back = dbToNormalized(db);
    if (!approx(back, n)) {
      std::fprintf(stderr, "FAIL: roundtrip n=%.2f -> db=%.2f -> n=%.3f\n", n, db, back);
      ++failed;
    }
  }

  // --- normalizedToLinear ---
  if (!approx(normalizedToLinear(0.5), 1.0)) {
    std::fprintf(stderr, "FAIL: normalizedToLinear(0.5) expected 1.0 (0 dB), got %.3f\n",
                 normalizedToLinear(0.5));
    ++failed;
  }
  if (!approx(normalizedToLinear(0.), 0.1)) {
    std::fprintf(stderr, "FAIL: normalizedToLinear(0) expected 0.1 (-20 dB), got %.3f\n",
                 normalizedToLinear(0.));
    ++failed;
  }
  double plus20 = normalizedToLinear(1.);
  if (!approx(plus20, 10.0)) {
    std::fprintf(stderr, "FAIL: normalizedToLinear(1) expected 10.0 (+20 dB), got %.3f\n", plus20);
    ++failed;
  }

  // --- gainStagingTrimToLinear ---
  if (!approx(gainStagingTrimToLinear(0.0), 0.1)) {
    std::fprintf(stderr, "FAIL: gainStagingTrimToLinear(0) expected 0.1, got %.3f\n",
                 gainStagingTrimToLinear(0.0));
    ++failed;
  }
  if (!approx(gainStagingTrimToLinear(1.0), 2.0)) {
    std::fprintf(stderr, "FAIL: gainStagingTrimToLinear(1) expected 2.0, got %.3f\n",
                 gainStagingTrimToLinear(1.0));
    ++failed;
  }

  // --- isBypassStage / isChannelMode ---
  if (isBypassStage(0.0) || !isBypassStage(0.5) || !isBypassStage(1.0)) {
    std::fprintf(stderr, "FAIL: isBypassStage\n");
    ++failed;
  }
  if (!isChannelMode(0.0) || isChannelMode(0.5) || isChannelMode(1.0)) {
    std::fprintf(stderr, "FAIL: isChannelMode\n");
    ++failed;
  }

  return failed;
}

} // namespace

int main() {
  int failed = runTests();
  if (failed > 0) {
    std::fprintf(stderr, "%d test(s) failed.\n", failed);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
