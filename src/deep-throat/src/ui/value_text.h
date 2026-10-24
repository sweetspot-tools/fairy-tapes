/**
 * Fairy Tapes · Deep Throat — the values printed under the knobs and the typed values (model:
 * src/the-stripper/src/ui/value_text.h, adapted). The formatting and the parsing are the controller's own
 * (src/value_format.h): the panel prints exactly what the host shows, and a typed value is read by
 * Controller::getParamValueByString. This header adds what only the editor needs: the text a value field starts with.
 * Pure, no VSTGUI: unit tested in tests/test_ui.cpp.
 */

#pragma once

#include "value_format.h"

#include <string>

namespace FairyTapes::DeepThroat::Ui {

using FairyTapes::DeepThroat::formatKnobValue;
using FairyTapes::DeepThroat::isKnobParam;
using FairyTapes::DeepThroat::parseKnobValue;

// The text a value field opens with: the printed number without its unit ("+6.0 dB" -> "+6.0", "6.5 kHz" -> "6.5"),
// which parseKnobValue reads back in the knob's own unit.
inline std::string editFieldText(Steinberg::Vst::ParamID id, double norm) {
  const std::string s = formatKnobValue(id, norm);
  const std::size_t space = s.find(' ');
  return space == std::string::npos ? s : s.substr(0, space);
}

} // namespace FairyTapes::DeepThroat::Ui
