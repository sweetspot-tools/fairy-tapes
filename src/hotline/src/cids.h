/**
 * Fairy Tapes · Hotline — Class IDs (FUIDs) for VST3.
 * Keep these unique; do not reuse in other plugins.
 */

#pragma once

#include "pluginterfaces/base/funknown.h"

namespace FairyTapes::Hotline {

// Processor (audio component) class ID
static const Steinberg::FUID kProcessorUID(
  0xF30AF228u, 0xC434FAA5u, 0xE7C3E72Au, 0x8B3B7F87u);

// Controller (EditController) class ID — must be different from Processor
static const Steinberg::FUID kControllerUID(
  0x8B9BAB84u, 0xFB186500u, 0x14A694C9u, 0x3F07CE37u);

} // namespace FairyTapes::Hotline
