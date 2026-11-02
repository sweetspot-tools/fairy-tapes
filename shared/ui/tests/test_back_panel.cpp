/**
 * Fairy Tapes shared UI — the back of the panel without VSTGUI (fairy_ui/back_panel.h): the fixed template (tabs, rows,
 * columns, credits), the data a plug-in supplies (items, credits) and its checks, hit tests of the widgets, word wrapping.
 * Word wrapping and the stepper rule are ported from src/the-stripper/tests/test_back_panel.cpp; the rest of the
 * Stripper's test is about its own help and preferences and stays there.
 */
#include "fairy_ui/back_panel.h"
#include "wide_back_layout.h"

#include <cstdio>
#include <set>
#include <string>

using namespace FairyTapes::Ui;

namespace {
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

bool inside(const BackBox& inner, const BackBox& outer) {
  return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.w <= outer.x + outer.w && inner.y + inner.h <= outer.y + outer.h;
}
bool disjoint(const BackBox& a, const BackBox& b) {
  return a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y;
}
bool hasProblem(const BackContent& c, const std::string& needle) {
  for (const auto& p : backContentProblems(c))
    if (p.find(needle) != std::string::npos) return true;
  return false;
}

BackContent sample() {
  BackContent c;
  c.headerLine = "SAMPLE  \xC2\xB7  TEST VST3";
  c.tabs = {{"SETTINGS", BackTabKind::Settings}, {"ROUTING", BackTabKind::Routing}, {"CREDITS", BackTabKind::Credits}};
  c.items.push_back(choiceItem(1, BackColumn::Appearance, "WEAR", "", {"OFF", "LIGHT", "HEAVY"}, 1));
  c.items.push_back(toggleItem(2, BackColumn::Appearance, "NOTES LAYER", "marks", "HIDE", "SHOW", true));
  c.items.push_back(stepperItem(3, BackColumn::Appearance, "TAPE FONT", "handwriting", {"Zeyada", "Rock Salt", "Homemade Apple"}, 0, true));
  c.items.push_back(buttonItem(4, BackColumn::Appearance, "LOOK", "RE-ROLL", "new wear"));
  c.items.push_back(choiceItem(5, BackColumn::Behaviour, "DISPLAY WINDOW", "time shown", {"3 S", "6 S", "12 S"}, 1));
  c.notes = {"one", "two"};
  c.credits.title = "SAMPLE";
  c.credits.versionLine = "Version 1.0.0";
  c.credits.licenseLines = {"GPL-3.0"};
  c.credits.entries = {{"VST 3 SDK", "Steinberg Media Technologies", "MIT"}, {"VSTGUI", "Steinberg Media Technologies", "BSD-3-Clause"}};
  c.credits.notices = {"VST is a trademark of Steinberg Media Technologies GmbH."};
  RoutingBuilder r;
  r.jack(60, 200, "IN L \xC2\xB7 R");
  r.ln("M69 200 H194");
  r.block(194, 304, 200, 48, "FILTERS", {"HP \xC2\xB7 LP"});
  c.routing.flow = r.take();
  c.routing.legend.symbols = {LegendSymbol::Module, LegendSymbol::Jack};
  return c;
}

void test_header_links_follow_the_template() {
  // the last tab ends at 880, the tabs are 90 px apart, "<- BACK" ends at 966 (stripper_rack.html / pillow_rack.html)
  const BackBox back = backLinkBox();
  CHECK(back.x + back.w == Back::kHeaderRight && back.y == Back::kLinkTop);
  CHECK(tabLinkBox(1, 2).x + tabLinkBox(1, 2).w == 880 && tabLinkBox(0, 2).x + tabLinkBox(0, 2).w == 790);
  CHECK(tabLinkBox(2, 3).x + tabLinkBox(2, 3).w == 880 && tabLinkBox(0, 3).x + tabLinkBox(0, 3).w == 700);
  for (int n = 1; n <= Back::kMaxTabs; ++n)
    for (int i = 0; i < n; ++i) {
      CHECK(disjoint(tabLinkBox(i, n), back));
      if (i > 0) CHECK(disjoint(tabLinkBox(i, n), tabLinkBox(i - 1, n)));
      CHECK(tabLinkBox(i, n).x > 400);   // clear of the big title
    }
  CHECK(frontBackLinkBox().x + frontBackLinkBox().w == Back::kHeaderRight);   // "BACK ->" on the front, same place
}

// kTall and kShort carry the numbers the horizontal constants had: the existing plug-ins do not move.
void test_back_layouts_unchanged() {
  for (const BackLayout* L : {&Back::kTall, &Back::kShort}) {
    CHECK(L->width == 1000 && L->width == Back::kWidth);
    CHECK(L->headerRight == Back::kHeaderRight && L->headerRight == 966);
    CHECK(L->lastTabRight == Back::kLastTabRight && L->lastTabRight == 880);
    CHECK(L->tabSpacing == Back::kTabSpacing && L->tabSpacing == 90);
    CHECK(L->creditNameX == Back::kCreditNameX && L->creditAuthorX == Back::kCreditAuthorX);
    CHECK(L->creditLicenseRight == Back::kCreditLicenseRight);
    CHECK(L->appearance.x == 24 && L->appearance.w == 462 && L->behaviour.x == 514 && L->behaviour.w == 462);
    for (const BackBox& b : {L->notes, L->creditsTitle, L->components, L->notices, L->custom, L->flow, L->legend})
      CHECK(b.x == 24 && b.w == 952);
  }
  CHECK(Back::kTall.height == 628 && Back::kShort.height == 470);
  // the old signatures give the kTall places, the new ones agree with them for kTall and kShort
  for (int n = 1; n <= Back::kMaxTabs; ++n)
    for (int i = 0; i < n; ++i) {
      const BackBox a = tabLinkBox(i, n), b = tabLinkBox(i, n, Back::kTall), c = tabLinkBox(i, n, Back::kShort);
      CHECK(a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h && a.x == c.x);
    }
  CHECK(backLinkBox().x == backLinkBox(Back::kTall).x && backLinkBox().x == 966 - Back::kLinkWidth);
  CHECK(labelX(BackColumn::Behaviour) == labelX(BackColumn::Behaviour, Back::kShort));
}

// A layout of any width: the header links, the columns and the credit columns come from its own fields.
void test_back_layout_any_width() {
  const BackLayout L = wideBackLayout();
  const BackBox back = backLinkBox(L);
  CHECK(back.x + back.w == 1246 && back.y == Back::kLinkTop && back.w == Back::kLinkWidth);
  CHECK(frontBackLinkBox(L).x == back.x && frontBackLinkBox().x == 966 - Back::kLinkWidth);
  CHECK(tabLinkBox(1, 2, L).x + tabLinkBox(1, 2, L).w == 1160 && tabLinkBox(0, 2, L).x + tabLinkBox(0, 2, L).w == 1060);
  CHECK(tabLinkBox(0, 4, L).x + tabLinkBox(0, 4, L).w == 860);
  for (int n = 1; n <= Back::kMaxTabs; ++n)
    for (int i = 0; i < n; ++i) {
      const BackBox t = tabLinkBox(i, n, L);
      CHECK(disjoint(t, back) && t.x > 400 && t.x + t.w <= L.width);
      if (i > 0) CHECK(disjoint(t, tabLinkBox(i - 1, n, L)));
    }
  CHECK(labelX(BackColumn::Appearance, L) == 24 + Back::kLabelInset && labelX(BackColumn::Behaviour, L) == 654 + Back::kLabelInset);
  CHECK(columnBox(BackColumn::Behaviour, L).w == 602);
  // the boxes stay inside the panel and the columns do not overlap
  const BackBox panel{0, 0, L.width, L.height};
  for (const BackBox& b : {L.appearance, L.behaviour, L.notes, L.creditsTitle, L.components, L.notices, L.custom, L.flow, L.legend})
    CHECK(inside(b, panel) && b.x >= 24 && b.x + b.w <= L.width - 24);
  CHECK(disjoint(L.appearance, L.behaviour));
  // the controls of an item follow the wider column; a control that was too wide for 462 px fits in 602
  const BackContent c = sample();
  CHECK(itemControlBox(c.items[0], 0, L).x == 24 + Back::kControlInset);
  BackContent wide = c;
  wide.items.push_back(choiceItem(9, BackColumn::Behaviour, "WIDE", "", {"A", "B", "C", "D"}, 0, 84));   // 336 px
  CHECK(itemControlBox(wide.items.back(), 0, L).x == 654 + Back::kControlInset);
  bool leaves = false;
  for (const auto& p : backContentProblems(wide, L)) leaves = leaves || p.find("leaves its box") != std::string::npos;
  CHECK(!leaves);
  leaves = false;
  for (const auto& p : backContentProblems(wide, Back::kTall)) leaves = leaves || p.find("leaves its box") != std::string::npos;
  CHECK(leaves);
  CHECK(backContentProblems(c, L).empty());
  // credits: the columns of the layout
  CHECK(L.creditAuthorX > L.creditNameX && L.creditLicenseRight <= L.width && L.creditLicenseRight <= L.components.x + L.components.w);
}

void test_rows_and_controls_stay_in_their_column() {
  const BackContent c = sample();
  CHECK(itemRow(c.items, 0) == 0 && itemRow(c.items, 3) == 3 && itemRow(c.items, 4) == 0);   // rows count per column
  CHECK(itemRowY(0) == 146 && itemRowY(1) == 194 && itemRowY(6) == 434);                     // the preview's rows
  const BackBox wear = itemControlBox(c.items[0], 0);
  CHECK(wear.x == 200 && wear.y == 146 && wear.w == 3 * 84 && wear.h == 24);
  const BackBox window = itemControlBox(c.items[4], 0);
  CHECK(window.x == 690 && window.w == 3 * 84);
  const BackBox stepper = itemControlBox(c.items[2], 2);
  CHECK(stepper.x == 200 && stepper.w == 2 * Back::kArrowWidth + 2 * Back::kArrowGap + 176);   // < name > as in the preview
  CHECK(itemControlBox(c.items[3], 3).w == 120);
  CHECK(labelX(BackColumn::Appearance) == 50 && labelX(BackColumn::Behaviour) == 540);
  CHECK(backContentProblems(c).empty());
}

void test_problems_are_reported() {
  BackContent c = sample();
  CHECK(!hasProblem(c, "tab"));
  c.tabs.pop_back();
  CHECK(hasProblem(c, "CREDITS"));   // SETTINGS and CREDITS are mandatory (DESIGN_SYSTEM.md)
  c = sample();
  for (int i = 0; i < 8; ++i) c.items.push_back(buttonItem(100 + i, BackColumn::Behaviour, "X", "GO", ""));
  CHECK(hasProblem(c, "rows"));
  c = sample();
  c.items[0].options = {"A", "B", "C", "D", "E", "F", "G"};   // 7 x 84 px leaves the box
  CHECK(hasProblem(c, "WEAR"));
  c = sample();
  c.items[1].key = c.items[0].key;
  CHECK(hasProblem(c, "key"));
  c = sample();
  c.items[0].selected = 7;
  CHECK(hasProblem(c, "selected"));
  c = sample();
  c.items[2].options.clear();
  CHECK(hasProblem(c, "options"));
  c = sample();
  c.credits.entries.push_back({"", "x", "MIT"});
  CHECK(hasProblem(c, "credit"));
  c = sample();
  c.credits.notices = {"a", "b", "c", "d"};
  CHECK(hasProblem(c, "notices"));
  c = sample();
  c.notes = {"a", "b", "c", "d", "e"};
  CHECK(hasProblem(c, "notes"));
}

void test_routing_is_mandatory_and_inside_its_box() {
  BackContent c = sample();
  CHECK(backContentProblems(c).empty());
  c.tabs.erase(c.tabs.begin() + 1);
  CHECK(hasProblem(c, "ROUTING"));
  c = sample();
  c.routing.flow.clear();
  CHECK(hasProblem(c, "routing: no diagram"));
  c = sample();
  RoutingBuilder r;
  r.ln("M60 200 H990");   // past the right border of SIGNAL FLOW
  c.routing.flow = r.take();
  CHECK(hasProblem(c, "leaves the SIGNAL FLOW"));
  c = sample();
  c.routing.legend.notes = {"a", "b", "c"};
  CHECK(hasProblem(c, "legend notes"));
}

void test_short_layout_is_the_pillow_talk_back() {
  // 1000 x 470 (pillow_rack.html, 2026-10-21): the same template with lower boxes
  CHECK(&backLayoutFor(470) == &Back::kShort && &backLayoutFor(628) == &Back::kTall);
  const BackLayout& L = Back::kShort;
  CHECK(itemRowY(0, L) == 140 && itemRowY(4, L) == 300 && L.maxRows == 5);
  CHECK(L.appearance.y + L.appearance.h == 336 && L.notes.y == 350 && L.notes.y + L.notes.h <= 470 - 20);
  CHECK(creditBaseline(0, 5, L) == 238 && creditBaseline(4, 5, L) == 326);   // rows of 22 px
  CHECK(L.components.y + L.components.h <= L.notices.y && L.notices.y + L.notices.h <= 470 - 20);
  CHECK(L.flow.y + L.flow.h <= L.legend.y && L.legend.y + L.legend.h <= 470 - 20);
  for (const BackBox& b : {L.appearance, L.behaviour, L.notes, L.creditsTitle, L.components, L.notices, L.custom, L.flow, L.legend})
    CHECK(b.y >= 102 && b.y + b.h <= 450);
  // five rows per column fit, a sixth does not
  BackContent c = sample();
  for (int k = 0; k < 2; ++k) c.items.push_back(buttonItem(10 + k, BackColumn::Appearance, "X", "GO", ""));
  bool tooMany = false;
  for (const auto& p : backContentProblems(c, L)) tooMany = tooMany || p.find("too many rows") != std::string::npos;
  CHECK(tooMany);   // six rows in APPEARANCE
  c.items.pop_back();
  for (const auto& p : backContentProblems(c, L)) CHECK(p.find("too many rows") == std::string::npos);
  // the tall layout is the template of the constants
  CHECK(Back::kTall.firstRowY == Back::kFirstRowY && Back::kTall.flow.h == 388 && Back::kTall.legend.y == 508);
}

void test_credit_rows_fit_the_box() {
  CHECK(creditRowStep(5) == 30);                      // the preview: 5 rows, 30 px apart
  CHECK(creditBaseline(0, 5) == 284 && creditBaseline(4, 5) == 404);
  for (int n = 1; n <= 20; ++n) {
    const double last = creditBaseline(n - 1, n);
    CHECK(last <= Back::kCreditLastBaseline && creditRowStep(n) > 0 && creditRowStep(n) <= 30);
  }
}

void test_hit_tests() {
  const BackBox b{200, 146, 252, 24};
  CHECK(segmentAt(b, 3, 200) == 0 && segmentAt(b, 3, 300) == 1 && segmentAt(b, 3, 451) == 2);
  CHECK(segmentAt(b, 3, 199) == -1 && segmentAt(b, 3, 452) == -1 && segmentAt(b, 0, 300) == -1);
  const BackBox s{200, 242, 228, 24};
  CHECK(stepperDirectionAt(s, 205) == -1 && stepperDirectionAt(s, 420) == 1 && stepperDirectionAt(s, 300) == 0);
  CHECK(wrapIndex(0, -1, 3) == 2 && wrapIndex(2, 1, 3) == 0 && wrapIndex(1, 1, 3) == 2 && wrapIndex(5, 0, 0) == 0);
  CHECK(clampIndex(-3, 3) == 0 && clampIndex(9, 3) == 2 && clampIndex(1, 3) == 1 && clampIndex(1, 0) == 0);
}

void test_lookup() {
  const BackContent c = sample();
  CHECK(findItem(c, 3) == 2 && findItem(c, 99) == -1);
  CHECK(tabIndexOf(c, BackTabKind::Credits) == 2 && tabIndexOf(c, BackTabKind::Routing) == 1 && tabIndexOf(c, BackTabKind::Custom) == -1);
  CHECK(toggleItem(9, BackColumn::Behaviour, "X", "", "OFF", "ON", true).selected == 1);
  CHECK(buttonItem(9, BackColumn::Behaviour, "X", "GO", "").options.size() == 1);
}

// ---- ported from The Stripper -------------------------------------------------------------------------------------
std::size_t utf8Count(const std::string& s) {
  std::size_t n = 0;
  for (unsigned char c : s) n += (c & 0xC0) != 0x80;
  return n;
}

void test_word_wrap() {
  auto measure = [](const std::string& s) { return 6.0 * static_cast<double>(utf8Count(s)); };   // every character 6 px
  const auto lines = wrapText("the quick brown fox jumps over the lazy dog", 66, measure);      // 11 characters per line
  CHECK(lines.size() >= 4);
  for (const auto& l : lines) CHECK(measure(l) <= 66.0 && !l.empty() && l.front() != ' ' && l.back() != ' ');
  std::string joined;
  for (const auto& l : lines) joined += (joined.empty() ? "" : " ") + l;
  CHECK(joined == "the quick brown fox jumps over the lazy dog");              // nothing lost, nothing added
  CHECK(wrapText("", 100, measure).empty());
  const auto longWord = wrapText("supercalifragilistic short", 36, measure);   // a word wider than the line is cut, never dropped
  CHECK(!longWord.empty());
  std::string all;
  for (const auto& l : longWord) { CHECK(measure(l) <= 36.0); all += l; }
  CHECK(all == "supercalifragilisticshort");
  CHECK(wrapText("a b", 0.5, measure).size() >= 1);                            // nothing fits: no loop, no crash
  const auto utf = wrapText("\xC3\xA8\xC3\xA8\xC3\xA8\xC3\xA8\xC3\xA8\xC3\xA8 x", 24, measure);   // never cuts a UTF-8 sequence
  for (const auto& l : utf) CHECK(decodeValid(l));
  CHECK(!decodeValid("\xC3") && !decodeValid("\x80") && decodeValid("abc"));
}
} // namespace

int main() {
  test_header_links_follow_the_template();
  test_back_layouts_unchanged();
  test_back_layout_any_width();
  test_rows_and_controls_stay_in_their_column();
  test_problems_are_reported();
  test_routing_is_mandatory_and_inside_its_box();
  test_short_layout_is_the_pillow_talk_back();
  test_credit_rows_fit_the_box();
  test_hit_tests();
  test_lookup();
  test_word_wrap();
  if (g_failed) { std::fprintf(stderr, "%d check(s) failed\n", g_failed); return 1; }
  std::puts("test_back_panel: ok");
  return 0;
}
