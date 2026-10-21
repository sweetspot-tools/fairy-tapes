/**
 * Fairy Tapes · The Stripper — the TONE3000 client of one plug-in instance: the HTTP client, the login flow and the worker
 * thread behind the picker's TONE3000 tab. Created on the first use of the tab, so a plug-in whose user never opens it starts
 * no thread and makes no request. Without the network library (built without OpenSSL) or without an app id there is no
 * service and the tab says so.
 */

#pragma once

#include "tone3000_actions.h"

#include <memory>

namespace FairyTapes::TheStripper {

class Tone3000Service {
public:
  static std::unique_ptr<Tone3000Service> create();   // nullptr when TONE3000 is not available in this build
  ~Tone3000Service();
  Tone3000Actions actions();

private:
  Tone3000Service();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace FairyTapes::TheStripper
