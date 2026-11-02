/** Fairy Tapes shared UI — the back view and its widgets without a window (no drawing): clicks call the item callbacks,
 *  tabs show and hide the settings, refreshes from outside call nothing, the flip helper refuses what it cannot do. */
#include "fairy_ui/back_view.h"
#include "fairy_ui/back_widgets.h"
#include "fairy_ui/flip_view.h"

#include "wide_back_layout.h"

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

// A click on a container at (x, y): the topmost visible child that contains the point gets it (a stand-in for the frame's
// dispatch, which needs a window). Returns false when no child is there.
bool clickAt(CViewContainer* c, double x, double y) {
  for (int i = static_cast<int>(c->getNbViews()) - 1; i >= 0; --i) {
    CView* child = c->getView(static_cast<uint32_t>(i));
    if (!child->isVisible() || !child->getViewSize().pointInside(CPoint(x, y))) continue;
    click(child, x, y);   // children take the coordinates of their parent, as in the other tests
    return true;
  }
  return false;
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

// A plug-in with its own panel size passes its layout: the links and the controls go where that layout says.
void test_back_view_uses_given_layout() {
  const BackLayout L = wideBackLayout();
  BackContent c;
  c.headerLine = "SAMPLE";
  c.tabs = {{"SETTINGS", BackTabKind::Settings}, {"CREDITS", BackTabKind::Credits}};
  int selected = -1, backs = 0, tabs = -1;
  c.items.push_back(choiceItem(1, BackColumn::Behaviour, "MODE", "", {"A", "B", "C"}, 0));
  c.items.back().onChange = [&](int i) { selected = i; };
  c.credits = {"SAMPLE", "Version 1", {}, {{"VSTGUI", "Steinberg Media Technologies", "BSD-3-Clause"}}, {}};
  BackView::Callbacks cb;
  cb.back = [&] { ++backs; };
  cb.tabChanged = [&](int t) { tabs = t; };
  auto* v = new BackView(CRect(0, 0, 1280, 628), nullptr, c, cb, 0, L);
  CHECK(v->layout().width == 1280 && v->layout().headerRight == 1246 && v->layout().lastTabRight == 1160);

  // children: wear, two tab links, "<- BACK", the item
  const BackBox credits = tabLinkBox(1, 2, L), home = backLinkBox(L), settings = tabLinkBox(0, 2, L);
  // the click goes to the child under the point, so the place decides which link (if any) takes them
  clickAt(v, 870, 70);                                   // where the 1000 px template had CREDITS: nothing here
  CHECK(tabs == -1 && v->tab() == 0);
  clickAt(v, credits.x + credits.w - 10, credits.y + 5);   // its new place
  CHECK(tabs == 1 && v->tab() == 1);
  clickAt(v, settings.x + settings.w - 10, settings.y + 5);
  CHECK(tabs == 0 && v->tab() == 0);
  clickAt(v, 950, 70);                                   // the old "<- BACK" place
  CHECK(backs == 0);
  clickAt(v, home.x + home.w - 10, home.y + 5);
  CHECK(backs == 1);
  CHECK(v->getView(2)->getViewSize().right == credits.x + credits.w && v->getView(3)->getViewSize().right == 1246);

  // the control sits in the wide BEHAVIOUR column (x 654 + 176), 3 x 84 px
  const CRect r = v->getView(4)->getViewSize();
  CHECK(r.left == 654 + Back::kControlInset && r.getWidth() == 3 * 84 && r.top == L.firstRowY);
  clickAt(v, 654 + Back::kControlInset + 200, L.firstRowY + 5);   // the third cell
  CHECK(selected == 2);
  v->forget();

  // without a layout: the one of the view's height, as before
  auto* classic = new BackView(CRect(0, 0, 1000, 470), nullptr, c, cb, 0);
  CHECK(classic->layout().height == 470 && classic->layout().width == 1000 && classic->layout().lastTabRight == 880);
  classic->forget();
}

void test_text_link_alignment() {
  TextLink link(CRect(0, 0, 100, 20), "Powered by");
  CHECK(!link.leftAligned());                       // right-aligned like the tab links by default
  link.setLeftAligned(true);
  CHECK(link.leftAligned());
  link.setLeftAligned(false);
  CHECK(!link.leftAligned());
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
  test_back_view_uses_given_layout();
  test_text_link_alignment();
  test_panel_flip_refuses_without_views();
  if (g_failed) { std::fprintf(stderr, "%d check(s) failed\n", g_failed); return 1; }
  std::puts("test_back_view: ok");
  return 0;
}
