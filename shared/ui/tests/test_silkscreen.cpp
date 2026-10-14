/** Fairy Tapes · shared UI — silkscreen layout and state (pure, no VSTGUI). */
#include "fairy_ui/silkscreen.h"

#include <cmath>
#include <cstdio>

using namespace FairyTapes::Ui;

namespace {
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)
bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

const PrintText* find(const PrintedParts& p, TextRole role) {
  for (const auto& t : p.texts)
    if (t.role == role) return &t;
  return nullptr;
}

PrintedItem knob() {
  PrintedItem k;
  k.id = 7;
  k.kind = PrintKind::Knob;
  k.cx = 100;
  k.cy = 200;
  k.capRadius = 23;
  k.name = "TIME";
  k.lo = "0";
  k.hi = "10";
  return k;
}

void test_knob_prints_scale_ends_name_and_value() {
  const PrintedParts p = printedParts(knob(), "1.2 s");
  CHECK(p.dots.size() == static_cast<std::size_t>(Rack::kScaleDots));
  for (int d = 0; d < Rack::kScaleDots; ++d) {
    const Rack::Pt q = Rack::scaleDot(100, 200, 23, d);
    CHECK(near(p.dots[d].x, q.x) && near(p.dots[d].y, q.y) && p.dots[d].r == Rack::scaleDotRadius(d));
  }
  CHECK(p.texts.size() == 4);
  const PrintText* lo = nullptr;
  const PrintText* hi = nullptr;
  for (const auto& t : p.texts)
    if (t.role == TextRole::EndValue) (t.text == "0" ? lo : hi) = &t;
  CHECK(lo && hi);
  if (lo && hi) {
    // the low end value ends left of the scale, the high one starts right of it, on the same baseline
    CHECK(lo->align == TextAlign::Right && hi->align == TextAlign::Left && near(lo->y, hi->y));
    CHECK(near(lo->x, Rack::endLabelLow(100, 200, 23).x) && near(hi->x, Rack::endLabelHigh(100, 200, 23).x));
    CHECK(lo->x < p.dots.front().x && hi->x > p.dots.back().x);
  }
  const PrintText* name = find(p, TextRole::Name);
  const PrintText* value = find(p, TextRole::Value);
  CHECK(name && name->text == "TIME" && name->align == TextAlign::Centre && near(name->x, 100) &&
        near(name->y, Rack::nameBaseline(200, 23)));
  CHECK(value && value->text == "1.2 s" && near(value->y, Rack::valueBaseline(200, 23)));
  if (name && value) CHECK(value->y > name->y);   // the value sits under the name
}

void test_knob_without_ends_value_or_name() {
  PrintedItem k = knob();
  k.lo.clear();
  k.hi.clear();
  CHECK(printedParts(k, "").texts.size() == 1);                // name only, an empty value is not printed
  k.showValue = false;
  CHECK(find(printedParts(k, "5"), TextRole::Value) == nullptr);   // the value is not shown for this knob
  k.name.clear();
  CHECK(printedParts(k, "5").texts.empty());
  CHECK(printedParts(k, "5").dots.size() == static_cast<std::size_t>(Rack::kScaleDots));   // the scale is always printed
}

void test_button_label_above_the_face() {
  PrintedItem b;
  b.kind = PrintKind::Button;
  b.cx = 50;
  b.cy = 60;
  b.name = "SYNC";
  const PrintedParts p = printedParts(b, "ignored");
  CHECK(p.dots.empty() && p.texts.size() == 1);
  CHECK(p.texts[0].text == "SYNC" && p.texts[0].role == TextRole::Label && near(p.texts[0].x, 50) &&
        near(p.texts[0].y, 60 + Rack::kLabelOffsetY) && p.texts[0].y < 60);
  b.name.clear();   // e.g. a button with an icon
  CHECK(printedParts(b, "").texts.empty());
}

void test_choice_labels_over_equal_cells() {
  PrintedItem c;
  c.kind = PrintKind::Choice;
  c.cx = 300;
  c.cy = 100;
  c.width = 120;
  c.labels = {"1/16", "1/8", "1/4"};
  const PrintedParts p = printedParts(c, "");
  CHECK(p.texts.size() == 3);
  if (p.texts.size() == 3) {
    CHECK(near(p.texts[0].x, 260) && near(p.texts[1].x, 300) && near(p.texts[2].x, 340));
    for (const auto& t : p.texts) CHECK(near(t.y, 100 + Rack::kLabelOffsetY) && t.align == TextAlign::Centre);
  }
  c.labels = {};
  CHECK(printedParts(c, "").texts.empty());
}

void test_aligned_left() {
  CHECK(alignedLeft(100, 20, TextAlign::Left) == 100);
  CHECK(alignedLeft(100, 20, TextAlign::Right) == 80);
  CHECK(alignedLeft(100, 20, TextAlign::Centre) == 90);
}

void test_model_state_by_id_and_changes() {
  PrintedItem a = knob(), b = knob(), c = knob();
  b.cx = 300;   // a second print of the same control (shares id 7)
  c.id = 9;
  SilkscreenModel m({a, b, c}, {{1, 40, 302}, {2, 418, 474}});
  CHECK(m.size() == 3);
  CHECK(m.setValueText(7, "3 dB"));
  CHECK(!m.setValueText(7, "3 dB"));   // unchanged: no redraw
  CHECK(m.value(0) == "3 dB" && m.value(1) == "3 dB" && m.value(2).empty());
  CHECK(!m.setValueText(42, "x"));     // unknown id
  CHECK(m.setDimmed(9, true) && m.dimmed(2) && !m.dimmed(0));
  CHECK(!m.setDimmed(9, true));
  CHECK(m.setHidden(7, true) && m.hidden(0) && m.hidden(1) && !m.hidden(2));
  CHECK(m.setHidden(7, false) && !m.hidden(0));
  CHECK(find(m.parts(0), TextRole::Value) && find(m.parts(0), TextRole::Value)->text == "3 dB");

  CHECK(m.tags().size() == 2 && m.tags()[0].text.empty());
  CHECK(m.setTag(2, "NAM") && !m.setTag(2, "NAM"));
  CHECK(m.tags()[1].text == "NAM" && m.tags()[1].slot.x == 418 && m.tags()[1].slot.y == 474);
  CHECK(m.setTagDimmed(1, true) && m.tags()[0].dimmed && !m.setTagDimmed(1, true));
  CHECK(!m.setTag(5, "x") && !m.setTagDimmed(5, true));   // no such block
}
}  // namespace

int main() {
  test_knob_prints_scale_ends_name_and_value();
  test_knob_without_ends_value_or_name();
  test_button_label_above_the_face();
  test_choice_labels_over_equal_cells();
  test_aligned_left();
  test_model_state_by_id_and_changes();
  if (g_failed) { std::fprintf(stderr, "%d check(s) failed\n", g_failed); return 1; }
  std::puts("fairy_ui_silkscreen_tests: ok");
  return 0;
}
