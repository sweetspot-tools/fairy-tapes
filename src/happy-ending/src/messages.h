/**
 * Fairy Tapes · Happy Ending — Messages between controller and processor (never on the audio thread).
 *
 * "Oversampling" (controller -> processor): Int "Index" = the new OVERSAMPLING position (0 Off, 1 2x, 2 4x).
 * Sent when the user changes it, just before the controller asks the host to restart the component
 * (kLatencyChanged): the processor learns the setting even if the host deactivates it before the next
 * process() call has delivered the parameter change, and rebuilds its zones in setActive(true).
 */

#pragma once

#include "params.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include <cstring>

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

} // namespace FairyTapes::HappyEnding
