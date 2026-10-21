/**
 * Fairy Tapes · Pillow Talk — reads a state blob from a host stream (processor setState, controller
 * setComponentState). The blob has a variable length since v2: read until the end, at most kMaxStateBytes.
 */

#pragma once

#include "state.h"
#include "pluginterfaces/base/ibstream.h"
#include <cstdint>
#include <vector>

namespace FairyTapes::PillowTalk {

inline bool readStateBytes(Steinberg::IBStream* stream, std::vector<uint8_t>& out) {
  out.clear();
  if (!stream) return false;
  uint8_t chunk[512];
  while (out.size() < kMaxStateBytes) {
    const auto want = (Steinberg::int32)std::min<size_t>(sizeof chunk, kMaxStateBytes - out.size());
    Steinberg::int32 got = 0;
    if (stream->read(chunk, want, &got) != Steinberg::kResultOk) return !out.empty();
    if (got <= 0) break;
    out.insert(out.end(), chunk, chunk + got);
    if (got < want) break;
  }
  return !out.empty();
}

} // namespace FairyTapes::PillowTalk
