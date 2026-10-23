/**
 * Fairy Tapes · Pillow Talk — the CREDITS tab of the back panel (ui/credits.h): every component with name, author and
 * license; the VST 3 SDK, VSTGUI and the three tape fonts present with the licenses of their LICENSE files; the GPL-3.0
 * notice and the plug-in version; the credits fit the shared template. Build target: pillow_talk_credits_tests.
 */
#include "ui/credits.h"
#include "version.h"

#include <cstdio>
#include <set>
#include <string>

using namespace FairyTapes::PillowTalk;
using namespace FairyTapes::PillowTalk::Ui;

namespace {
int g_failed = 0, g_passed = 0;

#define ASSERT_TRUE(expr) do { \
  if (!(expr)) { \
    std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); \
    ++g_failed; return; \
  } \
} while(0)
#define RUN_TEST(fn) do { const int before = g_failed; fn(); if (g_failed == before) ++g_passed; } while(0)

bool contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

void test_every_entry_is_complete_and_unique() {
  std::set<std::string> names;
  for (const Credit& c : kCredits) {
    ASSERT_TRUE(c.name && *c.name && c.author && *c.author && c.license && *c.license);
    ASSERT_TRUE(names.insert(c.name).second);   // no component twice
  }
  ASSERT_TRUE(kCreditCount == static_cast<int>(names.size()) && kCreditCount >= 5);
}

void test_required_components_and_their_licenses() {
  std::string all;
  for (const Credit& c : kCredits) all += std::string(c.name) + "|" + c.author + "|" + c.license + "\n";
  // vendors/vst3sdk/LICENSE.txt (MIT) and vendors/vst3sdk/vstgui4/LICENSE (BSD 3-clause)
  ASSERT_TRUE(contains(all, "VST 3 SDK|Steinberg Media Technologies|MIT"));
  ASSERT_TRUE(contains(all, "VSTGUI|Steinberg Media Technologies|BSD-3-Clause"));
  // shared/ui/assets/fonts/README.md and the license files next to it
  ASSERT_TRUE(contains(all, "Zeyada") && contains(all, "Kimberly Geswein|OFL-1.1"));
  ASSERT_TRUE(contains(all, "Rock Salt") && contains(all, "Homemade Apple"));
  int apache = 0;
  for (const Credit& c : kCredits)
    if ((contains(c.name, "Rock Salt") || contains(c.name, "Homemade Apple")) && std::string(c.license) == "Apache-2.0" &&
        std::string(c.author) == "Font Diner")
      ++apache;
  ASSERT_TRUE(apache == 2);
}

void test_license_notice_version_and_template() {
  ASSERT_TRUE(std::string(kPluginVersion) == PILLOW_TALK_VERSION && *kPluginVersion);
  const FairyTapes::Ui::BackCredits c = backCredits();
  ASSERT_TRUE(c.title == "PILLOW TALK");
  ASSERT_TRUE(contains(c.versionLine, std::string("Version ") + kPluginVersion));
  ASSERT_TRUE(contains(c.versionLine, "Fairy Tapes  \xC2\xB7  github.com/sweetspot-tools"));   // suite and publisher
  std::string license;
  for (const auto& l : c.licenseLines) license += l + "\n";
  ASSERT_TRUE(contains(license, "GNU General Public License v3.0") && contains(license, "GPL-3.0"));
  std::string notices;
  for (const auto& n : c.notices) notices += n + "\n";
  ASSERT_TRUE(contains(notices, "trademark") && contains(notices, "Steinberg"));   // the VST trademark notice
  ASSERT_TRUE(static_cast<int>(c.entries.size()) == kCreditCount);
  ASSERT_TRUE(static_cast<int>(c.licenseLines.size()) <= FairyTapes::Ui::Back::kMaxLicenseLines);
  ASSERT_TRUE(static_cast<int>(c.notices.size()) <= FairyTapes::Ui::Back::kMaxNotices);
}
} // namespace

int main() {
  RUN_TEST(test_every_entry_is_complete_and_unique);
  RUN_TEST(test_required_components_and_their_licenses);
  RUN_TEST(test_license_notice_version_and_template);
  std::printf("%d passed, %d failed\n", g_passed, g_failed);
  return g_failed ? 1 : 0;
}
