/** Fairy Tapes · shared UI — ScrewView without a window: hit area, dragging, end of the gesture, cancel (no drawing). */
#include "fairy_ui/screw_view.h"

#include "vstgui/lib/cbuttonstate.h"

#include <cmath>
#include <cstdio>

using namespace FairyTapes::Ui;
using namespace VSTGUI;

namespace {
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

ScrewSpec spec(bool missing = false) {
  ScrewSpec s;
  s.x = 100;
  s.y = 50;
  s.cross = true;
  s.missing = missing;
  s.initialAngle = 30;
  return s;
}

void test_view_box_and_hit_area() {
  auto* v = new ScrewView(spec(), 400);
  CHECK(std::fabs(v->angle() - 40) < 1e-9);   // normalised
  CHECK(v->getViewSize().getCenter() == CPoint(100, 50));
  CHECK(v->takesClickAt(CPoint(100, 50)));
  CHECK(v->takesClickAt(CPoint(100 + kScrewHitRadius, 50)));
  CHECK(!v->takesClickAt(CPoint(111, 61)));   // inside the box, outside the screw: the click goes to the views below
  CButtonState left(kLButton);
  CPoint corner(111, 61);
  CHECK(v->onMouseDown(corner, left) == kMouseEventNotHandled);
  CPoint centre(100, 50);
  CButtonState right(kRButton);
  CHECK(v->onMouseDown(centre, right) == kMouseEventNotHandled);
  v->forget();
}

void test_missing_screw_cannot_be_turned() {
  auto* v = new ScrewView(spec(true), 0);
  CHECK(!v->takesClickAt(CPoint(100, 50)));
  CPoint centre(100, 50);
  CButtonState left(kLButton);
  CHECK(v->onMouseDown(centre, left) == kMouseEventNotHandled);
  v->forget();
}

void test_drag_turns_and_reports_the_end_once() {
  auto* v = new ScrewView(spec(), 10);
  int turning = 0, ended = 0;
  double last = -1;
  v->onTurning = [&](double a) { ++turning; last = a; };
  v->onTurned = [&](double a) { ++ended; last = a; };
  CButtonState left(kLButton);
  CPoint p(100, 50);
  CHECK(v->onMouseDown(p, left) == kMouseEventHandled && v->dragging());
  CPoint q(110, 50);
  CHECK(v->onMouseMoved(q, left) == kMouseEventHandled);
  CHECK(std::fabs(v->angle() - 26) < 1e-9 && turning == 1 && ended == 0 && std::fabs(last - 26) < 1e-9);
  CPoint r(110, 40);   // and up: further clockwise
  v->onMouseMoved(r, left);
  CHECK(std::fabs(v->angle() - 42) < 1e-9 && turning == 2 && ended == 0);
  CHECK(v->onMouseUp(r, left) == kMouseEventHandled);
  CHECK(!v->dragging() && ended == 1 && std::fabs(last - 42) < 1e-9);
  CHECK(v->onMouseUp(r, left) == kMouseEventNotHandled && ended == 1);
  CPoint far(300, 300);   // no drag: moves are ignored
  CHECK(v->onMouseMoved(far, left) == kMouseEventNotHandled && std::fabs(v->angle() - 42) < 1e-9);
  v->forget();
}

void test_click_without_moving_does_not_report_a_turn() {
  auto* v = new ScrewView(spec(), 10);
  int ended = 0;
  v->onTurned = [&](double) { ++ended; };
  CButtonState left(kLButton);
  CPoint p(100, 50);
  v->onMouseDown(p, left);
  v->onMouseUp(p, left);
  CHECK(ended == 0 && v->angle() == 10);
  v->forget();
}

void test_cancel_puts_the_screw_back() {
  auto* v = new ScrewView(spec(), 10);
  int ended = 0;
  v->onTurned = [&](double) { ++ended; };
  CButtonState left(kLButton);
  CPoint p(100, 50), q(130, 50);
  v->onMouseDown(p, left);
  v->onMouseMoved(q, left);
  CHECK(v->angle() != 10);
  CHECK(v->onMouseCancel() == kMouseEventHandled);
  CHECK(!v->dragging() && v->angle() == 10 && ended == 0);
  v->forget();
}

void test_set_screw_and_angle() {
  auto* v = new ScrewView(spec(), 10);
  ScrewSpec s = spec();
  s.x = 20;
  s.y = 30;
  s.cross = false;
  v->setScrew(s, -90);
  CHECK(v->getViewSize().getCenter() == CPoint(20, 30) && !v->spec().cross && v->angle() == 270);
  v->setAngle(std::nan(""));
  CHECK(v->angle() == 0);
  v->forget();
}
}  // namespace

int main() {
  test_view_box_and_hit_area();
  test_missing_screw_cannot_be_turned();
  test_drag_turns_and_reports_the_end_once();
  test_click_without_moving_does_not_report_a_turn();
  test_cancel_puts_the_screw_back();
  test_set_screw_and_angle();
  if (g_failed) { std::fprintf(stderr, "%d check(s) failed\n", g_failed); return 1; }
  std::puts("fairy_ui_screw_view_tests: ok");
  return 0;
}
