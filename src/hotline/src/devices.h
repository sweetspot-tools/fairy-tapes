/**
 * Fairy Tapes · Hotline — Device table, Morph interpolation and back-panel descriptions.
 * Pure C++ (no SDK headers).
 */

#pragma once

#include "params.h"
#include <string>

namespace FairyTapes::Hotline {

// Settings of every block of the signal chain for one device (or an interpolation towards it).
struct BlockParams {
  double hpHz;        // high-pass corner
  double lpHz;        // low-pass corner
  double drive;       // 0..1 amount
  double coneHz;      // cone peak frequency
  double coneGainDb;
  double coneQ;
  double hornGainDb;
  double crushRateHz; // sample-rate reduction target
  double crushBits;   // bit depth
  double compRatio;   // compander ratio (1 = off)
  double squelchDb;   // squelch threshold (-120 = off)
  double noiseDb;     // noise floor level (-120 = off)
  double room;        // 0..1 amount
};

struct DeviceSpec {
  const char* button; // short label on the device button
  const char* name;   // name shown on the back panel
  BlockParams p;
};

// The single source of the device values.
extern const DeviceSpec kDevices[kNumDevices];

// Every block bypassed: full band, no colouring.
BlockParams neutralParams();

// neutral -> device. amount is clamped to 0..1 (NaN -> 0). Frequencies and rates interpolate
// logarithmically, levels (cone/horn gain, squelch, noise) linearly in dB, the rest linearly.
BlockParams morphParams(const BlockParams& device, double amount);

// One "NAME value unit" line per non-neutral block, separated by '\n'. Empty for an invalid index.
std::string describeDevice(int index);

} // namespace FairyTapes::Hotline
