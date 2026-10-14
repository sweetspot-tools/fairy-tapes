/**
 * Fairy Tapes · The Stripper — frozen parameter table: X(enumerator, id, default).
 * Generated from the code on 2026-10-10 and checked by test_state.cpp (ids at compile time, defaults at run time):
 * renumbering, renaming or changing the default of a parameter breaks the build or the test. Parameters added
 * later are appended at the end ONLY, and this table is extended in the same commit (existing rows never change).
 */

#pragma once

#define GOLDEN_PARAMS(X) \
  X(kBypass, 0, 0.0) \
  X(kStripMode, 1, 0.0) \
  X(kInputGain, 2, 0.5) \
  X(kOutputGain, 3, 0.5) \
  X(kBypassGainStaging, 4, 0.0) \
  X(kBypassLPHP, 5, 0.0) \
  X(kBypassEQ, 6, 0.0) \
  X(kBypassCompression, 7, 0.0) \
  X(kBypassDrive, 8, 0.0) \
  X(kBypassLimiter, 9, 0.0) \
  X(kBypassGate, 10, 0.0) \
  X(kGainStagingTrim, 11, 0.5) \
  X(kCompressAmount, 12, 0.0) \
  X(kCompressMix, 13, 1.0) \
  X(kCompressorMode, 14, 0.0) \
  X(kEqHPFFreq, 15, 0.0) \
  X(kEqHPFOn, 16, 1.0) \
  X(kEqLPFFreq, 17, 1.0) \
  X(kEqLPFOn, 18, 1.0) \
  X(kEqLFGain, 19, 0.5) \
  X(kEqLFFreq, 20, 0.15555556118488312) \
  X(kEqLFBell, 21, 0.0) \
  X(kEqLMGain, 22, 0.5) \
  X(kEqLMFreq, 23, 0.17391304671764374) \
  X(kEqLMQ, 24, 0.083333328366279602) \
  X(kEqHMGain, 25, 0.5) \
  X(kEqHMFreq, 26, 0.21875) \
  X(kEqHMQ, 27, 0.083333328366279602) \
  X(kEqHFGain, 28, 0.5) \
  X(kEqHFFreq, 29, 0.4482758641242981) \
  X(kEqHFBell, 30, 0.0) \
  X(kEqSaturation, 31, 0.0) \
  X(kDriveAmount, 32, 0.0) \
  X(kLimiterPound, 33, 0.5) \
  X(kGateThreshold, 34, 0.0) \
  X(kDriveNamOn, 35, 1.0)
