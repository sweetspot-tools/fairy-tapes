/** Fairy Tapes · shared UI — panel layer views without a window: state and click-through (no drawing). */
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
}  // namespace

int main() {
  test_wear_view_lets_clicks_through_and_keeps_marks();
  if (g_failed) { std::fprintf(stderr, "%d check(s) failed\n", g_failed); return 1; }
  std::puts("fairy_ui_layer_view_tests: ok");
  return 0;
}
