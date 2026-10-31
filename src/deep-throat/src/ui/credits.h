/**
 * Fairy Tapes · Deep Throat — what the CREDITS tab of the back says (docs/DESIGN_SYSTEM.md, "Retro del pannello"; spec §8;
 * preview deep_throat_rack.html): the plug-in version and license, the third-party components shipped inside the plug-in
 * with their authors and licenses, the legal notices. Pure data, unit tested (tests/test_ui.cpp); the shared BackView
 * draws it. All visible text is English. Model: Pillow Talk's ui/credits.h.
 *
 * Components: VST 3 SDK (vendors/vst3sdk/LICENSE.txt, MIT), VSTGUI (vendors/vst3sdk/vstgui4/LICENSE, BSD 3-clause), the
 * three tape fonts of shared/ui/assets/fonts/ (Zeyada OFL 1.1, Rock Salt and Homemade Apple Apache 2.0). The DSP is
 * written for this plug-in: no other third-party component.
 */
#pragma once

#include "version.h"

#include "fairy_ui/back_panel.h"

#include <string>
#include <vector>

namespace FairyTapes::DeepThroat::Ui {

inline std::vector<FairyTapes::Ui::CreditEntry> credits() {
  return {
      {"VST 3 SDK", "Steinberg Media Technologies", "MIT"},
      {"VSTGUI", "Steinberg Media Technologies", "BSD-3-Clause"},
      {"Zeyada (tape font)", "Kimberly Geswein", "OFL-1.1"},
      {"Rock Salt (tape font)", "Font Diner", "Apache-2.0"},
      {"Homemade Apple (tape font)", "Font Diner", "Apache-2.0"},
  };
}

// The NOTICES box (the preview's two lines).
inline std::vector<std::string> notices() {
  return {"VST is a trademark of Steinberg Media Technologies GmbH, registered in Europe and other countries.",
          "Third-party names are cited for attribution only."};
}

inline FairyTapes::Ui::BackCredits backCredits() {
  FairyTapes::Ui::BackCredits c;
  c.title = "DEEP THROAT";
  c.versionLine = std::string("Version ") + kPluginVersion + "  \xC2\xB7  Fairy Tapes  \xC2\xB7  github.com/sweetspot-tools";
  c.licenseLines = {
      "Free software: released under the GNU General Public License v3.0 (GPL-3.0), full text in the LICENSE file.",
      "Source code, build instructions and the license texts of the components are in the project repository.",
  };
  c.entries = credits();
  c.notices = notices();
  return c;
}

} // namespace FairyTapes::DeepThroat::Ui
