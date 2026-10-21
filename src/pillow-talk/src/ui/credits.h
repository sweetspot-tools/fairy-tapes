/**
 * Fairy Tapes · Pillow Talk — what the CREDITS tab of the back panel says (docs/DESIGN_SYSTEM.md, "Retro del pannello";
 * preview pillow_rack.html): the plug-in version and license, the third-party components shipped inside the plug-in with
 * their authors and licenses, the legal notices. Pure data, unit tested (tests/test_credits.cpp); the shared BackView
 * draws it. All visible text is English.
 *
 * Licenses as read from the files (2026-10-21): vendors/vst3sdk/LICENSE.txt (MIT), vendors/vst3sdk/vstgui4/LICENSE
 * (BSD 3-clause), shared/ui/assets/fonts/ (README.md and the three license texts: Zeyada OFL 1.1, Rock Salt and
 * Homemade Apple Apache 2.0). Pillow Talk has no GPL-3.0 component of its own (unlike The Stripper): the preview's line
 * "the plugin includes GPL-3.0 components" is left out.
 */
#pragma once

#include "version.h"

#include "fairy_ui/back_panel.h"

#include <string>

namespace FairyTapes::PillowTalk::Ui {

struct Credit {
  const char* name;
  const char* author;
  const char* license;
};

constexpr Credit kCredits[] = {
    {"VST 3 SDK", "Steinberg Media Technologies", "MIT"},
    {"VSTGUI", "Steinberg Media Technologies", "BSD-3-Clause"},
    {"Zeyada (tape font)", "Kimberly Geswein", "OFL-1.1"},
    {"Rock Salt (tape font)", "Font Diner", "Apache-2.0"},
    {"Homemade Apple (tape font)", "Font Diner", "Apache-2.0"},
};
constexpr int kCreditCount = static_cast<int>(sizeof(kCredits) / sizeof(kCredits[0]));

constexpr const char* kLicenseLines[] = {
    "Free software: released under the GNU General Public License v3.0 (GPL-3.0), full text in the LICENSE file.",
    "Source code, build instructions and the license texts of the components are in the project repository.",
};

constexpr const char* kNotices[] = {
    "VST is a trademark of Steinberg Media Technologies GmbH, registered in Europe and other countries.",
    "The tape fonts are embedded as vector outlines of their glyphs; no font file is installed.",
    "Third-party names are cited for attribution only.",
};

inline FairyTapes::Ui::BackCredits backCredits() {
  FairyTapes::Ui::BackCredits c;
  c.title = "PILLOW TALK";
  c.versionLine = std::string("Version ") + kPluginVersion + "  \xC2\xB7  Fairy Tapes  \xC2\xB7  github.com/fairy-tapes";
  for (const char* l : kLicenseLines) c.licenseLines.emplace_back(l);
  for (const Credit& e : kCredits) c.entries.push_back({e.name, e.author, e.license});
  for (const char* n : kNotices) c.notices.emplace_back(n);
  return c;
}

} // namespace FairyTapes::PillowTalk::Ui
