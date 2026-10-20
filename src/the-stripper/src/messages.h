/**
 * Fairy Tapes · The Stripper — messages between controller and processor (model loading).
 *
 *   "LoadModel"   controller -> processor   attribute "path": UTF-8 path of the .nam file (empty = unload)
 *   "ModelStatus" processor  -> controller  attributes "state" (one digit, NamDrive::LoadState), "name", "error" (UTF-8), "gpu" ("1" = inference on the GPU)
 *
 * Strings travel as binary UTF-8 (setBinary/getBinary), so non-ASCII paths need no UTF-16 conversion.
 * Header-only templates over the message type: Steinberg::Vst::IMessage in the plugin, a fake in the tests.
 * (The message must offer setMessageID/getMessageID and getAttributes() with setBinary/getBinary.)
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <string>

namespace FairyTapes::TheStripper {

constexpr const char* kMsgLoadModel = "LoadModel";
constexpr const char* kMsgModelStatus = "ModelStatus";
constexpr unsigned kMaxMessageString = 4096;   // paths and error texts longer than this are rejected

struct ModelStatus {
  int state = 0;          // NamDrive::LoadState as an int (0 Idle, 1 Loading, 2 Loaded, 3 Failed)
  std::string name;       // file name of the loaded model, empty when none
  std::string error;      // reason of the last failure, empty otherwise
  bool gpu = false;       // the loaded model runs on the GPU
};

namespace messages_detail {
template <class Msg>
bool getString(Msg* m, const char* id, std::string& out) {
  auto* a = m->getAttributes();
  if (!a)
    return false;
  const void* data = nullptr;
  unsigned size = 0;
  if (a->getBinary(id, data, size) != 0)   // kResultOk == 0
    return false;
  if (size > kMaxMessageString)
    return false;
  out.assign(static_cast<const char*>(data), size);
  return true;
}
template <class Msg>
void setString(Msg* m, const char* id, const std::string& s) {
  const unsigned n = static_cast<unsigned>(s.size());
  m->getAttributes()->setBinary(id, s.data(), n);
}
template <class Msg>
bool hasId(Msg* m, const char* id) {
  const char* got = m ? m->getMessageID() : nullptr;
  return got && std::strcmp(got, id) == 0;
}
} // namespace messages_detail

template <class Msg>
void encodeLoadMessage(Msg* m, const std::string& path) {
  m->setMessageID(kMsgLoadModel);
  messages_detail::setString(m, "path", path);
}

template <class Msg>
bool decodeLoadMessage(Msg* m, std::string& path) {
  if (!messages_detail::hasId(m, kMsgLoadModel))
    return false;
  return messages_detail::getString(m, "path", path);
}

template <class Msg>
void encodeStatusMessage(Msg* m, const ModelStatus& st) {
  m->setMessageID(kMsgModelStatus);
  messages_detail::setString(m, "state", std::to_string(st.state));
  messages_detail::setString(m, "name", st.name);
  messages_detail::setString(m, "error", st.error);
  messages_detail::setString(m, "gpu", st.gpu ? "1" : "0");
}

template <class Msg>
bool decodeStatusMessage(Msg* m, ModelStatus& st) {
  if (!messages_detail::hasId(m, kMsgModelStatus))
    return false;
  std::string state;
  if (!messages_detail::getString(m, "state", state) || state.size() != 1 || state[0] < '0' || state[0] > '3')
    return false;
  st.state = state[0] - '0';
  std::string gpu;
  st.gpu = messages_detail::getString(m, "gpu", gpu) && gpu == "1";   // absent = CPU
  return messages_detail::getString(m, "name", st.name) && messages_detail::getString(m, "error", st.error);
}

} // namespace FairyTapes::TheStripper
