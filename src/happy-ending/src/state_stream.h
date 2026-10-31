/**
 * Fairy Tapes · Happy Ending — reads a state blob from a host stream (processor setState,
 * controller setComponentState): until the end of the stream, at most kMaxStateBytes.
 */

#pragma once

#include "state.h"
#include "pluginterfaces/base/ibstream.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace FairyTapes::HappyEnding {

inline bool readStateBytes(Steinberg::IBStream* stream, std::vector<std::uint8_t>& out) {
  out.clear();
  if (!stream) return false;
  std::uint8_t chunk[512];
  while (out.size() < kMaxStateBytes) {
    const auto want = static_cast<Steinberg::int32>(std::min<std::size_t>(sizeof chunk, kMaxStateBytes - out.size()));
    Steinberg::int32 got = 0;
    if (stream->read(chunk, want, &got) != Steinberg::kResultOk) return !out.empty();
    if (got <= 0) break;
    out.insert(out.end(), chunk, chunk + got);
    if (got < want) break;
  }
  return !out.empty();
}

} // namespace FairyTapes::HappyEnding
