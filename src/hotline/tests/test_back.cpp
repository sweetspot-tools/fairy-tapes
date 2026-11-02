/**
 * Fairy Tapes · Hotline — the back of the panel without a window: the content for the shared template (ui/back_settings.h),
 * the ROUTING diagram of the spec (ui/routing_diagram.h), the CREDITS (ui/credits.h) and the DEVICES cards generated from
 * the engine's device table (ui/devices_tab.h). Build target: hotline_back_tests.
 */
#include "devices.h"
#include "ui/back_settings.h"
#include "ui/credits.h"
#include "ui/devices_tab.h"
#include "ui/editor_size.h"
#include "ui/routing_diagram.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

using namespace FairyTapes::Hotline;
using namespace FairyTapes::Hotline::Ui;
using FairyTapes::Ui::BackBox;
using FairyTapes::Ui::BackColumn;
using FairyTapes::Ui::BackItemKind;
using FairyTapes::Ui::BackTabKind;
using FairyTapes::Ui::RoutingPrim;
using FairyTapes::Ui::WearLevel;

namespace {
int g_failed = 0, g_passed = 0;

#define ASSERT_TRUE(expr) do { \
  if (!(expr)) { \
    std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); \
    ++g_failed; return; \
  } \
} while(0)
#define RUN_TEST(fn) do { const int before = g_failed; fn(); if (g_failed == before) ++g_passed; } while(0)

const FairyTapes::Ui::BackItem* item(const FairyTapes::Ui::BackContent& c, int key) {
  const int i = FairyTapes::Ui::findItem(c, key);
  return i < 0 ? nullptr : &c.items[static_cast<std::size_t>(i)];
}
bool contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

// ---- the diagram's text primitives -------------------------------------------------------------------------------------
std::vector<const RoutingPrim*> texts(const FairyTapes::Ui::RoutingDiagram& d, const std::string& s, double size = -1) {
  std::vector<const RoutingPrim*> out;
  for (const RoutingPrim& p : d)
    if (p.kind == RoutingPrim::Kind::Text && p.text == s && (size < 0 || p.size == size)) out.push_back(&p);
  return out;
}
const RoutingPrim* blockName(const FairyTapes::Ui::RoutingDiagram& d, const std::string& s) {
  const auto v = texts(d, s, 11);   // the module names are the 11 px texts
  return v.size() == 1 ? v[0] : nullptr;
}
// Reading order of the diagram: the first row (y < 215, the signal path above the DEVICE line), then the second.
double readingKey(const RoutingPrim& p) { return (p.y > 215 ? 10000.0 : 0.0) + p.x; }

void test_routing_has_every_spec_block_in_order() {
  const auto r = hotlineRouting();
  ASSERT_TRUE(!r.flow.empty());
  // IN -> meter IN -> MONO -> dry tap -> BAND -> DRIVE (clip DRV) -> CONE -> CRUSH -> COMPANDER -> NOISE sum -> ROOM -> MIX
  // -> OUTPUT gain -> meter OUT -> OUT, all in the reading order of the signal.
  const char* modules[] = {"MONO", "BAND", "DRIVE", "CONE", "CRUSH", "COMPANDER", "NOISE", "ROOM", "MIX"};
  double last = -1;
  for (const char* m : modules) {
    const RoutingPrim* t = blockName(r.flow, m);
    ASSERT_TRUE(t != nullptr);
    ASSERT_TRUE(readingKey(*t) > last);
    last = readingKey(*t);
  }
  // the jacks, the meters, the clip tap, the output gain
  const auto in = texts(r.flow, "IN L \xC2\xB7 R"), out = texts(r.flow, "OUT L \xC2\xB7 R");
  ASSERT_TRUE(in.size() == 1 && out.size() == 1);
  const auto inMeter = texts(r.flow, "IN METER"), clipIn = texts(r.flow, "CLIP IN"), clipDrv = texts(r.flow, "CLIP DRV");
  const auto outMeter = texts(r.flow, "OUT METER"), clipOut = texts(r.flow, "CLIP OUT"), gain = texts(r.flow, "OUTPUT");
  ASSERT_TRUE(inMeter.size() == 1 && clipIn.size() == 1 && clipDrv.size() == 1);
  ASSERT_TRUE(outMeter.size() == 1 && clipOut.size() == 1 && gain.size() == 1);
  const double xMono = blockName(r.flow, "MONO")->x, xBand = blockName(r.flow, "BAND")->x, xDrive = blockName(r.flow, "DRIVE")->x,
               xCone = blockName(r.flow, "CONE")->x;
  ASSERT_TRUE(in[0]->x < inMeter[0]->x && inMeter[0]->x < xMono);   // IN, then its meter, then MONO
  ASSERT_TRUE(xDrive < clipDrv[0]->x && clipDrv[0]->x < xCone);     // the clip tap sits on the DRIVE output
  const RoutingPrim* mix = blockName(r.flow, "MIX");
  ASSERT_TRUE(readingKey(*gain[0]) > readingKey(*mix) && readingKey(*outMeter[0]) > readingKey(*gain[0]));
  ASSERT_TRUE(readingKey(*out[0]) > readingKey(*outMeter[0]));
  // the module names are on the rectangles of their blocks (11 px name inside a Rect)
  for (const char* m : modules) {
    const RoutingPrim* t = blockName(r.flow, m);
    bool inside = false;
    for (const RoutingPrim& p : r.flow)
      if (p.kind == RoutingPrim::Kind::Rect && t->x > p.x && t->x < p.x + p.w && t->y > p.y && t->y < p.y + p.h) inside = true;
    ASSERT_TRUE(inside);
  }
}

void test_routing_dry_tap_global_bypass_and_mono_off() {
  const auto r = hotlineRouting();
  // the dry tap after MONO and before BAND (connector B), and the dry coming back into MIX (the other B)
  const auto b = texts(r.flow, "B", 11);
  ASSERT_TRUE(b.size() == 2);
  const double xMono = blockName(r.flow, "MONO")->x, xBand = blockName(r.flow, "BAND")->x, xMix = blockName(r.flow, "MIX")->x;
  const RoutingPrim* tap = b[0]->y < 215 ? b[0] : b[1];
  const RoutingPrim* intoMix = b[0]->y < 215 ? b[1] : b[0];
  ASSERT_TRUE(tap->y < 215 && intoMix->y > 215);
  ASSERT_TRUE(tap->x > xMono + 40 && tap->x < xBand - 40);   // between the MONO and BAND blocks
  ASSERT_TRUE(std::fabs(intoMix->x - xMix) < 1.0);           // under MIX
  ASSERT_TRUE(texts(r.flow, "DRY", 9).size() == 2);
  // the compander is reached through the continuation A (row 1 -> row 2)
  ASSERT_TRUE(texts(r.flow, "A", 11).size() == 2);
  // the global BYPASS starts at the input (the dot after the IN jack) and ends at the output switch
  ASSERT_TRUE(texts(r.flow, "BYPASS", 9).size() == 1);
  const double yIn = texts(r.flow, "IN L \xC2\xB7 R")[0]->y - 24;
  bool bypass = false;
  for (const RoutingPrim& p : r.flow)
    if (p.kind == RoutingPrim::Kind::Line && p.pts.size() >= 4 && p.pts.front().x == 82 && p.pts.front().y == yIn && !p.dashed) bypass = true;
  ASSERT_TRUE(bypass);
  ASSERT_TRUE(!texts(r.flow, "output = input, bit for bit").empty());
  // the MONO switch is drawn in the off position: the lever points at the contact of the line that jumps the block
  bool thrown = false;
  for (const RoutingPrim& p : r.flow)
    if (p.kind == RoutingPrim::Kind::Line && p.pts.size() == 2 && p.width < 2 && p.pts[0].y == yIn && p.pts[1].y < yIn - 8 &&
        p.pts[1].x > p.pts[0].x)
      thrown = true;
  ASSERT_TRUE(thrown);
  ASSERT_TRUE(!texts(r.flow, "MONO", 9).empty());   // the switch label
}

void test_routing_device_morph_is_a_dashed_control_line_to_the_device_blocks() {
  const auto r = hotlineRouting();
  const auto dm = texts(r.flow, "DEVICE \xC2\xB7 MORPH");
  ASSERT_TRUE(dm.size() == 1);
  int dashed = 0, vertical = 0;
  for (const RoutingPrim& p : r.flow)
    if (p.kind == RoutingPrim::Kind::Line && p.dashed) {
      ++dashed;
      if (p.pts.size() == 2 && p.pts[0].x == p.pts[1].x) ++vertical;
    }
  ASSERT_TRUE(dashed == 8 && vertical == 7);   // one trunk and one drop for each of the seven blocks the device table sets
  // every dashed drop is under (row 1) or over (row 2) the centre of a block that the device table sets
  const char* set[] = {"BAND", "DRIVE", "CONE", "CRUSH", "COMPANDER", "NOISE", "ROOM"};
  for (const char* m : set) {
    const RoutingPrim* t = blockName(r.flow, m);
    const RoutingPrim* rect = nullptr;   // the rectangle of the block
    for (const RoutingPrim& p : r.flow)
      if (p.kind == RoutingPrim::Kind::Rect && t->x > p.x && t->x < p.x + p.w && t->y > p.y && t->y < p.y + p.h) rect = &p;
    ASSERT_TRUE(rect != nullptr);
    bool hit = false;   // a dashed drop inside the block's width
    for (const RoutingPrim& p : r.flow)
      if (p.kind == RoutingPrim::Kind::Line && p.dashed && p.pts.size() == 2 && p.pts[0].x == p.pts[1].x && p.pts[0].x > rect->x &&
          p.pts[0].x < rect->x + rect->w)
        hit = true;
    ASSERT_TRUE(hit);
  }
}

void test_routing_legend_explains_character() {
  const auto r = hotlineRouting();
  ASSERT_TRUE(!r.legend.symbols.empty() && r.legend.notes.size() == 1);   // the LEGEND box of the 470 px back has room for one line
  std::string all;
  for (const auto& n : r.legend.notes) all += n + "\n";
  ASSERT_TRUE(contains(all, "CHARACTER") && contains(all, " IN ") && contains(all, "DEVICE") && contains(all, "MORPH"));
  // IN off zeroes Drive, Noise and Room (applyCharacterIn): the device's own drive, noise and room go away.
  ASSERT_TRUE(contains(all, "removes drive, noise and room") && !contains(all, "keep the device values"));
  ASSERT_TRUE(all.size() <= 150);   // one line of 11 px text across the box
}

void test_back_content_passes_the_shared_validation() {
  const auto c = backContent(Prefs{});
  const auto problems = FairyTapes::Ui::backContentProblems(c, FairyTapes::Ui::Back::kShort);
  for (const std::string& p : problems) std::fprintf(stderr, "  problem: %s\n", p.c_str());
  ASSERT_TRUE(problems.empty());
  ASSERT_TRUE(c.headerLine == "HOTLINE  \xC2\xB7  LINE DEVICES VST3");
}

void test_back_layout_matches_the_editor_height() {
  ASSERT_TRUE(&FairyTapes::Ui::backLayoutFor(kEditorHeight) == &FairyTapes::Ui::Back::kShort);
  ASSERT_TRUE(FairyTapes::Ui::Back::kShort.height == kEditorHeight);
}

void test_back_tabs_in_spec_order() {
  const auto c = backContent(Prefs{});
  ASSERT_TRUE(c.tabs.size() == 4 && static_cast<int>(c.tabs.size()) <= FairyTapes::Ui::Back::kMaxTabs);
  ASSERT_TRUE(c.tabs[0].label == "SETTINGS" && c.tabs[0].kind == BackTabKind::Settings);
  ASSERT_TRUE(c.tabs[1].label == "DEVICES" && c.tabs[1].kind == BackTabKind::Custom);
  ASSERT_TRUE(c.tabs[2].label == "ROUTING" && c.tabs[2].kind == BackTabKind::Routing);
  ASSERT_TRUE(c.tabs[3].label == "CREDITS" && c.tabs[3].kind == BackTabKind::Credits);
  ASSERT_TRUE(kDevicesTab == 1 && FairyTapes::Ui::tabIndexOf(c, BackTabKind::Custom) == kDevicesTab);
  ASSERT_TRUE(!c.routing.flow.empty());
}

void test_settings_are_appearance_only_and_map_to_prefs() {
  const auto c = backContent(Prefs{});
  ASSERT_TRUE(c.items.size() == 2);   // WEAR and TAPE FONT; Hotline has no behaviour settings yet
  for (const auto& it : c.items) ASSERT_TRUE(it.column == BackColumn::Appearance);
  const auto* wear = item(c, kSetWear);
  const auto* font = item(c, kSetTapeFont);
  ASSERT_TRUE(wear && font);
  ASSERT_TRUE(wear->kind == BackItemKind::Choice && wear->label == "WEAR");
  ASSERT_TRUE((wear->options == std::vector<std::string>{"OFF", "LIGHT", "HEAVY"}));
  ASSERT_TRUE(font->kind == BackItemKind::Stepper && font->label == "TAPE FONT" && font->tapeFontOptions);
  ASSERT_TRUE((font->options == std::vector<std::string>{"Zeyada", "Rock Salt", "Homemade Apple"}));
  // the shown choice is the preference (defaults: Light, Zeyada)
  ASSERT_TRUE(wear->selected == 1 && font->selected == 0);
  Prefs p;
  p.wear = WearLevel::Heavy;
  p.tapeFont = TapeFont::HomemadeApple;
  const auto d = backContent(p);
  ASSERT_TRUE(item(d, kSetWear)->selected == 2 && item(d, kSetTapeFont)->selected == 2);
  ASSERT_TRUE(selectedFor(kSetWear, p) == 2 && selectedFor(kSetTapeFont, p) == 2 && selectedFor(999, p) == 0);
  // a choice goes to the preference it belongs to
  Prefs q;
  ASSERT_TRUE(applySetting(kSetWear, 0, q) && q.wear == WearLevel::Off);
  ASSERT_TRUE(applySetting(kSetWear, 2, q) && q.wear == WearLevel::Heavy);
  ASSERT_TRUE(applySetting(kSetTapeFont, 1, q) && q.tapeFont == TapeFont::RockSalt);
  ASSERT_TRUE(applySetting(kSetTapeFont, 2, q) && q.tapeFont == TapeFont::HomemadeApple);
  // out of range or unknown key: nothing changes
  const Prefs before = q;
  ASSERT_TRUE(!applySetting(kSetWear, 3, q) && !applySetting(kSetWear, -1, q) && !applySetting(kSetTapeFont, 3, q));
  ASSERT_TRUE(!applySetting(12345, 0, q));
  ASSERT_TRUE(q.wear == before.wear && q.tapeFont == before.tapeFont);
  // the other stored preferences are untouched
  ASSERT_TRUE(q.notes == before.notes && q.handwrittenValues == before.handwrittenValues);
}

void test_settings_notes_in_english() {
  const auto c = backContent(Prefs{});
  ASSERT_TRUE(c.notes.size() >= 3 && static_cast<int>(c.notes.size()) <= FairyTapes::Ui::Back::kMaxNotes);
  std::string all;
  for (const auto& n : c.notes) all += n + "\n";
  ASSERT_TRUE(contains(all, "preferences") && contains(all, "no behaviour settings"));
}

// ---- DEVICES --------------------------------------------------------------------------------------------------------------
void test_devices_cards_come_from_the_device_table() {
  const auto cards = devicesLayout(FairyTapes::Ui::Back::kShort.custom);
  ASSERT_TRUE(static_cast<int>(cards.size()) == kNumDevices && kNumDevices == 6);
  for (int i = 0; i < kNumDevices; ++i) {
    const DeviceCard& c = cards[static_cast<std::size_t>(i)];
    ASSERT_TRUE(c.device == i && c.title == kDevices[i].name);
    std::string joined;
    for (const std::string& l : c.lines) joined += (joined.empty() ? "" : "\n") + l;
    ASSERT_TRUE(joined == describeDevice(i));   // exactly the engine's text, no value typed by hand
    ASSERT_TRUE(!c.lines.empty());
  }
}

void test_devices_layout_fits_every_line_of_every_device() {
  const BackBox area = FairyTapes::Ui::Back::kShort.custom;
  const auto cards = devicesLayout(area);
  ASSERT_TRUE(static_cast<int>(cards.size()) == kNumDevices);
  std::size_t longest = 0;
  for (int i = 0; i < kNumDevices; ++i) {
    std::size_t n = 1;
    for (char ch : describeDevice(i)) n += ch == '\n';
    longest = std::max(longest, n);
  }
  ASSERT_TRUE(longest >= 8);   // measured on the real table: the megaphone sets eight blocks
  for (const DeviceCard& c : cards) {
    // the card is inside the area
    ASSERT_TRUE(c.box.x >= area.x && c.box.y >= area.y && c.box.x + c.box.w <= area.x + area.w && c.box.y + c.box.h <= area.y + area.h);
    ASSERT_TRUE(c.lines.size() <= longest);
    // every line: baseline and text inside the card, written on its own row, nothing clipped
    double prevBottom = c.box.y + kCardTitleHeight;
    for (std::size_t i = 0; i < c.lines.size(); ++i) {
      const double baseline = c.firstBaseline + c.step * static_cast<double>(i);
      const double top = baseline - kLineFontSize, bottom = baseline + 3;   // 3 px of descender
      ASSERT_TRUE(top >= prevBottom - 0.001);                               // under the title and the line above
      ASSERT_TRUE(bottom <= c.box.y + c.box.h - kCardPad);
      prevBottom = baseline + 3;
      ASSERT_TRUE(estimatedTextWidth(c.lines[i], kLineFontSize) <= c.box.w - 2 * kLineInset);
    }
    ASSERT_TRUE(estimatedTextWidth(c.title, kTitleFontSize) <= c.box.w - 2 * kTitleInset);
  }
  // the cards do not overlap
  for (std::size_t a = 0; a < cards.size(); ++a)
    for (std::size_t b = a + 1; b < cards.size(); ++b) {
      const BackBox& p = cards[a].box;
      const BackBox& q = cards[b].box;
      const bool apart = p.x + p.w <= q.x || q.x + q.w <= p.x || p.y + p.h <= q.y || q.y + q.h <= p.y;
      ASSERT_TRUE(apart);
    }
  // a grid of three columns by two rows, in the order of the Device buttons
  ASSERT_TRUE(cards[0].box.y == cards[1].box.y && cards[1].box.y == cards[2].box.y && cards[3].box.y > cards[0].box.y);
  ASSERT_TRUE(cards[0].box.x < cards[1].box.x && cards[1].box.x < cards[2].box.x && cards[3].box.x == cards[0].box.x);
}

void test_devices_layout_detects_a_card_too_small() {
  // a deliberately small area cannot hold the lines: the check must say so (the checker can fail)
  const auto cards = devicesLayout({24, 102, 952, 120});
  bool anyTooTall = false;
  for (const DeviceCard& c : cards)
    if (!cardFits(c)) anyTooTall = true;
  ASSERT_TRUE(anyTooTall);
  for (const DeviceCard& c : devicesLayout(FairyTapes::Ui::Back::kShort.custom)) ASSERT_TRUE(cardFits(c));
}

// ---- CREDITS --------------------------------------------------------------------------------------------------------------
void test_credits_list_exactly_what_hotline_links() {
  const auto c = backCredits();
  const std::vector<std::string> expected = {"VST 3 SDK", "VSTGUI", "Zeyada (tape font)", "Rock Salt (tape font)",
                                              "Homemade Apple (tape font)"};
  ASSERT_TRUE(c.entries.size() == expected.size() && kCreditCount == static_cast<int>(expected.size()));
  for (std::size_t i = 0; i < expected.size(); ++i) ASSERT_TRUE(c.entries[i].name == expected[i]);
  ASSERT_TRUE(c.entries[0].author == "Steinberg Media Technologies" && c.entries[0].license == "MIT");
  ASSERT_TRUE(c.entries[1].license == "BSD-3-Clause");
  ASSERT_TRUE(c.entries[2].author == "Kimberly Geswein" && c.entries[2].license == "OFL-1.1");
  ASSERT_TRUE(c.entries[3].author == "Font Diner" && c.entries[3].license == "Apache-2.0");
  ASSERT_TRUE(c.entries[4].author == "Font Diner" && c.entries[4].license == "Apache-2.0");
}

void test_credits_have_no_stripper_components() {
  const auto c = backCredits();
  std::string all = c.title + c.versionLine;
  for (const auto& e : c.entries) all += "|" + e.name + "|" + e.author + "|" + e.license;
  for (const auto& l : c.licenseLines) all += "|" + l;
  for (const auto& n : c.notices) all += "|" + n;
  for (const char* banned : {"NAM", "Neural", "Dusk", "Airwindows", "Eigen", "nlohmann", "4K-EQ", "Multi-Comp", ".nam", "Chris Johnson",
                             "Atkinson", "Niels", "MPL", "GPL-3.0 components"})
    ASSERT_TRUE(!contains(all, banned));
}

void test_credits_version_license_and_notices() {
  ASSERT_TRUE(std::string(kPluginVersion) == HOTLINE_VERSION && *kPluginVersion);
  const auto c = backCredits();
  ASSERT_TRUE(c.title == "HOTLINE");
  ASSERT_TRUE(c.versionLine == std::string("Version ") + kPluginVersion + "  \xC2\xB7  Fairy Tapes  \xC2\xB7  github.com/sweetspot-tools");
  std::string license;
  for (const auto& l : c.licenseLines) license += l + "\n";
  ASSERT_TRUE(contains(license, "GNU General Public License v3.0") && contains(license, "GPL-3.0") && contains(license, "LICENSE file"));
  std::string notices;
  for (const auto& n : c.notices) notices += n + "\n";
  ASSERT_TRUE(contains(notices, "trademark") && contains(notices, "Steinberg") && contains(notices, "Third-party names"));
  ASSERT_TRUE(static_cast<int>(c.licenseLines.size()) <= FairyTapes::Ui::Back::kMaxLicenseLines);
  ASSERT_TRUE(static_cast<int>(c.notices.size()) <= FairyTapes::Ui::Back::kMaxNotices);
  std::set<std::string> names;
  for (const auto& e : c.entries) ASSERT_TRUE(names.insert(e.name).second);
}
} // namespace

int main() {
  RUN_TEST(test_routing_has_every_spec_block_in_order);
  RUN_TEST(test_routing_dry_tap_global_bypass_and_mono_off);
  RUN_TEST(test_routing_device_morph_is_a_dashed_control_line_to_the_device_blocks);
  RUN_TEST(test_routing_legend_explains_character);
  RUN_TEST(test_back_content_passes_the_shared_validation);
  RUN_TEST(test_back_layout_matches_the_editor_height);
  RUN_TEST(test_back_tabs_in_spec_order);
  RUN_TEST(test_settings_are_appearance_only_and_map_to_prefs);
  RUN_TEST(test_settings_notes_in_english);
  RUN_TEST(test_devices_cards_come_from_the_device_table);
  RUN_TEST(test_devices_layout_fits_every_line_of_every_device);
  RUN_TEST(test_devices_layout_detects_a_card_too_small);
  RUN_TEST(test_credits_list_exactly_what_hotline_links);
  RUN_TEST(test_credits_have_no_stripper_components);
  RUN_TEST(test_credits_version_license_and_notices);
  std::printf("hotline_back_tests: %d passed, %d failed\n", g_passed, g_failed);
  return g_failed ? 1 : 0;
}
