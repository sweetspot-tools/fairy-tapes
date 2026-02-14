/**
 * Fairy Tapes · The Stripper — Class IDs (FUIDs) for VST3.
 * Keep these unique; do not reuse in other plugins.
 */

#pragma once

#include "pluginterfaces/base/funknown.h"

namespace FairyTapes::TheStripper {

// Processor (audio component) class ID
static const Steinberg::FUID kProcessorUID(
  0xA7B8C9D0u, 0x1E2F3A4Bu, 0x5C6D7E8Fu, 0x90A1B2C3u);

// Controller (EditController) class ID — must be different from Processor
static const Steinberg::FUID kControllerUID(
  0xB8C9D0E1u, 0x2F3A4B5Cu, 0x6D7E8F90u, 0xA1B2C3D4u);

} // namespace FairyTapes::TheStripper
