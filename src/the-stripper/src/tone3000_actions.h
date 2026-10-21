/**
 * Fairy Tapes · The Stripper — what the TONE3000 tab of the model picker can ask for and what it can read. Plain std
 * functions over the common client's state type, so the interface does not link the network library and the render tool and
 * the tests can feed it made-up states. Controller::tone3000Actions() fills it in for the real client.
 */

#pragma once

#include "tone3000/client.h"

#include <functional>
#include <string>

namespace FairyTapes::TheStripper {

struct Tone3000Actions {
  bool configured = false;                                   // false: the plug-in was built without a TONE3000 app id
  std::function<Tone3000::ClientState()> snapshot;
  std::function<void()> signIn, signOut, chooseTone, cancel;
  std::function<void(Tone3000::ListKind, int)> showList;
  std::function<void(const std::string&)> showTone, download;
  std::function<void(const std::string&)> openUrl;           // opens a tone3000.com address in the browser
  std::string tonesFolder;                                   // where downloads go: the LOCAL tab scans it too
};

} // namespace FairyTapes::TheStripper
