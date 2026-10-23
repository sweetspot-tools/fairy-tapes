/** Fairy Tapes · The Stripper — what the silkscreen prints: the adapter from the layout table (no VSTGUI). */

#include "ui/silkscreen_items.h"

#include <cstdio>
#include <set>
#include <string>

using namespace FairyTapes::TheStripper;
using namespace FairyTapes::TheStripper::Ui;
using FairyTapes::Ui::PrintKind;

namespace {
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

void test_one_item_per_view_and_unique_ids() {
  const auto items = silkscreenItems();
  std::size_t views = 0;
  for (const auto& c : kControls) views += (c.sharesView || c.kind == Kind::Choice) ? 0 : 1;   // choices carry their names on the face
  CHECK(items.size() == views);
  std::set<int> ids;
  for (const auto& it : items) CHECK(ids.insert(it.id).second);   // ids stay unique: the shared setters act on every item with an id
  CHECK(ids.count(static_cast<int>(kDriveAmount)) == 1 && ids.count(static_cast<int>(kEqSaturation)) == 0);   // one print for the shared Drive knob
}

void test_knobs_print_scale_name_and_ends() {
  for (const auto& it : silkscreenItems()) {
    if (it.kind != PrintKind::Knob) continue;
    CHECK(!it.name.empty() && !it.lo.empty() && !it.hi.empty());
    CHECK(it.capRadius >= 15 && it.capRadius <= 26);
  }
  // the cap centre is 30 px below the top of the knob cell, the cell centred on the cap
  for (const auto& c : kControls) {
    if (c.kind != Kind::Knob || c.sharesView) continue;
    for (const auto& it : silkscreenItems())
      if (it.id == static_cast<int>(c.id)) CHECK(it.cx == c.rect.x + 32 && it.cy == c.rect.y + 30);
  }
}

void test_buttons_and_choices() {
  for (const auto& it : silkscreenItems()) {
    if (it.id == static_cast<int>(kBypass)) CHECK(it.kind == PrintKind::Button && it.name == "BYPASS");
    if (it.id == static_cast<int>(kBypassEQ)) CHECK(it.kind == PrintKind::Button && it.name == "IN");
    if (it.id == static_cast<int>(kEqLFBell)) CHECK(it.kind == PrintKind::Button && it.name.empty());   // an icon, no label
    CHECK(it.kind != PrintKind::Choice);   // nothing printed for the exclusive choices: the names are on their faces
  }
}

void test_tag_slots_follow_the_preview() {
  const auto tags = silkscreenTags();
  CHECK(tags.size() == 6);
  auto at = [&](Module m) { for (const auto& t : tags) if (t.key == static_cast<int>(m)) return t; return FairyTapes::Ui::BlockTag{-1, 0, 0}; };
  CHECK(at(Module::Filters).x == 196 && at(Module::Filters).y == 114);
  CHECK(at(Module::Eq).x == 40 && at(Module::Eq).y == 302);
  CHECK(at(Module::Comp).x == 40 && at(Module::Comp).y == 474);
  CHECK(at(Module::Drive).x == 388 && at(Module::Drive).y == 474);   // Compression 30 px narrower (preview 2026-10-21)
  CHECK(at(Module::Limiter).x == 694 && at(Module::Gate).x == 862);
}
} // namespace

int main() {
  test_one_item_per_view_and_unique_ids();
  test_knobs_print_scale_name_and_ends();
  test_buttons_and_choices();
  test_tag_slots_follow_the_preview();
  if (g_failed) { std::fprintf(stderr, "%d check(s) failed\n", g_failed); return 1; }
  std::puts("test_silkscreen_items: ok");
  return 0;
}
