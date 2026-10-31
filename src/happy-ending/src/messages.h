/**
 * Fairy Tapes · Happy Ending — Messages between controller and processor (never on the audio thread).
 *
 * "Oversampling" (controller -> processor): Int "Index" = the new OVERSAMPLING position (0 Off, 1 2x, 2 4x).
 * Sent when the user changes it, just before the controller asks the host to restart the component
 * (kLatencyChanged): the processor learns the setting even if the host deactivates it before the next
 * process() call has delivered the parameter change, and rebuilds its zones in setActive(true).
 *
 * "Decor" (both ways): Binary "decor" = the decor blob of state.h (serializeDecor: seed, screw angles, tape texts).
 * The processor owns the saved copy (getState/setState); the controller keeps the copy the editor reads and edits.
 * Processor -> controller after setState and on connect; controller -> processor after an editor edit or when the
 * controller had to generate a seed. The decor templates work on any message type offering setMessageID/getMessageID
 * and getAttributes() with setBinary/getBinary: Steinberg::Vst::IMessage in the plug-in, a fake in the tests.
 */

#pragma once

#include "params.h"
#include "state.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include <cstdint>
#include <cstring>
#include <vector>

namespace FairyTapes::HappyEnding {

inline constexpr const char* kMsgOversampling = "Oversampling";
inline constexpr const char* kMsgOversamplingIndex = "Index";

inline void encodeOversamplingMessage(Steinberg::Vst::IMessage* msg, int index) {
  msg->setMessageID(kMsgOversampling);
  msg->getAttributes()->setInt(kMsgOversamplingIndex, index);
}

// True and the clamped index when `msg` is an "Oversampling" message.
inline bool decodeOversamplingMessage(Steinberg::Vst::IMessage* msg, int& index) {
  if (!msg || !msg->getMessageID() || std::strcmp(msg->getMessageID(), kMsgOversampling) != 0) return false;
  Steinberg::int64 v = 0;
  if (!msg->getAttributes() || msg->getAttributes()->getInt(kMsgOversamplingIndex, v) != Steinberg::kResultOk) return false;
  const int last = kParams[kOversampling].steps - 1;
  index = v < 0 ? 0 : (v > last ? last : static_cast<int>(v));
  return true;
}

inline constexpr const char* kMsgDecor = "Decor";

template <class Msg>
void encodeDecorMessage(Msg* m, const Decor& d) {
  std::vector<std::uint8_t> blob;
  serializeDecor(d, blob);
  m->setMessageID(kMsgDecor);
  m->getAttributes()->setBinary("decor", blob.data(), static_cast<unsigned>(blob.size()));
}

// False (and `out` untouched) for another message, a missing attribute or a corrupt blob. The result is sanitized.
template <class Msg>
bool decodeDecorMessage(Msg* m, Decor& out) {
  const char* id = m ? m->getMessageID() : nullptr;
  if (!id || std::strcmp(id, kMsgDecor) != 0) return false;
  auto* a = m->getAttributes();
  if (!a) return false;
  const void* data = nullptr;
  unsigned size = 0;
  if (a->getBinary("decor", data, size) != 0 || !data || size > kMaxStateBytes) return false;   // 0 == kResultOk
  return deserializeDecor(static_cast<const std::uint8_t*>(data), size, out);
}

} // namespace FairyTapes::HappyEnding
