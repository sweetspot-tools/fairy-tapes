/** Fairy Tapes · shared UI — panel layer views without a window: state and click-through (no drawing). */
#include "fairy_ui/silkscreen_view.h"
#include "fairy_ui/wear_view.h"

#include "vstgui/lib/crect.h"

#include <cstdio>

using namespace FairyTapes::Ui;
using namespace VSTGUI;

namespace {
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

void test_wear_view_lets_clicks_through_and_keeps_marks() {
  auto* v = new WearView(CRect(0, 0, kWearWidth, kWearHeight), generateWear(kDefaultWearSeed, WearLevel::Light));
  CHECK(!v->getMouseEnabled());
  CHECK(v->marks().size() == generateWear(kDefaultWearSeed, WearLevel::Light).size());
  v->setMarks(generateWear(kDefaultWearSeed, WearLevel::Off));
  CHECK(v->marks().empty());
  v->forget();
}

void test_silkscreen_view_lets_clicks_through_and_forwards_state() {
  PrintedItem k;
  k.id = 3;
  k.cx = 50;
  k.cy = 50;
  k.name = "MIX";
  auto* v = new SilkscreenView(CRect(0, 0, 200, 100), {k}, {{1, 10, 10}});
  CHECK(!v->getMouseEnabled());
  v->setValueText(3, "50 %");
  v->setDimmed(3, true);
  v->setHidden(3, true);
  v->setTag(1, "NAM");
  v->setTagDimmed(1, true);
  CHECK(v->model().value(0) == "50 %" && v->model().dimmed(0) && v->model().hidden(0));
  CHECK(v->model().tags().size() == 1 && v->model().tags()[0].text == "NAM" && v->model().tags()[0].dimmed);
  v->forget();
}
}  // namespace

int main() {
  test_wear_view_lets_clicks_through_and_keeps_marks();
  test_silkscreen_view_lets_clicks_through_and_forwards_state();
  if (g_failed) { std::fprintf(stderr, "%d check(s) failed\n", g_failed); return 1; }
  std::puts("fairy_ui_layer_view_tests: ok");
  return 0;
}
