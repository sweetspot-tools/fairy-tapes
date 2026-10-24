/**
 * Fairy Tapes · Hotline — reads the state blob from a host stream (processor setState, controller setComponentState).
 */

#pragma once

#include "state.h"
#include "pluginterfaces/base/ibstream.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace FairyTapes::Hotline {

// Reads up to kStateSize bytes (the layout is fixed; extra bytes are not needed).
inline bool readStateBytes(Steinberg::IBStream* stream, std::vector<uint8_t>& out) {
  out.clear();
  if (!stream) return false;
  uint8_t chunk[256];
  while (out.size() < kStateSize) {
    const auto want = static_cast<Steinberg::int32>(std::min<size_t>(sizeof chunk, kStateSize - out.size()));
    Steinberg::int32 got = 0;
    if (stream->read(chunk, want, &got) != Steinberg::kResultOk) break;
    if (got <= 0) break;
    out.insert(out.end(), chunk, chunk + got);
  }
  return !out.empty();
}

} // namespace FairyTapes::Hotline
