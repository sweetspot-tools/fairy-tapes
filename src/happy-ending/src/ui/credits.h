/**
 * Fairy Tapes · Happy Ending — what the CREDITS tab of the back says (docs/DESIGN_SYSTEM.md, "Retro del pannello"; spec §8;
 * preview happy_ending_rack.html): the plug-in version and license, the third-party components with their authors and
 * licenses, the legal notices. Pure data, unit tested (tests/test_ui.cpp); the shared BackView draws it. All visible text is
 * English. Model: Deep Throat's ui/credits.h.
 *
 * The rows agree with THIRD_PARTY.md (the Happy Ending rows) and with what the code really uses (CLAUDE.md §6):
 *   Multi-Q (`AnalogMatchedBiquad`, Tube EQ, `ADAASaturation`): FINE EQ, VIBE EQ     (Dusk Audio, GPL-3.0)
 *   Multi-Comp `BusCompressor`: GLUE                                                 (Dusk Audio, GPL-3.0)
 *   ClipSoftly, ClipOnly2: CLIP, FINAL                                               (Airwindows, Chris Johnson, MIT)
 *   ZLInflator shaper curves and mixer: FINAL inflate (shared/inflator)              (ZL Audio, zsliu98, GPL-3.0)
 *   libebur128: the constants of the analog K-weighting prototype of the LUFS meter  (Jan Kokemüller, MIT)
 *   Vicanek, "Matched Second Order Digital Filters" (2016): the matched bells and shelf; the method only, no code
 *   VST 3 SDK, VSTGUI (the editor), the three tape fonts of shared/ui/assets/fonts/ (Zeyada OFL 1.1, Rock Salt and Homemade
 *   Apple Apache 2.0).
 * ZL Equalizer is not used (AGPL-3.0, excluded).
 */
#pragma once

#include "version.h"

#include "fairy_ui/back_panel.h"

#include <string>
#include <vector>

namespace FairyTapes::HappyEnding::Ui {

inline std::vector<FairyTapes::Ui::CreditEntry> credits() {
  return {
      {"Multi-Q (Fine EQ, Vibe EQ)", "Dusk Audio", "GPL-3.0"},
      {"Multi-Comp (Glue)", "Dusk Audio", "GPL-3.0"},
      {"Airwindows (ClipSoftly, ClipOnly2)", "Chris Johnson", "MIT"},
      {"ZLInflator (Final: Inflate)", "ZL Audio (zsliu98)", "GPL-3.0"},
      {"libebur128 (LUFS K-weighting)", "Jan Kokem\xC3\xBCller", "MIT"},
      {"Matched Second Order Digital Filters", "Martin Vicanek", "published method"},
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
  c.title = "HAPPY ENDING";
  c.versionLine = std::string("Version ") + kPluginVersion + "  \xC2\xB7  Fairy Tapes  \xC2\xB7  github.com/sweetspot-tools";
  c.licenseLines = {
      "Free software: released under the GNU General Public License v3.0 (GPL-3.0), full text in the LICENSE file.",
      "The plugin includes GPL-3.0 components, so the combined work is distributed under the same license.",
      "Source code and build instructions are available in the project repository.",
  };
  c.entries = credits();
  c.notices = notices();
  return c;
}

} // namespace FairyTapes::HappyEnding::Ui
