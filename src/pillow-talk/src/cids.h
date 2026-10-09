/**
 * Fairy Tapes · Pillow Talk — Class IDs (FUIDs) for VST3.
 * Keep these unique; do not reuse in other plugins.
 */

#pragma once

#include "pluginterfaces/base/funknown.h"

namespace FairyTapes::PillowTalk {

// Processor (audio component) class ID
static const Steinberg::FUID kProcessorUID(
  0x32B78297u, 0x37D19353u, 0x4E71FA0Fu, 0xC0F3ECC6u);

// Controller (EditController) class ID — must be different from Processor
static const Steinberg::FUID kControllerUID(
  0x13056DC3u, 0x9AA369D5u, 0x0087B11Cu, 0x0B56519Au);

} // namespace FairyTapes::PillowTalk
