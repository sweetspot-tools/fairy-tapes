/**
 * Fairy Tapes · Deep Throat — messages between controller and processor (instance decor).
 *
 *   "Decor"  both ways  attribute "decor": the decor blob of state.h (serializeDecor: seed, screw angles, tape texts)
 *
 * The processor owns the saved copy (getState/setState); the controller keeps the copy the editor reads and edits.
 * Processor -> controller after setState and on connect; controller -> processor after an editor edit or when the
 * controller had to generate a seed. Never sent or handled on the audio thread.
 * Header-only templates over the message type: Steinberg::Vst::IMessage in the plugin, a fake in the tests.
 * (The message must offer setMessageID/getMessageID and getAttributes() with setBinary/getBinary.)
 */

#pragma once

#include "state.h"
#include <cstdint>
#include <cstring>
#include <vector>

namespace FairyTapes::DeepThroat {

constexpr const char* kMsgDecor = "Decor";

template <class Msg>
void encodeDecorMessage(Msg* m, const Decor& d) {
  std::vector<uint8_t> blob;
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
  return deserializeDecor(static_cast<const uint8_t*>(data), size, out);
}

} // namespace FairyTapes::DeepThroat
