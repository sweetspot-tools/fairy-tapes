/**
 * Fairy Tapes · The Stripper — the TONE3000 app. `kTone3000ClientId` is the publishable key of the app registered on
 * tone3000.com (it is meant to be in client code: no secret goes into the plug-in). Left empty, the TONE3000 tab says the
 * plug-in is not set up and nothing is ever sent. Registering the app and its redirect address is the user's task
 * (docs/superpowers/specs/2026-10-21-tone3000-integration-design.md, sections 7 and 8).
 */

#pragma once

namespace FairyTapes::TheStripper {

constexpr const char* kTone3000ClientId = "";

} // namespace FairyTapes::TheStripper
