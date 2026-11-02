/**
 * Fairy Tapes · Pillow Talk — tests for the editor layout table (ui/layout.h): pure data, no VSTGUI.
 * Build target: pillow_talk_layout_tests.
 */
#include "ui/layout.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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

bool inside(const Rect& in, const Rect& out) {
  return in.x >= out.x && in.y >= out.y && in.x + in.w <= out.x + out.w && in.y + in.h <= out.y + out.h;
}
bool overlap(const Rect& a, const Rect& b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
const Rect kPanel{0, 0, static_cast<float>(kEditorWidth), static_cast<float>(kEditorHeight)};

// Everything the editor places in a box: control views, the scope, meter bars and clip LEDs.
struct Placed {
  Box box;
  Rect rect;
  std::string what;
};
std::vector<Placed> placedItems() {
  std::vector<Placed> out;
  for (const ControlSpec& c : kControls) out.push_back({c.box, viewRect(c), c.name ? c.name : "choice"});
  out.push_back({Box::Ducking, kScopeRect, "scope"});
  for (const MeterRow& m : kMeterRows) out.push_back({Box::Level, meterBarRect(m), "meter"});
  for (int i = 0; i < kClipCount; ++i) out.push_back({Box::Level, clipRect(kClips[i]), kClips[i].label});
  return out;
}

void test_editor_is_1000_by_470() {
  ASSERT_TRUE(kEditorWidth == 1000 && kEditorHeight == 470);   // lowered on 2026-10-21 (pillow_rack.html)
}

void test_every_persistent_param_has_exactly_one_control() {
  for (Steinberg::Vst::ParamID id = 0; id < kNumParams; ++id) {
    int n = 0;
    for (const ControlSpec& c : kControls) n += c.id == id ? 1 : 0;
    ASSERT_TRUE(n == 1);
  }
  ASSERT_TRUE(sizeof(kControls) / sizeof(kControls[0]) == kNumParams);
  ASSERT_TRUE(controlOf(kMix) != nullptr && controlOf(kMix)->id == kMix);
  ASSERT_TRUE(controlOf(static_cast<Steinberg::Vst::ParamID>(kNumParams)) == nullptr);
}

void test_control_kinds_follow_the_parameters() {
  for (const ControlSpec& c : kControls) {
    const bool onOff = c.id == kBypass || c.id == kTimeSync || c.id == kHoldSync || c.id == kSidechain;
    const bool notes = c.id == kTimeNote || c.id == kHoldNote;
    ASSERT_TRUE(c.kind == (onOff ? Kind::Toggle : notes ? Kind::Choice : Kind::Knob));
    if (c.kind == Kind::Choice) {
      ASSERT_TRUE(c.choiceCount == 4);
      ASSERT_TRUE(!std::strcmp(c.choices[0], "1/16") && !std::strcmp(c.choices[3], "1/2"));
    }
    if (c.kind == Kind::Knob) ASSERT_TRUE(c.name && c.lo && c.hi && c.capRadius > 0);
    else if (c.kind == Kind::Toggle) ASSERT_TRUE(c.name && *c.name);
  }
}

// docs/DESIGN_SYSTEM.md (three button types): on/off buttons (Bypass, Sync, Sidechain) red; the note values are exclusive
// choices with the name on the face (no LED); knobs none.
void test_led_colours_follow_the_rule() {
  for (const ControlSpec& c : kControls) {
    if (c.kind == Kind::Toggle) ASSERT_TRUE(c.led == Led::Red);
    else ASSERT_TRUE(c.led == Led::None);
  }
}

void test_boxes_inside_panel_and_disjoint() {
  const int n = static_cast<int>(sizeof(kBoxes) / sizeof(kBoxes[0]));
  ASSERT_TRUE(n == 5);
  for (int i = 0; i < n; ++i) {
    ASSERT_TRUE(inside(kBoxes[i].rect, kPanel));
    ASSERT_TRUE(kBoxes[i].title && *kBoxes[i].title);
    for (int j = i + 1; j < n; ++j) ASSERT_TRUE(!overlap(kBoxes[i].rect, kBoxes[j].rect));
  }
  ASSERT_TRUE(boxOf(Box::Ducking).rect.x == 24 && boxOf(Box::Ducking).rect.y == 92);
}

void test_every_item_inside_its_box_and_the_panel() {
  for (const Placed& p : placedItems()) {
    if (!inside(p.rect, kPanel) || !inside(p.rect, boxOf(p.box).rect))
      std::fprintf(stderr, "  outside: %s\n", p.what.c_str());
    ASSERT_TRUE(inside(p.rect, kPanel));
    ASSERT_TRUE(inside(p.rect, boxOf(p.box).rect));
  }
}

void test_no_overlap_within_a_box() {
  const auto items = placedItems();
  for (size_t i = 0; i < items.size(); ++i)
    for (size_t j = i + 1; j < items.size(); ++j) {
      if (items[i].box != items[j].box) continue;
      if (i < kNumParams && j < kNumParams && sharesPlace(kControls[i].id, kControls[j].id)) continue;   // shown in turn
      if (overlap(items[i].rect, items[j].rect))
        std::fprintf(stderr, "  overlap: %s / %s\n", items[i].what.c_str(), items[j].what.c_str());
      ASSERT_TRUE(!overlap(items[i].rect, items[j].rect));
    }
}

void test_preview_positions() {
  ASSERT_TRUE(kScopeRect.x == 44 && kScopeRect.y == 108 && kScopeRect.w == 376 && kScopeRect.h == 130);
  // Bypass top right of OUTPUT, Mix in the middle of it.
  const ControlSpec* by = controlOf(kBypass);
  ASSERT_TRUE(by->box == Box::Output && by->rect.x + by->rect.w / 2 == 940 && by->rect.y + by->rect.h / 2 == 130);
  const ControlSpec* mix = controlOf(kMix);
  ASSERT_TRUE(mix->box == Box::Output && knobCentreX(*mix) == 891 && knobCentreY(*mix) == 176 && mix->capRadius == 22);
  ASSERT_TRUE(controlOf(kSidechain)->box == Box::Ducking);
  ASSERT_TRUE(controlOf(kTimeNote)->box == Box::Delay && controlOf(kHoldNote)->box == Box::AutoDuck);
  // DELAY and AUTO DUCK on one row (2026-10-21); SYNC in the top right corner of each block
  ASSERT_TRUE(boxOf(Box::Delay).rect.y == 282 && boxOf(Box::AutoDuck).rect.y == 282 && boxOf(Box::AutoDuck).rect.x == 550);
  ASSERT_TRUE(knobCentreY(*controlOf(kThreshold)) == 352 && knobCentreY(*controlOf(kTimeMs)) == 352);
  ASSERT_TRUE(knobCentreX(*controlOf(kTimeMs)) == 84 && knobCentreX(*controlOf(kWowFlutter)) == 446);
  ASSERT_TRUE(knobCentreX(*controlOf(kHoldMs)) == 796 && knobCentreX(*controlOf(kRelease)) == 886);
  const ControlSpec* ts = controlOf(kTimeSync);
  const ControlSpec* hs = controlOf(kHoldSync);
  ASSERT_TRUE(ts->rect.x + ts->rect.w / 2 == 515 && ts->rect.y + ts->rect.h / 2 == 320);
  ASSERT_TRUE(hs->rect.x + hs->rect.w / 2 == 947 && hs->rect.y + hs->rect.h / 2 == 320);
  // the note values take the place of the knob: the first face centred at (cx - 20, cy - 16), the last at (cx + 20, cy + 10)
  const ControlSpec* tn = controlOf(kTimeNote);
  ASSERT_TRUE(tn->rect.x == 44 && tn->rect.w == 80 && tn->rect.y == 323 && tn->rect.h == 52 && tn->choiceCount == 4);
  ASSERT_TRUE(controlOf(kHoldNote)->rect.x == 756);
}

void test_meters_and_clips_use_the_display_params() {
  int in = 0, out = 0, duck = 0;
  for (const MeterRow& m : kMeterRows) {
    if (m.id == kDispInL || m.id == kDispInR) ++in;
    else if (m.id == kDispOutL || m.id == kDispOutR) ++out;
    else if (m.id == kDispSend) { ++duck; ASSERT_TRUE(m.gainReduction && !m.alwaysLit); }
    if (m.id != kDispSend) ASSERT_TRUE(!m.gainReduction && m.alwaysLit);
  }
  ASSERT_TRUE(in == 2 && out == 2 && duck == 1);
  ASSERT_TRUE(kClipCount == 2 && kClips[0].id == kDispClipIn && kClips[1].id == kDispClipOut);
}

void test_level_meters_use_the_0_dB_scale() {
  // The processor publishes the peaks over -60..0 dB (processor.cpp, meterNormFromDb(x, -60, 0)); the LED colours and the
  // fall of the held peak use the same scale (2026-11-02): green below -18, lime to -9, yellow to 0, red at 0 dB.
  ASSERT_TRUE(kMeterScale.minDb == -60.0 && kMeterScale.maxDb == 0.0);
  using FairyTapes::Ui::LedColor;
  using FairyTapes::Ui::MeterKind;
  using FairyTapes::Ui::ledColor;
  const LedColor want[kMeterLeds] = {LedColor::Green, LedColor::Green, LedColor::Green,  LedColor::Green,
                                     LedColor::Green, LedColor::Green, LedColor::Green,  LedColor::Green,
                                     LedColor::Lime,  LedColor::Lime,  LedColor::Yellow, LedColor::Red};
  for (int i = 0; i < kMeterLeds; ++i) ASSERT_TRUE(ledColor(i, kMeterLeds, MeterKind::Level, kMeterScale) == want[i]);
  // the printed "0" sits over the last LED, the red one
  ASSERT_TRUE(scaleX(0) == kMeterFirstX + kMeterPitch * (kMeterLeds - 1));
}

void test_duck_meter_is_send_reduction() {
  ASSERT_TRUE(duckMeterNorm(1.0) == 0.0);
  ASSERT_TRUE(duckMeterNorm(0.0) == 1.0);
  ASSERT_TRUE(std::fabs(duckMeterNorm(0.25) - 0.75) < 1e-12);
  ASSERT_TRUE(duckMeterNorm(std::nan("")) == 0.0);  // unknown send: nothing ducked
  ASSERT_TRUE(duckMeterNorm(-3.0) == 1.0 && duckMeterNorm(7.0) == 0.0);
}

void test_dimming_rules() {
  // SYNC swaps the knob and the note values (one hidden, the other shown); nothing is dimmed by it
  const DimState off{false, false, false};
  ASSERT_TRUE(!controlHidden(kTimeMs, off) && controlHidden(kTimeNote, off));
  ASSERT_TRUE(!controlHidden(kHoldMs, off) && controlHidden(kHoldNote, off));
  ASSERT_TRUE(!controlHidden(kTimeSync, off) && !controlHidden(kFeedback, off));
  for (const ControlSpec& c : kControls) ASSERT_TRUE(!controlDimmed(c.id, off));
  const DimState sync{false, true, true};
  ASSERT_TRUE(controlHidden(kTimeMs, sync) && !controlHidden(kTimeNote, sync));
  ASSERT_TRUE(controlHidden(kHoldMs, sync) && !controlHidden(kHoldNote, sync));
  ASSERT_TRUE(!controlDimmed(kTimeNote, sync) && !controlDimmed(kTimeMs, sync));
  const DimState timeOnly{false, true, false};
  ASSERT_TRUE(controlHidden(kTimeMs, timeOnly) && !controlHidden(kHoldMs, timeOnly));
  ASSERT_TRUE(sharesPlace(kTimeMs, kTimeNote) && sharesPlace(kHoldNote, kHoldMs) && !sharesPlace(kTimeMs, kHoldNote));
  // The global bypass dims everything but itself.
  const DimState bypass{true, false, false};
  for (const ControlSpec& c : kControls) ASSERT_TRUE(controlDimmed(c.id, bypass) == (c.id != kBypass));
}

void test_silkscreen_items_from_the_table() {
  using FairyTapes::Ui::PrintKind;
  const auto items = silkscreenItems();
  ASSERT_TRUE(items.size() == kNumParams);   // no print for the two note grids, one name each for the TIME and HOLD place
  int labels = 0;
  for (const auto& it : items) {
    if (it.kind == PrintKind::Label) {   // TIME / HOLD: printed whichever control holds the place
      ++labels;
      const ControlSpec* k = controlOf(static_cast<Steinberg::Vst::ParamID>(it.id - kPlaceLabelKey));
      ASSERT_TRUE(k && (k->id == kTimeMs || k->id == kHoldMs) && it.name == k->name && it.cx == knobCentreX(*k));
      ASSERT_TRUE(it.cy == knobCentreY(*k) + k->capRadius + 24);   // the knob name's baseline
      continue;
    }
    const ControlSpec* c = controlOf(static_cast<Steinberg::Vst::ParamID>(it.id));
    ASSERT_TRUE(c != nullptr && c->kind != Kind::Choice);
    if (c->kind == Kind::Knob) {
      ASSERT_TRUE(it.kind == PrintKind::Knob && it.cx == knobCentreX(*c) && it.cy == knobCentreY(*c));
      ASSERT_TRUE(it.capRadius == c->capRadius && it.lo == c->lo && it.hi == c->hi);
      ASSERT_TRUE(it.name == ((c->id == kTimeMs || c->id == kHoldMs) ? std::string() : std::string(c->name)));
    } else {
      ASSERT_TRUE(it.kind == PrintKind::Button && it.name == c->name);
      ASSERT_TRUE(it.cx == c->rect.x + c->rect.w / 2 && it.cy == c->rect.y + c->rect.h / 2);
    }
  }
  ASSERT_TRUE(labels == 2);
}

void test_panel_texts_and_rules_inside_panel() {
  for (const PanelText& t : kPanelTexts) {
    ASSERT_TRUE(t.text && *t.text);
    ASSERT_TRUE(t.x > 0 && t.x < kEditorWidth && t.y > 0 && t.y < kEditorHeight);
  }
}

void test_tape_slots_and_screws_inside_panel() {
  ASSERT_TRUE(kNumTapeSlots == 6);
  for (const TapeSlot& s : kTapeSlots) ASSERT_TRUE(s.cx > 0 && s.cx < kEditorWidth && s.cy > 0 && s.cy < kEditorHeight);
  for (const auto& s : kScrewPos) ASSERT_TRUE(s[0] > 0 && s[0] < kEditorWidth && s[1] > 0 && s[1] < kEditorHeight);
}

} // namespace

int main() {
  RUN_TEST(test_editor_is_1000_by_470);
  RUN_TEST(test_every_persistent_param_has_exactly_one_control);
  RUN_TEST(test_control_kinds_follow_the_parameters);
  RUN_TEST(test_led_colours_follow_the_rule);
  RUN_TEST(test_boxes_inside_panel_and_disjoint);
  RUN_TEST(test_every_item_inside_its_box_and_the_panel);
  RUN_TEST(test_no_overlap_within_a_box);
  RUN_TEST(test_preview_positions);
  RUN_TEST(test_meters_and_clips_use_the_display_params);
  RUN_TEST(test_level_meters_use_the_0_dB_scale);
  RUN_TEST(test_duck_meter_is_send_reduction);
  RUN_TEST(test_dimming_rules);
  RUN_TEST(test_silkscreen_items_from_the_table);
  RUN_TEST(test_panel_texts_and_rules_inside_panel);
  RUN_TEST(test_tape_slots_and_screws_inside_panel);
  std::printf("pillow_talk_layout_tests: %d passed, %d failed\n", g_passed, g_failed);
  return g_failed ? 1 : 0;
}
