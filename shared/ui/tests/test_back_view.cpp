/** Fairy Tapes shared UI — the back view and its widgets without a window (no drawing): clicks call the item callbacks,
 *  tabs show and hide the settings, refreshes from outside call nothing, the flip helper refuses what it cannot do. */
#include "fairy_ui/back_view.h"
#include "fairy_ui/back_widgets.h"
#include "fairy_ui/flip_view.h"

#include "vstgui/lib/cviewcontainer.h"

#include <cstdio>
#include <vector>

using namespace FairyTapes::Ui;
using namespace VSTGUI;

namespace {
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

const CButtonState kLeft(kLButton);

void click(CView* v, double x, double y) {
  CPoint p(x, y);
  v->onMouseDown(p, kLeft);
}

void test_segment_row_reports_changes_only() {
  auto* row = new SegmentRow(CRect(200, 146, 452, 170), {"OFF", "LIGHT", "HEAVY"}, 1);
  std::vector<int> got;
  row->onSelect = [&](int i) { got.push_back(i); };
  click(row, 210, 150);
  click(row, 210, 150);   // already selected: nothing
  click(row, 440, 150);
  CHECK(got == std::vector<int>({0, 2}) && row->selected() == 2);
  row->setSelected(9);    // clamped
  CHECK(row->selected() == 2);
  row->setEnabled(false);
  click(row, 210, 150);
  CHECK(got.size() == 2);
  row->forget();
}

void test_stepper_and_button() {
  auto* s = new OptionStepper(CRect(200, 242, 428, 266), {"A", "B", "C"}, 0, false);
  std::vector<int> d;
  s->onStep = [&](int delta) { d.push_back(delta); };
  click(s, 205, 250);
  click(s, 420, 250);
  click(s, 300, 250);   // the name: nothing
  CHECK(d == std::vector<int>({-1, 1}));
  s->forget();
  auto* b = new PushButton(CRect(0, 0, 120, 24), "RE-ROLL");
  int pressed = 0;
  b->onClick = [&] { ++pressed; };
  click(b, 10, 10);
  b->setEnabled(false);
  click(b, 10, 10);
  CHECK(pressed == 1);
  b->forget();
}

void test_back_view_items_and_tabs() {
  BackContent c;
  c.headerLine = "SAMPLE";
  c.tabs = {{"SETTINGS", BackTabKind::Settings}, {"CREDITS", BackTabKind::Credits}};
  std::vector<int> changes;
  int presses = 0, backs = 0, tabs = -1;
  c.items.push_back(choiceItem(1, BackColumn::Appearance, "WEAR", "", {"OFF", "LIGHT", "HEAVY"}, 1));
  c.items.back().onChange = [&](int i) { changes.push_back(10 + i); };
  c.items.push_back(stepperItem(2, BackColumn::Appearance, "FONT", "", {"A", "B", "C"}, 0));
  c.items.back().onChange = [&](int i) { changes.push_back(20 + i); };
  c.items.push_back(buttonItem(3, BackColumn::Behaviour, "LOOK", "RE-ROLL", ""));
  c.items.back().onPress = [&] { ++presses; };
  c.credits = {"SAMPLE", "Version 1", {}, {{"VSTGUI", "Steinberg Media Technologies", "BSD-3-Clause"}}, {}};
  BackView::Callbacks cb;
  cb.back = [&] { ++backs; };
  cb.tabChanged = [&](int t) { tabs = t; };
  auto* v = new BackView(CRect(0, 0, 1000, 628), nullptr, c, cb, 5);
  CHECK(v->tab() == 1);   // clamped to the last tab
  v->setTab(0);

  // children: wear, the tab links, "<- BACK", then one widget per item
  const int firstItem = 1 + 2 + 1;
  CHECK(static_cast<int>(v->getNbViews()) == firstItem + 3);
  auto* wear = v->getView(firstItem + 0);
  auto* font = v->getView(firstItem + 1);
  auto* look = v->getView(firstItem + 2);
  click(wear, 210, 150);                 // OFF
  click(font, 205, 200);                 // left arrow: wraps to the last option
  click(look, 700, 160);
  CHECK(changes == std::vector<int>({10, 22}) && presses == 1);
  CHECK(v->selected(1) == 0 && v->selected(2) == 2 && v->selected(99) == -1);

  v->setSelected(1, 2);                  // from outside: no callback
  CHECK(v->selected(1) == 2 && changes.size() == 2 && static_cast<SegmentRow*>(wear)->selected() == 2);
  v->setItemEnabled(3, false);
  click(look, 700, 160);
  CHECK(presses == 1);

  click(v->getView(2), 870, 70);         // the CREDITS link
  CHECK(tabs == 1 && v->tab() == 1 && !wear->isVisible() && !look->isVisible());
  click(v->getView(1), 780, 70);         // SETTINGS again
  CHECK(tabs == 0 && wear->isVisible());
  click(v->getView(3), 950, 70);         // "<- BACK"
  CHECK(backs == 1);
  v->forget();
}

void test_panel_flip_refuses_without_views() {
  PanelFlip none;
  CHECK(!none.start(true) && !none.running() && !none.showingBack() && !none.tick());
  auto* host = new CViewContainer(CRect(0, 0, 100, 100));
  auto* front = new CView(CRect(0, 0, 100, 100));
  auto* back = new CView(CRect(0, 0, 100, 100));
  host->addView(front);
  host->addView(back);
  PanelFlip f(host, front, back);
  CHECK(!f.start(false));                // already on the front
  f.reset();
  CHECK(!f.start(true));                 // forgotten views
  host->forget();
}
} // namespace

int main() {
  test_segment_row_reports_changes_only();
  test_stepper_and_button();
  test_back_view_items_and_tabs();
  test_panel_flip_refuses_without_views();
  if (g_failed) { std::fprintf(stderr, "%d check(s) failed\n", g_failed); return 1; }
  std::puts("test_back_view: ok");
  return 0;
}
