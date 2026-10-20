/**
 * Fairy Tapes · Deep Throat — Class IDs (FUIDs) for VST3.
 * Keep these unique; do not reuse in other plugins.
 */

#pragma once

#include "pluginterfaces/base/funknown.h"

namespace FairyTapes::DeepThroat {

// Processor (audio component) class ID
static const Steinberg::FUID kProcessorUID(
  0x3143A98Fu, 0x8064B401u, 0x54AEDC31u, 0x3FDA0FF5u);

// Controller (EditController) class ID — must be different from Processor
static const Steinberg::FUID kControllerUID(
  0x5BC85F6Cu, 0x99C221DCu, 0xC041D25Au, 0x5BCF72B0u);

} // namespace FairyTapes::DeepThroat
