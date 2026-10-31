/**
 * Fairy Tapes · Happy Ending — Class IDs (FUIDs) for VST3.
 * Keep these unique; do not reuse in other plugins.
 */

#pragma once

#include "pluginterfaces/base/funknown.h"

namespace FairyTapes::HappyEnding {

// Processor (audio component) class ID
static const Steinberg::FUID kProcessorUID(
  0xACFCCE6Au, 0x1F8013A1u, 0xB978CBD5u, 0x6F069591u);

// Controller (EditController) class ID — must be different from Processor
static const Steinberg::FUID kControllerUID(
  0xC26E4C35u, 0x7B4CE172u, 0xD3936083u, 0x1DDA711Bu);

} // namespace FairyTapes::HappyEnding
