/**
 * Fairy Tapes · Happy Ending — tests for the pure editor layout (ui/editor_size.h, ui/layout.h): no VSTGUI.
 * Build target: happy_ending_ui_tests.
 */
#include "ui/decor.h"
#include "ui/editor_size.h"
#include "ui/layout.h"
#include "ui/prefs.h"
#include "ui/readouts.h"
#include "ui/screen_model.h"
#include "ui/screw_angles.h"
#include "ui/tapes.h"
#include "ui/value_text.h"

#include "dsp/clip_softly.h"
#include "dsp/final_stage.h"
#include "dsp/fine_eq.h"
#include "dsp/vibe_eq.h"
#include "engine.h"
#include "loudness_macro.h"
#include "params.h"
#include "tuning.h"

#include "fairy_ui/screws.h"
#include "fairy_ui/tape_paper.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <string>
#include <system_error>
#include <vector>

using namespace FairyTapes::HappyEnding;
using namespace FairyTapes::HappyEnding::Ui;

namespace {

int g_failed = 0;
int g_passed = 0;

#define ASSERT_TRUE(expr) do { \
  if (!(expr)) { \
    std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); \
    ++g_failed; return; \
  } \
} while(0)

#define RUN_TEST(fn) do { \
  const int before = g_failed; \
  fn(); \
  if (g_failed == before) { ++g_passed; std::printf("PASS %s\n", #fn); } \
  else std::printf("FAIL %s\n", #fn); \
} while(0)

bool inside(const Rect& in, const Rect& out) {
  return in.x >= out.x && in.y >= out.y && in.x + in.w <= out.x + out.w && in.y + in.h <= out.y + out.h;
}
bool overlap(const Rect& a, const Rect& b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
const Rect kPanel{0, 0, static_cast<float>(kEditorWidth), static_cast<float>(kEditorHeight)};

bool isFinalModeControl(Steinberg::Vst::ParamID id) {
  return id == kFinalEffect || id == kFinalCurve || id == kFinalDrive;
}

// Everything on the panel that a control must not cover (and that a tape must keep clear of).
std::vector<Rect> fixedReadouts() {
  std::vector<Rect> out{kScreenRect, kLufsScreenRect};
  for (const MeterRow& r : kMeterRows) out.push_back(meterBarRect(r));
  for (const Ui::GrRow& r : kGrRows) out.push_back(grBarRect(r));
  for (const Ui::ClipLed& c : kClipLeds) out.push_back(clipLedRect(c));
  for (const Led& l : kActLeds) out.push_back({l.cx - l.r, l.cy - l.r, 2 * l.r, 2 * l.r});
  out.push_back({kTargetLed.cx - kTargetLed.r, kTargetLed.cy - kTargetLed.r, 2 * kTargetLed.r, 2 * kTargetLed.r});
  return out;
}

UiState allLive() {
  UiState s;
  s.bypass = false;
  for (bool& b : s.stageOn) b = true;
  for (bool& b : s.bandOn) b = true;
  s.listen = -1;
  s.set = false;
  s.finalMode = 0;
  return s;
}

bool dimmedAndLocked(Steinberg::Vst::ParamID id, const UiState& s, int choice = -1) {
  const ControlSpec* c = controlOf(id, choice);
  return c != nullptr && controlDimmed(*c, s) && controlLocked(*c, s);
}
bool live(Steinberg::Vst::ParamID id, const UiState& s, int choice = -1) {
  const ControlSpec* c = controlOf(id, choice);
  return c != nullptr && !controlDimmed(*c, s) && !controlLocked(*c, s);
}

// ---------------------------------------------------------------------------------------------------------------------

void test_editor_size() {
  ASSERT_TRUE(kEditorWidth == 1280 && kEditorHeight == 890);
  const PixelSize a = scaledEditorSize(1.0, FairyTapes::Ui::EditorSize::S100);
  ASSERT_TRUE(a.width == 1280 && a.height == 890);
  const PixelSize b = scaledEditorSize(2.0, FairyTapes::Ui::EditorSize::S75);
  ASSERT_TRUE(b.width == 1920 && b.height == 1335);
  const PixelSize c = scaledEditorSize(std::numeric_limits<double>::quiet_NaN(), FairyTapes::Ui::EditorSize::S125);
  ASSERT_TRUE(c.width == 1600 && c.height == 1113);
  // the screws sit in the corners of this panel (shared rule, Task 1)
  const auto sp = FairyTapes::Ui::screwPositions(1280, 890);
  for (int i = 0; i < 4; ++i) {
    ASSERT_TRUE(kScrewPos[i][0] == static_cast<float>(sp[static_cast<std::size_t>(i)].x));
    ASSERT_TRUE(kScrewPos[i][1] == static_cast<float>(sp[static_cast<std::size_t>(i)].y));
  }
  ASSERT_TRUE(kScrewPos[3][0] == 1262.f && kScrewPos[3][1] == 872.f);
}

void test_layout_binds_every_param_once() {
  for (Steinberg::Vst::ParamID id = 0; id < kNumParams; ++id) {
    int n = 0;
    for (const ControlSpec& c : kControls)
      if (c.id == id) ++n;
    if (id == kOversampling) {
      ASSERT_TRUE(n == 0);   // on the back (BEHAVIOUR), not on the front
    } else {
      if (n != 1) std::fprintf(stderr, "  param %u bound %d times\n", static_cast<unsigned>(id), n);
      ASSERT_TRUE(n == 1);
    }
  }
  int listen = 0, set = 0;
  bool choiceSeen[3] = {false, false, false};
  for (const ControlSpec& c : kControls) {
    ASSERT_TRUE(c.id < kNumParams || c.id == kListenBand || c.id == kSetActive);   // no display ID, nothing else
    ASSERT_TRUE(c.id < kDisplayBase);
    if (c.id == kListenBand) {
      ++listen;
      ASSERT_TRUE(c.choiceIndex >= 0 && c.choiceIndex <= 2);
      choiceSeen[c.choiceIndex] = true;
    } else {
      ASSERT_TRUE(c.choiceIndex == -1);
    }
    if (c.id == kSetActive) ++set;
  }
  ASSERT_TRUE(listen == 3 && set == 1);
  ASSERT_TRUE(choiceSeen[0] && choiceSeen[1] && choiceSeen[2]);
  // lookup
  ASSERT_TRUE(controlOf(kOversampling) == nullptr);
  ASSERT_TRUE(controlOf(kGlueMix) != nullptr && controlOf(kGlueMix)->id == kGlueMix);
  ASSERT_TRUE(controlOf(kListenBand, 1) != nullptr && controlOf(kListenBand, 1)->choiceIndex == 1);
  ASSERT_TRUE(controlOf(kListenBand, 3) == nullptr);
  ASSERT_TRUE(controlOf(kDispInL) == nullptr);
  // each LISTEN sits in the button row of its bell, right of the bell's M/S
  for (int k = 0; k < 3; ++k) {
    const ControlSpec* l = controlOf(kListenBand, k);
    const ControlSpec* ms = controlOf(static_cast<Steinberg::Vst::ParamID>(kLmMs + 5 * k));
    ASSERT_TRUE(l != nullptr && ms != nullptr && l->box == Box::FineEq);
    ASSERT_TRUE(std::fabs(faceCentre(*l).y - faceCentre(*ms).y) < 0.5f);
    ASSERT_TRUE(std::fabs(faceCentre(*l).x - faceCentre(*ms).x - 34.f) < 0.5f);
  }
  // the parameters that need a list of names for their faces have one, of the right size
  ASSERT_TRUE(litChoiceOf(kLimitStyle) != nullptr && litChoiceOf(kLimitStyle)->count == kParams[kLimitStyle].steps);
  ASSERT_TRUE(litChoiceOf(kFinalMode) != nullptr && litChoiceOf(kFinalMode)->count == kParams[kFinalMode].steps);
  ASSERT_TRUE(litChoiceOf(kGlueMix) == nullptr);
  for (const ControlSpec& c : kControls)
    if (c.kind == Kind::LitChoice) ASSERT_TRUE(litChoiceOf(c.id) != nullptr);
}

void test_layout_rects_inside() {
  constexpr int n = static_cast<int>(sizeof(kBoxes) / sizeof(kBoxes[0]));
  ASSERT_TRUE(n == 12);
  for (int i = 0; i < n; ++i) {
    ASSERT_TRUE(inside(kBoxes[i].rect, kPanel));
    for (int j = i + 1; j < n; ++j) ASSERT_TRUE(!overlap(kBoxes[i].rect, kBoxes[j].rect));
  }
  // anchors from the preview (final coordinates: group shifts applied)
  ASSERT_TRUE(boxOf(Box::FineEq).rect.y == 302.f && boxOf(Box::VibeEq).rect.y == 586.f && boxOf(Box::Clip).rect.y == 758.f);
  ASSERT_TRUE(boxOf(Box::Input).rect.y == 92.f && boxOf(Box::Screen).rect.x == 782.f);
  for (const ControlSpec& c : kControls) {
    if (!inside(viewRect(c), boxOf(c.box).rect))
      std::fprintf(stderr, "  control %u (%s) outside its box\n", static_cast<unsigned>(c.id), c.name ? c.name : "-");
    ASSERT_TRUE(inside(viewRect(c), boxOf(c.box).rect));
  }
  for (const ActionSpec& a : kActions) ASSERT_TRUE(inside(viewRect(a), boxOf(a.box).rect));
  ASSERT_TRUE(inside(kScreenRect, boxOf(Box::Screen).rect));
  ASSERT_TRUE(inside(kLufsScreenRect, boxOf(Box::Level).rect));
  for (const MeterRow& r : kMeterRows) ASSERT_TRUE(inside(meterBarRect(r), boxOf(Box::Level).rect));
  for (const Ui::GrRow& r : kGrRows) ASSERT_TRUE(inside(grBarRect(r), boxOf(Box::Level).rect));
  for (const Ui::ClipLed& c : kClipLeds) ASSERT_TRUE(inside(clipLedRect(c), boxOf(Box::Level).rect));
  ASSERT_TRUE(inside(kLufsResetRect, kLufsScreenRect));
  for (const Led& l : kActLeds) ASSERT_TRUE(inside({l.cx - l.r, l.cy - l.r, 2 * l.r, 2 * l.r}, boxOf(Box::VibeEq).rect));
}

void test_layout_no_control_overlap() {
  std::vector<std::pair<Rect, const ControlSpec*>> rects;
  for (const ControlSpec& c : kControls) rects.push_back({viewRect(c), &c});
  for (const ActionSpec& a : kActions) rects.push_back({viewRect(a), nullptr});
  for (std::size_t i = 0; i < rects.size(); ++i)
    for (std::size_t j = i + 1; j < rects.size(); ++j) {
      const ControlSpec* a = rects[i].second;
      const ControlSpec* b = rects[j].second;
      // EFFECT / CURVE (INFLATE) and DRIVE (CLIP) of FINAL are never visible together
      if (a && b && isFinalModeControl(a->id) && isFinalModeControl(b->id) && (a->id == kFinalDrive) != (b->id == kFinalDrive))
        continue;
      if (overlap(rects[i].first, rects[j].first))
        std::fprintf(stderr, "  overlap: %s / %s\n", a && a->name ? a->name : "?", b && b->name ? b->name : "?");
      ASSERT_TRUE(!overlap(rects[i].first, rects[j].first));
    }
  for (const auto& r : rects)
    for (const Rect& f : fixedReadouts()) ASSERT_TRUE(!overlap(r.first, f));
  // the exception is real: the FINAL knobs share the place
  ASSERT_TRUE(overlap(viewRect(*controlOf(kFinalDrive)), viewRect(*controlOf(kFinalEffect))));
}

void test_in_buttons_top_right() {
  const Box boxes[7] = {Box::FineEq, Box::VibeEq, Box::Glue, Box::Clip, Box::Limit, Box::Final, Box::Metal};
  const Steinberg::Vst::ParamID ins[7] = {kFineOn, kVibeOn, kGlueOn, kClipOn, kLimitOn, kFinalOn, kMetalOn};
  for (int i = 0; i < 7; ++i) {
    const ControlSpec* c = controlOf(ins[i]);
    ASSERT_TRUE(c != nullptr && c->kind == Kind::Toggle && c->box == boxes[i]);
    ASSERT_TRUE(std::strcmp(c->name, "IN") == 0);
    const Rect& r = boxOf(boxes[i]).rect;
    const Point p = faceCentre(*c);
    ASSERT_TRUE(std::fabs(p.x - (r.x + r.w - 29.f)) <= 2.f);
    ASSERT_TRUE(std::fabs(p.y - (r.y + 38.f)) <= 2.f);
    ASSERT_TRUE(stageInParam(stageOf(boxes[i])) == ins[i]);
  }
  // BYPASS and MATCH follow the same corner in OUTPUT and LOUDNESS
  const Rect& out = boxOf(Box::Output).rect;
  ASSERT_TRUE(std::fabs(faceCentre(*controlOf(kBypass)).x - (out.x + out.w - 29.f)) <= 2.f);
  const ActionSpec& m = actionOf(Action::Match);
  const Rect& ld = boxOf(Box::Loudness).rect;
  ASSERT_TRUE(m.box == Box::Loudness && std::fabs(m.rect.x + m.rect.w / 2 - (ld.x + ld.w - 29.f)) <= 2.f);
}

void test_titles_bottom_and_tags() {
  for (const BoxSpec& b : kBoxes) {
    if (b.box == Box::Screen) {
      ASSERT_TRUE(b.title == nullptr);   // the screen box has no title
      continue;
    }
    ASSERT_TRUE(b.title != nullptr && b.title[0] != '\0');
    for (const char* p = b.title; *p; ++p) ASSERT_TRUE(!std::islower(static_cast<unsigned char>(*p)));
    const Point t = titleAnchor(b);
    ASSERT_TRUE(t.y == b.rect.y + b.rect.h + 5.f);     // on the bottom border
    ASSERT_TRUE(t.x == b.rect.x + b.rect.w / 2);
  }
  ASSERT_TRUE(std::strcmp(engineTag(Box::FineEq, 0), "MULTI-Q") == 0);
  ASSERT_TRUE(std::strcmp(engineTag(Box::VibeEq, 0), "TUBE-1A") == 0);
  ASSERT_TRUE(std::strcmp(engineTag(Box::Glue, 1), "4K-G") == 0);
  ASSERT_TRUE(std::strcmp(engineTag(Box::Clip, 0), "C-SOFTLY") == 0);
  ASSERT_TRUE(std::strcmp(engineTag(Box::Final, 0), "INFL-Z") == 0);
  ASSERT_TRUE(std::strcmp(engineTag(Box::Final, 1), "C-ONLY2") == 0);
  for (Box b : {Box::Limit, Box::Metal, Box::Input, Box::Output, Box::Level, Box::Loudness, Box::Screen})
    for (int mode : {0, 1}) ASSERT_TRUE(engineTag(b, mode) == nullptr);
  // the tags sit at the top left of their box (x + 16, y + 22), as in the preview
  const Point ft = tagPosition(Box::FineEq);
  ASSERT_TRUE(ft.x == 40.f && ft.y == 324.f);
  const Point fn = tagPosition(Box::Final);
  ASSERT_TRUE(fn.x == 657.f && fn.y == 780.f);
}

void test_dimming_rules() {
  UiState s = allLive();
  for (const ControlSpec& c : kControls)
    ASSERT_TRUE(!controlDimmed(c, s) && (!controlLocked(c, s) || c.id == kFinalDrive));   // DRIVE: hidden in INFLATE
  for (const ActionSpec& a : kActions) ASSERT_TRUE(!actionDimmed(a, s));

  // BYPASS: everything but itself (actions included); the meters and the LUFS screen stay live
  s.bypass = true;
  for (const ControlSpec& c : kControls) {
    if (c.id == kBypass) ASSERT_TRUE(!controlDimmed(c, s) && !controlLocked(c, s));
    else ASSERT_TRUE(controlDimmed(c, s) && controlLocked(c, s));
  }
  for (const ActionSpec& a : kActions) ASSERT_TRUE(actionDimmed(a, s));
  ASSERT_TRUE(!clipLedDimmed(0, s) && !clipLedDimmed(8, s));   // IN and OUT stay lit
  for (int i = 1; i < 8; ++i) ASSERT_TRUE(clipLedDimmed(i, s));
  for (int k = 0; k < 5; ++k) ASSERT_TRUE(grRowDimmed(k, s));

  // a stage IN off: the stage dims and locks, but not its IN
  s = allLive();
  s.stageOn[static_cast<int>(Stage::Glue)] = false;
  ASSERT_TRUE(dimmedAndLocked(kGlueThresh, s) && dimmedAndLocked(kGlueRatio, s) && dimmedAndLocked(kGlueWarm, s));
  ASSERT_TRUE(live(kGlueOn, s) && live(kTilt, s) && live(kClipDrive, s));
  ASSERT_TRUE(clipLedDimmed(3, s) && !clipLedDimmed(2, s) && grRowDimmed(0, s) && !grRowDimmed(1, s));

  // FINE EQ off: the band INs dim too, the stage IN does not
  s = allLive();
  s.stageOn[static_cast<int>(Stage::Fine)] = false;
  ASSERT_TRUE(dimmedAndLocked(kLsOn, s) && dimmedAndLocked(kMidFreq, s) && dimmedAndLocked(kListenBand, s, 1));
  ASSERT_TRUE(live(kFineOn, s));

  // a band off: its controls dim and lock, its IN stays live; the other bands are untouched
  s = allLive();
  s.bandOn[2] = false;   // LO MID
  ASSERT_TRUE(dimmedAndLocked(kLmFreq, s) && dimmedAndLocked(kLmMs, s) && dimmedAndLocked(kListenBand, s, 0));
  ASSERT_TRUE(live(kLmOn, s) && live(kMidFreq, s) && live(kListenBand, s, 1));

  // LISTEN on MID: the other bands dim and lock, their IN included; MID and the rest of the panel stay live
  s = allLive();
  s.listen = 1;
  ASSERT_TRUE(dimmedAndLocked(kLmFreq, s) && dimmedAndLocked(kLmOn, s) && dimmedAndLocked(kHpOn, s));
  ASSERT_TRUE(dimmedAndLocked(kHsGain, s) && dimmedAndLocked(kListenBand, s, 0) && dimmedAndLocked(kListenBand, s, 2));
  ASSERT_TRUE(live(kMidFreq, s) && live(kMidOn, s) && live(kMidQ, s) && live(kListenBand, s, 1));
  ASSERT_TRUE(live(kFineOn, s) && live(kGlueThresh, s));
  // LISTEN counts as off while its bell is off (as in the engine): nothing else dims
  s.bandOn[3] = false;
  ASSERT_TRUE(live(kLmFreq, s) && live(kHpOn, s));

  // SET: LIMIT dims and locks except RELEASE and SET
  s = allLive();
  s.set = true;
  ASSERT_TRUE(dimmedAndLocked(kLimitThresh, s) && dimmedAndLocked(kLimitStyle, s) && dimmedAndLocked(kLimitOn, s));
  ASSERT_TRUE(live(kLimitRelease, s) && live(kSetActive, s) && live(kGlueThresh, s) && live(kFinalEffect, s));
  // SET with LIMIT off: LIMIT's IN stays live (no dead end), SET dims with its stage
  s.stageOn[static_cast<int>(Stage::Limit)] = false;
  ASSERT_TRUE(live(kLimitOn, s) && dimmedAndLocked(kSetActive, s) && dimmedAndLocked(kLimitRelease, s));

  // METALHEAD off: the box dims except its IN
  s = allLive();
  s.stageOn[static_cast<int>(Stage::Metal)] = false;
  ASSERT_TRUE(dimmedAndLocked(kPush, s) && live(kMetalOn, s));
  ASSERT_TRUE(grRowDimmed(4, s) && clipLedDimmed(7, s));
  // the ACT LEDs follow VIBE EQ
  s = allLive();
  ASSERT_TRUE(!actLedDimmed(s));
  s.stageOn[static_cast<int>(Stage::Vibe)] = false;
  ASSERT_TRUE(actLedDimmed(s));

  // uiStateFrom reads the parameters with the plug-in's index conversion
  double norm[kNumParams];
  loadDefaults(norm);
  UiState d = uiStateFrom(norm, -1, false);
  ASSERT_TRUE(!d.bypass && d.listen == -1 && !d.set && d.finalMode == 0);
  for (int i = 0; i < 6; ++i) ASSERT_TRUE(d.stageOn[i]);
  ASSERT_TRUE(!d.stageOn[6]);   // METALHEAD off by default
  for (bool b : d.bandOn) ASSERT_TRUE(b);
  norm[kBypass] = 0.6;          // index 1 of a switch
  norm[kFinalMode] = 0.51;      // index 1
  norm[kMidOn] = 0.4;           // index 0: off
  d = uiStateFrom(norm, 2, true);
  ASSERT_TRUE(d.bypass && d.finalMode == 1 && !d.bandOn[3] && d.listen == 2 && d.set);
  ASSERT_TRUE(uiStateFrom(norm, 7, false).listen == -1 && uiStateFrom(norm, -3, false).listen == -1);
}

void test_final_mode_visibility() {
  UiState s = allLive();
  s.finalMode = 0;   // INFLATE: EFFECT and CURVE
  ASSERT_TRUE(controlVisible(*controlOf(kFinalEffect), s) && controlVisible(*controlOf(kFinalCurve), s));
  ASSERT_TRUE(!controlVisible(*controlOf(kFinalDrive), s) && controlLocked(*controlOf(kFinalDrive), s));
  ASSERT_TRUE(!controlLocked(*controlOf(kFinalEffect), s));
  s.finalMode = 1;   // CLIP: DRIVE
  ASSERT_TRUE(!controlVisible(*controlOf(kFinalEffect), s) && !controlVisible(*controlOf(kFinalCurve), s));
  ASSERT_TRUE(controlVisible(*controlOf(kFinalDrive), s) && !controlLocked(*controlOf(kFinalDrive), s));
  ASSERT_TRUE(controlLocked(*controlOf(kFinalCurve), s));
  for (int mode : {0, 1}) {
    s.finalMode = mode;
    for (const ControlSpec& c : kControls)
      if (!isFinalModeControl(c.id)) ASSERT_TRUE(controlVisible(c, s));
  }
  // the three FINAL knobs belong to FINAL and dim with it
  s.stageOn[static_cast<int>(Stage::Final)] = false;
  ASSERT_TRUE(controlDimmed(*controlOf(kFinalDrive), s) && !controlDimmed(*controlOf(kFinalOn), s));
}

bool hasSourceName(const std::string& text) {
  std::string t = text;
  std::transform(t.begin(), t.end(), t.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  for (const char* w : {"pultec", "ssl", "airwindows", "dusk"})
    if (t.find(w) != std::string::npos) return true;
  return false;
}

void test_silkscreen_items_cover_controls() {
  using FairyTapes::Ui::PrintedItem;
  using FairyTapes::Ui::PrintKind;
  const std::vector<PrintedItem> items = silkscreenItems();
  std::size_t expected = 0;
  for (const ControlSpec& c : kControls) {
    if (c.kind == Kind::LitChoice || c.kind == Kind::HeadToggle) continue;   // the names are on the faces
    ++expected;
    int found = 0;
    for (const PrintedItem& it : items) {
      if (it.id != printId(c)) continue;
      const Point p = c.kind == Kind::Knob ? knobCentre(c) : faceCentre(c);
      if (std::fabs(it.cx - p.x) > 0.01 || std::fabs(it.cy - p.y) > 0.01) continue;
      ++found;
      if (c.kind == Kind::Knob) {
        ASSERT_TRUE(it.kind == PrintKind::Knob && it.capRadius == c.capRadius);
        ASSERT_TRUE(it.lo == c.lo && it.hi == c.hi && !it.lo.empty() && !it.hi.empty());
      } else if (c.kind == Kind::Cycle) {
        ASSERT_TRUE(it.kind == PrintKind::Cycle);
      } else {
        ASSERT_TRUE(it.kind == PrintKind::Button);
      }
      ASSERT_TRUE(!it.name.empty() && it.name == c.name);
    }
    if (found != 1) std::fprintf(stderr, "  control %u printed %d times\n", static_cast<unsigned>(c.id), found);
    ASSERT_TRUE(found == 1);
  }
  // MATCH is an action with its label printed above the face; the view buttons carry their names on the face
  int match = 0;
  for (const PrintedItem& it : items)
    if (it.id == actionPrintId(Action::Match) && it.kind == PrintKind::Button && it.name == "MATCH") ++match;
  ASSERT_TRUE(match == 1);
  // HI PASS FREQ is printed a second time with the SIDE scale (the editor shows one of the two, as the M/S says)
  const ControlSpec* hp = controlOf(kHpFreq);
  int side = 0;
  for (const PrintedItem& it : items)
    if (it.id == kHpSidePrintId) {
      ++side;
      ASSERT_TRUE(it.kind == PrintKind::Knob && it.cx == knobCentre(*hp).x && it.cy == knobCentre(*hp).y);
      ASSERT_TRUE(it.name == hp->name && it.lo == hp->lo && it.hi == kHpSideScaleHi && it.capRadius == hp->capRadius);
    }
  ASSERT_TRUE(side == 1 && std::string(kHpSideScaleHi) == "200");
  ASSERT_TRUE(items.size() == expected + 2);
  // every bell's LISTEN has its own key, so each one dims alone; no key is shared with another printed item
  std::set<int> keys;
  for (int k = 0; k < 3; ++k) keys.insert(printId(*controlOf(kListenBand, k)));
  ASSERT_TRUE(keys.size() == 3 && !keys.count(static_cast<int>(kListenBand)));
  std::set<int> all;
  for (const PrintedItem& it : items) ASSERT_TRUE(all.insert(it.id).second);

  // no source names anywhere in the front panel text (only the block tags)
  for (const PrintedItem& it : items) ASSERT_TRUE(!hasSourceName(it.name) && !hasSourceName(it.lo) && !hasSourceName(it.hi));
  for (const ControlSpec& c : kControls)
    for (const char* t : {c.name, c.lo, c.hi})
      if (t) ASSERT_TRUE(!hasSourceName(t));
  for (const PanelText& t : kPanelTexts) ASSERT_TRUE(!hasSourceName(t.text));
  for (const BoxSpec& b : kBoxes)
    if (b.title) ASSERT_TRUE(!hasSourceName(b.title));
  for (const ActionSpec& a : kActions) ASSERT_TRUE(!hasSourceName(a.name));
  for (const LitChoiceSpec& l : kLitChoices)
    for (int i = 0; i < l.count; ++i) ASSERT_TRUE(!hasSourceName(l.names[i]));
  for (const Ui::ClipLed& c : kClipLeds) ASSERT_TRUE(!hasSourceName(c.label));
  for (Box b : {Box::FineEq, Box::VibeEq, Box::Glue, Box::Clip, Box::Final})
    for (int mode : {0, 1}) ASSERT_TRUE(!hasSourceName(engineTag(b, mode)));
  // and the texts the spec asks for are there
  bool shame = false, level = false, matched = false;
  for (const PanelText& t : kPanelTexts) {
    if (std::strcmp(t.text, "SHAME ON YOU") == 0) shame = t.stage == Stage::Metal;
    if (std::strcmp(t.text, "level") == 0) level = t.stage == Stage::Clip;
    if (std::strcmp(t.text, "matched") == 0) matched = t.stage == Stage::Clip;
  }
  ASSERT_TRUE(shame && level && matched);
}

void test_tape_slots_clear() {
  // A tape is up to 96 x 26 and turned up to 9 degrees: with its torn ends and shadow it reaches 54 px either side of
  // its centre in x and 25 px in y (as in Deep Throat). Every position a slot allows keeps clear of the controls, the
  // screens, the meters, the LEDs and the screws.
  std::vector<Rect> keepClear = fixedReadouts();
  for (const ControlSpec& c : kControls) keepClear.push_back(viewRect(c));
  for (const ActionSpec& a : kActions) keepClear.push_back(viewRect(a));
  for (const auto& s : kScrewPos) keepClear.push_back({s[0] - 12, s[1] - 12, 24, 24});
  ASSERT_TRUE(kNumTapeSlots == 6);
  for (const TapeSlot& t : kTapeSlots) {
    const Rect reach{t.cx - 54 - t.jx, t.cy - 25 - t.jy, 2 * (54 + t.jx), 2 * (25 + t.jy)};
    ASSERT_TRUE(inside(reach, kPanel));
    for (const Rect& r : keepClear) ASSERT_TRUE(!overlap(reach, r));
  }
}

// ---- Value texts (ui/value_text.h) ------------------------------------------------------------------------------------
// A full table of normalized values with HI PASS M/S at `hpMsIndex` (every other parameter at its default).
void norms(double* all, int hpMsIndex) {
  loadDefaults(all);
  all[kHpMs] = normFromIndex(kHpMs, hpMsIndex);
}

void test_value_text_golden() {
  // The texts under the controls: units, signs and the named positions (the controller prints exactly these).
  double all[kNumParams];
  norms(all, 0);
  const auto t = [&](ParamID id, int index) { return formatValue(id, normFromIndex(id, index), all); };
  ASSERT_TRUE(t(kInputGain, 0) == "-12.0 dB" && t(kInputGain, 24) == "+0.0 dB");
  ASSERT_TRUE(t(kCeiling, 1) == "-0.3 dB" && t(kTarget, 3) == "-7 LUFS");
  ASSERT_TRUE(t(kBypass, 1) == "On" && t(kHmOn, 0) == "Off");
  ASSERT_TRUE(t(kLmMs, 1) == "Mid" && t(kLmMs, 2) == "Side" && t(kLmQ, 3) == "Surgical");
  ASSERT_TRUE(t(kMidFreq, 4) == "1 kHz" && t(kLmFreq, 4) == "250 Hz" && t(kHsFreq, 6) == "20 kHz");
  ASSERT_TRUE(t(kGlueRelease, 4) == "Auto" && t(kGlueRelease, 1) == "0.3 s" && t(kGlueRatio, 2) == "4:1");
  ASSERT_TRUE(t(kGlueMix, 10) == "50 %" && t(kGlueThresh, 0) == "-20 dB" && t(kGlueAttack, 0) == "0.1 ms");
  ASSERT_TRUE(t(kFinalMode, 0) == "Inflate" && t(kFinalMode, 1) == "Clip");
  ASSERT_TRUE(t(kLimitStyle, 2) == "Dyn" && t(kLimitRelease, 4) == releaseText(kParams[kLimitStyle].defaultIndex, 4) &&
              t(kLimitRelease, 0) == releaseText(kParams[kLimitStyle].defaultIndex, 0));   // ms of the style (2026-11-02)
  ASSERT_TRUE(t(kLoudness, 7) == "3.5" && t(kTilt, 0) == "-3.0 dB" && t(kTube, 2) == "2");
  ASSERT_TRUE(t(kOversampling, 0) == "Off" && t(kOversampling, 2) == "4x");
  ASSERT_TRUE(t(kGlueScHpf, 0) == "Off" && t(kGlueScHpf, 3) == "150 Hz");
  // an out-of-range norm is clamped, an unknown id gives nothing
  ASSERT_TRUE(formatValue(kInputGain, 7.0, all) == "+12.0 dB" && formatValue(kInputGain, -1.0, all) == "-12.0 dB");
  ASSERT_TRUE(formatValue(kNumParams, 0.0, all).empty());
}

void test_value_text_hp_side_scale() {
  // HI PASS FREQ reads the scale of its own M/S: SIDE has the mono-bass scale (the ledger item of engine Task 13).
  double st[kNumParams], side[kNumParams];
  norms(st, 0);
  norms(side, 2);
  ASSERT_TRUE(formatValue(kHpFreq, 1.0, st) == "40 Hz");
  ASSERT_TRUE(formatValue(kHpFreq, 1.0, side) == "200 Hz");
  ASSERT_TRUE(formatValue(kHpFreq, 0.0, side) == "Off" && formatValue(kHpFreq, normFromIndex(kHpFreq, 1), side) == "60 Hz");
  // no table of norms: the normal scale
  ASSERT_TRUE(formatValue(kHpFreq, 1.0, nullptr) == "40 Hz");
  // typing "60" on SIDE picks the 60 Hz step (index 1); on ST the nearest of 20..40 is the last step
  double v = -1.0;
  ASSERT_TRUE(parseValue(kHpFreq, "60", side, v) && indexFromNorm(kHpFreq, v) == 1);
  ASSERT_TRUE(parseValue(kHpFreq, "60", st, v) && indexFromNorm(kHpFreq, v) == 5);
  ASSERT_TRUE(parseValue(kHpFreq, "160", side, v) && indexFromNorm(kHpFreq, v) == 4);
  ASSERT_TRUE(parseValue(kHpFreq, "80 Hz", side, v) && indexFromNorm(kHpFreq, v) == 2);
  ASSERT_TRUE(parseValue(kHpFreq, "35", st, v) && indexFromNorm(kHpFreq, v) == 4);
  // the named text is read on the scale in use too: "200 hz" exists only on SIDE
  ASSERT_TRUE(parseValue(kHpFreq, "200 hz", side, v) && indexFromNorm(kHpFreq, v) == 5);
  ASSERT_TRUE(parseValue(kHpFreq, "Off", side, v) && indexFromNorm(kHpFreq, v) == 0);
}

void test_value_text_parse_roundtrip() {
  // parse(format(v)) == v for every step of every parameter (HI PASS on both its scales)
  for (int ms = 0; ms < 3; ++ms) {
    double all[kNumParams];
    norms(all, ms);
    for (int i = 0; i < static_cast<int>(kNumParams); ++i) {
      const auto id = static_cast<ParamID>(i);
      for (int s = 0; s < kParams[id].steps; ++s) {
        const double v = normFromIndex(id, s);
        const std::string text = formatValue(id, v, all);
        ASSERT_TRUE(!text.empty());
        double back = -1.0;
        ASSERT_TRUE(parseValue(id, text, all, back));
        ASSERT_TRUE(back == v);
      }
    }
  }
  double all[kNumParams];
  norms(all, 0);
  double v = -1.0;
  // case and spaces are ignored; a bare number picks the nearest step
  ASSERT_TRUE(parseValue(kLmQ, "  SURGICAL ", all, v) && indexFromNorm(kLmQ, v) == 3);
  // M/S: the silkscreen sigla and the host text are both read
  ASSERT_TRUE(parseValue(kMidMs, "side", all, v) && indexFromNorm(kMidMs, v) == 2);
  ASSERT_TRUE(parseValue(kMidMs, "SIDE", all, v) && indexFromNorm(kMidMs, v) == 2);
  ASSERT_TRUE(parseValue(kLsMs, " st ", all, v) && indexFromNorm(kLsMs, v) == 0);
  ASSERT_TRUE(parseValue(kLsMs, "Stereo", all, v) && indexFromNorm(kLsMs, v) == 0);
  ASSERT_TRUE(parseValue(kHsMs, "Mid", all, v) && indexFromNorm(kHsMs, v) == 1);
  ASSERT_TRUE(parseValue(kHsMs, "MID", all, v) && indexFromNorm(kHsMs, v) == 1);
  ASSERT_TRUE(parseValue(kInputGain, "-3", all, v) && indexFromNorm(kInputGain, v) == 18);
  ASSERT_TRUE(parseValue(kInputGain, "+3.0 dB", all, v) && indexFromNorm(kInputGain, v) == 30);
  ASSERT_TRUE(parseValue(kInputGain, "99", all, v) && indexFromNorm(kInputGain, v) == 48);
  // text that is no value gives false and leaves the result alone
  v = 0.25;
  ASSERT_TRUE(!parseValue(kLmQ, "bogus", all, v) && v == 0.25);
  ASSERT_TRUE(!parseValue(kInputGain, "", all, v) && !parseValue(kInputGain, "dB", all, v));
  ASSERT_TRUE(!parseValue(kInputGain, "nan", all, v) && !parseValue(kInputGain, "inf", all, v));
  ASSERT_TRUE(!parseValue(kBypass, "3", all, v) && !parseValue(kLmMs, "7", all, v));   // no value table: names only
  ASSERT_TRUE(!parseValue(kNumParams, "1", all, v) && v == 0.25);
}

// ---- Readouts (ui/readouts.h) -----------------------------------------------------------------------------------------
void test_lufs_readouts() {
  const std::string MINUS = "\xE2\x88\x92", DASH = "\xE2\x80\x94";
  ASSERT_TRUE(MINUS == kMinusSign && DASH == kEmDash && MINUS.size() == 3 && DASH.size() == 3);
  const double none = -std::numeric_limits<double>::infinity();
  ASSERT_TRUE(lufsText(none) == DASH && lufsText(std::nan("")) == DASH);
  ASSERT_TRUE(lufsText(-14.04) == MINUS+"14.0" && lufsText(-14.2) == MINUS+"14.2");
  ASSERT_TRUE(lufsText(-9.96) == MINUS+"10.0" && lufsText(-0.04) == "0.0" && lufsText(0.0) == "0.0");
  // TO TARGET: signed gap, one digit; the target is the TARGET choice (-14, -11, -9, -7)
  ASSERT_TRUE(toTargetText(none, 0) == DASH && toTargetText(std::nan(""), 0) == DASH);
  ASSERT_TRUE(toTargetText(-13.6, 0) == "+0.4" && toTargetText(-14.3, 0) == MINUS+"0.3");
  ASSERT_TRUE(toTargetText(-14.0, 0) == "0.0" && toTargetText(-14.02, 0) == "0.0");
  ASSERT_TRUE(toTargetText(-11.0, 1) == "0.0" && toTargetText(-12.0, 2) == MINUS+"3.0" && toTargetText(-6.0, 3) == "+1.0");
  // the LED is green within 0.5 LU of the target (the limit included), dark without a measurement
  ASSERT_TRUE(targetLedOn(-13.6, 0) && !targetLedOn(-13.4, 0));
  ASSERT_TRUE(targetLedOn(-14.4, 0) && !targetLedOn(-14.6, 0));
  ASSERT_TRUE(targetLedOn(-13.5, 0) && targetLedOn(-14.5, 0));
  ASSERT_TRUE(targetLedOn(-9.1, 2) && !targetLedOn(-9.6, 2) && targetLedOn(-7.0, 3));
  ASSERT_TRUE(!targetLedOn(none, 0) && !targetLedOn(std::nan(""), 0));
  // an index outside the choices is clamped, never read out of the table
  ASSERT_TRUE(toTargetText(-14.0, -3) == "0.0" && toTargetText(-7.0, 99) == "0.0");
}

void test_release_text_follows_tuning() {
  // real milliseconds: the base of the style times 2^((index - 4) / 3), as the limiter computes it
  ASSERT_TRUE(releaseText(0, 4) == "50 ms" && releaseText(1, 4) == "100 ms" && releaseText(2, 4) == "250 ms");
  ASSERT_TRUE(releaseText(0, 7) == "100 ms" && releaseText(0, 1) == "25 ms" && releaseText(1, 7) == "200 ms" && releaseText(2, 1) == "125 ms");
  // the bases come from tuning.h: a copy with another base gives another text, the other styles stay
  double bases[5];
  for (int i = 0; i < 5; ++i) bases[i] = Tuning::kLimitReleaseBaseMs[i];
  ASSERT_TRUE(Ui::detail::releaseTextFrom(bases, 5, 0, 4) == releaseText(0, 4));
  bases[0] = 80.0;
  ASSERT_TRUE(Ui::detail::releaseTextFrom(bases, 5, 0, 4) == "80 ms");
  ASSERT_TRUE(Ui::detail::releaseTextFrom(bases, 5, 0, 4) != releaseText(0, 4));
  ASSERT_TRUE(Ui::detail::releaseTextFrom(bases, 5, 1, 4) == releaseText(1, 4));
  // style and index out of range are clamped
  ASSERT_TRUE(releaseText(-1, 4) == releaseText(0, 4) && releaseText(99, 4) == releaseText(4, 4));
  ASSERT_TRUE(releaseText(0, -5) == releaseText(0, 0) && releaseText(0, 50) == releaseText(0, 8));
}

// ---- Screen model: EQ curves, transfer curves, scrolling history, paths --------------------------------------------

using Norm = std::vector<double>;

Norm defaultsNorm() {
  Norm n(kNumParams);
  loadDefaults(n.data());
  return n;
}
void setIdx(Norm& n, ParamID id, int index) { n[id] = normFromIndex(id, index); }

// Three different FINE EQ + VIBE EQ settings: stereo bands, bands on MID / SIDE, and an extreme VIBE.
Norm eqSettingA() {
  Norm n = defaultsNorm();
  setIdx(n, kHpFreq, 3);
  setIdx(n, kLsOn, 1); setIdx(n, kLsFreq, 2); setIdx(n, kLsGain, 14);
  setIdx(n, kLmOn, 1); setIdx(n, kLmFreq, 4); setIdx(n, kLmGain, 3); setIdx(n, kLmQ, 2);
  setIdx(n, kMidOn, 1); setIdx(n, kMidFreq, 5); setIdx(n, kMidGain, 16); setIdx(n, kMidQ, 1);
  setIdx(n, kHmOn, 1); setIdx(n, kHmFreq, 7); setIdx(n, kHmGain, 2); setIdx(n, kHmQ, 3);
  setIdx(n, kHsOn, 1); setIdx(n, kHsFreq, 4); setIdx(n, kHsGain, 12);
  setIdx(n, kVibeOn, 1); setIdx(n, kLowFreq, 2); setIdx(n, kLow, 7); setIdx(n, kAirFreq, 2); setIdx(n, kAir, 6);
  setIdx(n, kTilt, 9);
  return n;
}
Norm eqSettingB() {
  Norm n = defaultsNorm();
  setIdx(n, kLmOn, 1); setIdx(n, kLmMs, 1); setIdx(n, kLmFreq, 3); setIdx(n, kLmGain, 15); setIdx(n, kLmQ, 1);
  setIdx(n, kMidOn, 1); setIdx(n, kMidMs, 2); setIdx(n, kMidFreq, 6); setIdx(n, kMidGain, 1); setIdx(n, kMidQ, 2);
  setIdx(n, kHmOn, 1); setIdx(n, kHmMs, 1); setIdx(n, kHmFreq, 8); setIdx(n, kHmGain, 13); setIdx(n, kHmQ, 0);
  setIdx(n, kHpMs, 2); setIdx(n, kHpFreq, 4);
  setIdx(n, kVibeOn, 1); setIdx(n, kLowFreq, 3); setIdx(n, kLow, 10); setIdx(n, kAirFreq, 4); setIdx(n, kAir, 3);
  setIdx(n, kTilt, 2);
  return n;
}
Norm eqSettingC() {
  Norm n = defaultsNorm();
  setIdx(n, kFineOn, 1);
  setIdx(n, kLsOn, 1); setIdx(n, kLsMs, 0); setIdx(n, kLsFreq, 5); setIdx(n, kLsGain, 0);
  setIdx(n, kHsOn, 1); setIdx(n, kHsFreq, 6); setIdx(n, kHsGain, 16);
  setIdx(n, kVibeOn, 1); setIdx(n, kLowFreq, 0); setIdx(n, kLow, 4); setIdx(n, kAirFreq, 0); setIdx(n, kAir, 10);
  setIdx(n, kTilt, 12);
  return n;
}

// The reference: the DSP blocks as the engine sets them from the same settings, at 48 kHz.
struct EngineRef {
  DSP::FineEq fine;
  DSP::VibeEq vibe;
  explicit EngineRef(const Norm& n, int listen = -1) {
    const EngineSettings s = mapParams(n.data(), -1, false);
    fine.prepare(48000.0, 512);
    fine.setStageOn(s.fineOn);
    for (int b = 0; b < DSP::FineEq::kNumBands; ++b) fine.setBand(b, s.bands[static_cast<std::size_t>(b)]);
    fine.setListen(listen);
    vibe.prepare(48000.0, 512);
    vibe.set(s.vibe);
  }
};

double hzAt(int i, int n) { return 20.0 * std::pow(1000.0, static_cast<double>(i) / (n - 1)); }

void test_eq_curves_match_engine() {
  const Norm settings[3] = {eqSettingA(), eqSettingB(), eqSettingC()};
  for (const Norm& n : settings) {
    EqCurves curves;
    curves.update(n.data());
    EngineRef ref(n);
    double fineSpan = 0.0, vibeSpan = 0.0;
    for (int i = 0; i < 200; ++i) {
      const double hz = hzAt(i, 200);
      for (int t = 0; t < 3; ++t) {
        const double want = ref.fine.magnitudeDb(t, hz);
        ASSERT_TRUE(std::fabs(curves.fineDb(t, hz) - want) < 0.01);
        fineSpan = std::max(fineSpan, std::fabs(want));
      }
      const double vWant = ref.vibe.magnitudeDb(hz, true);
      ASSERT_TRUE(std::fabs(curves.vibeStaticDb(hz) - vWant) < 0.01);
      ASSERT_TRUE(std::fabs(curves.vibeDb(hz, 0.0f, 0.0f) - vWant) < 0.01);
      vibeSpan = std::max(vibeSpan, std::fabs(vWant));
    }
    ASSERT_TRUE(fineSpan > 1.0 && vibeSpan > 1.0);  // the settings are not flat: the comparison means something
  }
  // update() replaces the previous settings (it does not accumulate)
  EqCurves curves;
  const Norm a = eqSettingA(), c = eqSettingC();
  curves.update(a.data());
  curves.update(c.data());
  EngineRef refC(c);
  for (int i = 0; i < 50; ++i) {
    const double hz = hzAt(i, 50);
    ASSERT_TRUE(std::fabs(curves.fineDb(0, hz) - refC.fine.magnitudeDb(0, hz)) < 0.01);
    ASSERT_TRUE(std::fabs(curves.vibeStaticDb(hz) - refC.vibe.magnitudeDb(hz, true)) < 0.01);
  }
  // stage off: the signal passes untouched, so the curve is flat (the stage IN, as the preview)
  Norm off = eqSettingA();
  setIdx(off, kFineOn, 0);
  setIdx(off, kVibeOn, 0);
  curves.update(off.data());
  for (int i = 0; i < 20; ++i) {
    ASSERT_TRUE(curves.fineDb(0, hzAt(i, 20)) == 0.0);
    ASSERT_TRUE(curves.vibeStaticDb(hzAt(i, 20)) == 0.0);
  }
  ASSERT_TRUE(!curves.msActive());
}

void test_eq_curves_ms() {
  EqCurves curves;
  const Norm flat = defaultsNorm();
  curves.update(flat.data());
  ASSERT_TRUE(!curves.msActive());

  // all bands on ST: one curve (MID and SIDE are the same as ST)
  const Norm st = eqSettingA();
  curves.update(st.data());
  ASSERT_TRUE(!curves.msActive());
  for (int i = 0; i < 50; ++i) {
    const double hz = hzAt(i, 50);
    ASSERT_TRUE(curves.fineDb(1, hz) == curves.fineDb(0, hz));
    ASSERT_TRUE(curves.fineDb(2, hz) == curves.fineDb(0, hz));
  }

  // one band on MID: msActive, MID differs from SIDE (and from ST at that band's frequency)
  Norm oneMid = defaultsNorm();
  setIdx(oneMid, kMidOn, 1); setIdx(oneMid, kMidMs, 1); setIdx(oneMid, kMidFreq, 4); setIdx(oneMid, kMidGain, 14);
  curves.update(oneMid.data());
  ASSERT_TRUE(curves.msActive());
  double maxDiff = 0.0;
  for (int i = 0; i < 100; ++i) {
    const double hz = hzAt(i, 100);
    maxDiff = std::max(maxDiff, std::fabs(curves.fineDb(1, hz) - curves.fineDb(2, hz)));
    ASSERT_TRUE(std::fabs(curves.fineDb(2, hz) - curves.fineDb(0, hz)) < 1e-9);  // SIDE and ST: the MID band is not in them
  }
  ASSERT_TRUE(maxDiff > 1.0);

  // a band on SIDE: MID stays as ST, SIDE moves
  Norm oneSide = defaultsNorm();
  setIdx(oneSide, kHmOn, 1); setIdx(oneSide, kHmMs, 2); setIdx(oneSide, kHmFreq, 6); setIdx(oneSide, kHmGain, 2);
  curves.update(oneSide.data());
  ASSERT_TRUE(curves.msActive());
  ASSERT_TRUE(std::fabs(curves.fineDb(2, 5000.0) - curves.fineDb(1, 5000.0)) > 1.0);

  // a band on MID that is switched off does not count as M/S
  Norm offMid = oneMid;
  setIdx(offMid, kMidOn, 0);
  curves.update(offMid.data());
  ASSERT_TRUE(!curves.msActive());
  // ... nor does the whole stage off
  Norm stageOff = oneMid;
  setIdx(stageOff, kFineOn, 0);
  curves.update(stageOff.data());
  ASSERT_TRUE(!curves.msActive());
}

void test_vibe_pull_back() {
  Norm n = defaultsNorm();
  setIdx(n, kVibeOn, 1); setIdx(n, kLowFreq, 2); setIdx(n, kLow, 10); setIdx(n, kAirFreq, 2); setIdx(n, kAir, 10);
  setIdx(n, kTilt, 6);
  EqCurves curves;
  curves.update(n.data());

  // pull 0 is the static setting
  for (int i = 0; i < 60; ++i) {
    const double hz = hzAt(i, 60);
    ASSERT_TRUE(curves.vibeDb(hz, 0.0f, 0.0f) == curves.vibeStaticDb(hz));
  }
  const double boostStatic = curves.vibeStaticDb(60.0);
  const double airStatic = curves.vibeStaticDb(10000.0);
  ASSERT_TRUE(boostStatic > 5.0 && airStatic > 5.0);

  // pull 1: the boost is reduced by the share of tuning.h: LOW / AIR amount * (1 - kDynMaxPullback), the same
  // network as the stepped setting with that amount
  const double keep = 10.0 * (1.0 - Tuning::kDynMaxPullback);
  ASSERT_TRUE(keep > 0.0 && keep < 10.0);
  const double boostFull = curves.vibeDb(60.0, 1.0f, 1.0f);
  const double airFull = curves.vibeDb(10000.0, 1.0f, 1.0f);
  ASSERT_TRUE(boostFull < boostStatic - 1.0 && airFull < airStatic - 1.0);
  ASSERT_TRUE(boostFull > 0.0 && airFull > 0.0);  // never pulled all the way to nothing (the share stays below 1)
  if (std::fabs(keep - std::round(keep)) < 1e-9) {  // the kept amount is a step of the knob: compare with that step
    Norm kept = n;
    setIdx(kept, kLow, static_cast<int>(std::lround(keep)));
    setIdx(kept, kAir, static_cast<int>(std::lround(keep)));
    EqCurves keptCurves;
    keptCurves.update(kept.data());
    for (int i = 0; i < 60; ++i) {
      const double hz = hzAt(i, 60);
      ASSERT_TRUE(std::fabs(curves.vibeDb(hz, 1.0f, 1.0f) - keptCurves.vibeStaticDb(hz)) < 1e-6);
    }
  }
  // halfway pull: halfway amount, between the two
  const double boostHalf = curves.vibeDb(60.0, 0.5f, 0.5f);
  ASSERT_TRUE(boostHalf < boostStatic && boostHalf > boostFull);
  // LOW and AIR are pulled independently
  ASSERT_TRUE(std::fabs(curves.vibeDb(60.0, 1.0f, 0.0f) - boostFull) < 0.5);
  ASSERT_TRUE(std::fabs(curves.vibeDb(10000.0, 1.0f, 0.0f) - airStatic) < 0.5);
  ASSERT_TRUE(std::fabs(curves.vibeDb(10000.0, 0.0f, 1.0f) - airFull) < 0.5);
  // out-of-range and non-finite pulls are clamped to 0..1
  ASSERT_TRUE(curves.vibeDb(60.0, 5.0f, 5.0f) == curves.vibeDb(60.0, 1.0f, 1.0f));
  ASSERT_TRUE(curves.vibeDb(60.0, -3.0f, -3.0f) == curves.vibeStaticDb(60.0));
  ASSERT_TRUE(std::isfinite(curves.vibeDb(60.0, std::numeric_limits<float>::quiet_NaN(), 0.0f)));

  // the DSP overload itself: pull 0 / 0 is the static response of the existing function
  EngineRef ref(n);
  for (int i = 0; i < 60; ++i) {
    const double hz = hzAt(i, 60);
    ASSERT_TRUE(ref.vibe.magnitudeDb(hz, 0.0f, 0.0f) == ref.vibe.magnitudeDb(hz, true));
  }
}

void test_listen_not_in_curves() {
  // LISTEN solos a bell for the ear: the curves are those of the settings, whatever bell is soloed
  const Norm n = eqSettingA();
  EqCurves curves;
  curves.update(n.data());
  for (int listen = -1; listen <= 2; ++listen) {
    EngineRef ref(n, listen);  // the engine's FINE EQ with LISTEN on that bell
    for (int i = 0; i < 100; ++i) {
      const double hz = hzAt(i, 100);
      for (int t = 0; t < 3; ++t) ASSERT_TRUE(std::fabs(curves.fineDb(t, hz) - ref.fine.magnitudeDb(t, hz)) < 0.01);
    }
  }
  // and update() takes the stored table only: the LISTEN session value never reaches it (mapParams is called
  // with listen = -1), so the curves equal those of a table whose bells are all on and LISTEN-free
  const EngineSettings listening = mapParams(n.data(), 1, false);
  ASSERT_TRUE(listening.listenBand == 1);
  EqCurves again;
  again.update(n.data());
  for (int i = 0; i < 50; ++i) ASSERT_TRUE(again.fineDb(0, hzAt(i, 50)) == curves.fineDb(0, hzAt(i, 50)));
}

// A constant input through the DSP block, long enough to settle: the static transfer of the block.
double dspClip(const Norm& n, double x) {
  const EngineSettings s = mapParams(n.data(), -1, false);
  const LoudPush push = loudPush(s.loudness);
  DSP::ClipSoftly clip;
  clip.prepare(48000.0);
  clip.setDriveDb(s.clipDriveDb + push.clipDb);
  const int len = 6000;
  std::vector<float> l(static_cast<std::size_t>(len), static_cast<float>(x)), r = l;
  clip.process(l.data(), r.data(), len);
  return l.back();
}

double dspFinal(const Norm& n, double x) {
  const EngineSettings s = mapParams(n.data(), -1, false);
  const LoudPush push = loudPush(s.loudness);
  DSP::FinalStage fin;
  fin.prepare(48000.0, 1);
  fin.set(s.finalOn, s.finalMode, std::min(1.0f, s.finalEffect + finalEffectPush(push)), s.finalCurve,
          std::min(2.0f, s.finalDriveDb + finalDrivePush(push)), s.ceilingDb);
  const int len = 256;
  std::vector<float> l(static_cast<std::size_t>(len), static_cast<float>(x)), r = l;
  fin.process(l.data(), r.data(), len);
  return l.back();
}

void test_transfer_functions_match_dsp() {
  // CLIP: drive 0 / +2 / +6 dB, LOUDNESS 0 / 2.5 / 5 / 10 (the push adds drive)
  const int driveIdx[3] = {0, 4, 12};
  const int loudIdx[4] = {0, 5, 10, 20};
  const double xs[] = {0.0, 0.05, 0.2, 0.5, 0.7, 0.9, 1.0, 1.02, 1.05, -0.4, -1.0};
  for (int d : driveIdx) {
    for (int l : loudIdx) {
      Norm n = defaultsNorm();
      setIdx(n, kClipOn, 1); setIdx(n, kClipDrive, d); setIdx(n, kLoudness, l);
      for (double x : xs) ASSERT_TRUE(std::fabs(clipTransfer(x, n.data()) - dspClip(n, x)) < 1e-6);
    }
  }
  {  // LOUDNESS moves the curve; CLIP off is the identity
    Norm quiet = defaultsNorm(), loud = defaultsNorm();
    setIdx(quiet, kClipOn, 1); setIdx(quiet, kClipDrive, 4); setIdx(quiet, kLoudness, 0);
    setIdx(loud, kClipOn, 1); setIdx(loud, kClipDrive, 4); setIdx(loud, kLoudness, 20);
    ASSERT_TRUE(std::fabs(clipTransfer(0.8, quiet.data()) - clipTransfer(0.8, loud.data())) > 1e-3);
    Norm off = loud;
    setIdx(off, kClipOn, 0);
    ASSERT_TRUE(clipTransfer(0.8, off.data()) == 0.8);
  }

  // FINAL: INFLATE and CLIP modes, ceilings, LOUDNESS 0 / 5 / 10 (the push adds EFFECT or DRIVE), several curves
  const double fx[] = {0.0, 0.1, 0.3, 0.5, 0.8, 0.95, 1.0, 1.1, 1.25, -0.6, -1.2};
  int compared = 0;
  for (int mode = 0; mode < 2; ++mode) {
    for (int ceil = 0; ceil < 4; ++ceil) {
      for (int l : loudIdx) {
        for (int e : {0, 5, 10}) {
          for (int c : {0, 1, 4}) {
            for (int drv : {0, 4}) {
              Norm n = defaultsNorm();
              setIdx(n, kFinalOn, 1); setIdx(n, kFinalMode, mode); setIdx(n, kFinalEffect, e);
              setIdx(n, kFinalCurve, c); setIdx(n, kFinalDrive, drv);
              setIdx(n, kCeiling, ceil); setIdx(n, kLoudness, l);
              const double ceilLin = std::pow(10.0, mapParams(n.data(), -1, false).ceilingDb / 20.0);
              for (double x : fx) {
                ASSERT_TRUE(std::fabs(finalTransfer(x, n.data(), ceilLin) - dspFinal(n, x)) < 1e-6);
                ++compared;
              }
            }
          }
        }
      }
    }
  }
  ASSERT_TRUE(compared > 1000);
  {  // the curves are not trivial: INFLATE lifts the body, CLIP at +2 dB clamps at the ceiling, LOUDNESS adds
    Norm n = defaultsNorm();
    setIdx(n, kFinalOn, 1); setIdx(n, kFinalMode, 0); setIdx(n, kFinalEffect, 10); setIdx(n, kFinalCurve, 2);
    setIdx(n, kCeiling, 1); setIdx(n, kLoudness, 0);
    const double ceilLin = std::pow(10.0, Tables::kCeilings[1] / 20.0);
    ASSERT_TRUE(finalTransfer(0.5, n.data(), ceilLin) > 0.5 + 0.01);
    ASSERT_TRUE(finalTransfer(2.0, n.data(), ceilLin) <= ceilLin + 1e-12);
    Norm half = n;
    setIdx(half, kFinalEffect, 5);
    const double a = finalTransfer(0.5, half.data(), ceilLin);
    setIdx(half, kLoudness, 20);  // FINAL third complete: EFFECT + 0.6
    ASSERT_TRUE(finalTransfer(0.5, half.data(), ceilLin) > a + 0.005);
    Norm clipMode = defaultsNorm();
    setIdx(clipMode, kFinalOn, 1); setIdx(clipMode, kFinalMode, 1); setIdx(clipMode, kFinalDrive, 4);
    setIdx(clipMode, kCeiling, 1); setIdx(clipMode, kLoudness, 0);
    ASSERT_TRUE(std::fabs(finalTransfer(1.25, clipMode.data(), ceilLin) - ceilLin) < 0.05);
    ASSERT_TRUE(finalTransfer(1.25, clipMode.data(), ceilLin) <= ceilLin + 1e-12);
    Norm off = clipMode;
    setIdx(off, kFinalOn, 0);
    ASSERT_TRUE(finalTransfer(1.1, off.data(), ceilLin) == 1.1);
  }
}

void test_scroll_history_ring() {
  ScrollHistory<8> h;
  ASSERT_TRUE(h.size() == 0 && h.capacity() == 8);
  for (int i = 0; i < 3; ++i) h.push({static_cast<float>(i), 0.f, 0.f, 0.f});
  ASSERT_TRUE(h.size() == 3);
  ASSERT_TRUE(h.at(0).levelDb == 0.f && h.at(2).levelDb == 2.f);
  for (int i = 3; i < 8; ++i) h.push({static_cast<float>(i), static_cast<float>(i) * 2.f, 0.f, 0.f});
  ASSERT_TRUE(h.size() == 8 && h.at(0).levelDb == 0.f && h.at(7).levelDb == 7.f);
  // past the capacity the oldest drop off: size stays, at(0) is the oldest left
  for (int i = 8; i < 58; ++i) h.push({static_cast<float>(i), static_cast<float>(i) * 2.f, 1.f, -1.f});
  ASSERT_TRUE(h.size() == 8);
  for (std::size_t i = 0; i < 8; ++i) {
    ASSERT_TRUE(h.at(i).levelDb == static_cast<float>(50 + i));
    ASSERT_TRUE(h.at(i).grDb == static_cast<float>(50 + i) * 2.f);
    ASSERT_TRUE(h.at(i).netGrDb == 1.f && h.at(i).threshDb == -1.f);
  }
  // an index out of range reads the newest (never outside the ring)
  ASSERT_TRUE(h.at(99).levelDb == 57.f);
  h.clear();
  ASSERT_TRUE(h.size() == 0);
  h.push({5.f, 0.f, 0.f, 0.f});
  ASSERT_TRUE(h.size() == 1 && h.at(0).levelDb == 5.f);
  ScrollHistory<4> empty;
  ASSERT_TRUE(empty.at(0).levelDb == ScrollPoint{}.levelDb);
}

bool pathInside(const std::vector<Point>& path) {
  for (const Point& p : path) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y)) return false;
    if (p.x < kScreenRect.x || p.x > kScreenRect.x + kScreenRect.w) return false;
    if (p.y < kScreenRect.y || p.y > kScreenRect.y + kScreenRect.h) return false;
  }
  return true;
}

void test_axis_scales() {
  // EQ: 20 Hz at the left edge, 20 kHz at the right, 0 dB on the middle line, log spacing
  ASSERT_TRUE(std::fabs(eqX(20.0) - kScreenRect.x) < 1e-3);
  ASSERT_TRUE(std::fabs(eqX(20000.0) - (kScreenRect.x + kScreenRect.w)) < 1e-3);
  ASSERT_TRUE(std::fabs(eqX(632.4555320336759) - (kScreenRect.x + kScreenRect.w / 2)) < 0.05);
  ASSERT_TRUE(std::fabs(eqY(0.0) - (kScreenRect.y + kScreenRect.h / 2)) < 1e-3);
  ASSERT_TRUE(eqY(3.0) < eqY(0.0) && eqY(-3.0) > eqY(0.0));            // up is louder
  ASSERT_TRUE(std::fabs((eqY(0.0) - eqY(3.0)) - (eqY(-3.0) - eqY(0.0))) < 1e-3);
  ASSERT_TRUE(eqY(100.0) == eqY(7.5) && eqY(-100.0) == eqY(-7.5));      // +-7.5 dB range
  ASSERT_TRUE(eqY(7.5) < eqY(7.0));
  // GLUE: level -36..0 from the bottom, GR 0..12 from the top
  ASSERT_TRUE(levelY(ScreenView::Glue, -36.0) > levelY(ScreenView::Glue, -18.0));
  ASSERT_TRUE(levelY(ScreenView::Glue, -18.0) > levelY(ScreenView::Glue, 0.0));
  ASSERT_TRUE(levelY(ScreenView::Glue, -100.0) == levelY(ScreenView::Glue, -36.0));
  ASSERT_TRUE(levelY(ScreenView::Glue, 5.0) == levelY(ScreenView::Glue, 0.0));
  ASSERT_TRUE(grY(ScreenView::Glue, 12.0) > grY(ScreenView::Glue, 6.0) && grY(ScreenView::Glue, 6.0) > grY(ScreenView::Glue, 0.0));
  ASSERT_TRUE(grY(ScreenView::Glue, 50.0) == grY(ScreenView::Glue, 12.0) && grY(ScreenView::Glue, -5.0) == grY(ScreenView::Glue, 0.0));
  // LIMIT: level -24..0, GR 0..8 (other scales than GLUE)
  ASSERT_TRUE(levelY(ScreenView::Limit, -24.0) == levelY(ScreenView::Glue, -36.0));
  ASSERT_TRUE(std::fabs(levelY(ScreenView::Limit, -12.0) - (levelY(ScreenView::Limit, 0.0) + levelY(ScreenView::Limit, -24.0)) / 2) < 1e-3);
  ASSERT_TRUE(grY(ScreenView::Limit, 8.0) == grY(ScreenView::Glue, 12.0));
  ASSERT_TRUE(grY(ScreenView::Limit, 4.0) != grY(ScreenView::Glue, 4.0));
  // CLIP: input 0..1.05, FINAL: 0..1.25, the unity point of each one is where the diagonal passes
  ASSERT_TRUE(clipX(0.0) < clipX(1.0) && clipX(1.0) < clipX(1.05) && clipX(2.0) == clipX(1.05) && clipX(-1.0) == clipX(0.0));
  ASSERT_TRUE(clipY(0.0) > clipY(1.0) && clipY(1.0) > clipY(1.05));
  ASSERT_TRUE(finalX(1.25) > finalX(1.0) && finalX(9.0) == finalX(1.25));
  ASSERT_TRUE(finalY(1.25) < finalY(1.0) && finalY(0.0) > finalY(1.0));
  ASSERT_TRUE(finalCeilingY(0.9) == finalY(0.9) && finalCeilingY(std::pow(10.0, -0.3 / 20.0)) > finalY(1.0));  // below 0 dBFS
  {  // the ceiling line: the table's CEILING as a linear amplitude
    Norm n = defaultsNorm();
    for (int c = 0; c < 4; ++c) {
      setIdx(n, kCeiling, c);
      ASSERT_TRUE(std::fabs(ceilingLinear(n.data()) - std::pow(10.0, Tables::kCeilings[static_cast<std::size_t>(c)] / 20.0)) < 1e-6);
    }
  }
  // history: the newest at the right edge, the oldest of a full ring at the left edge
  ASSERT_TRUE(std::fabs(historyX(9, 10, 150) - (kScreenRect.x + kScreenRect.w)) < 1e-3);
  ASSERT_TRUE(std::fabs(historyX(0, 150, 150) - kScreenRect.x) < 1e-3);
  ASSERT_TRUE(historyX(0, 10, 150) > kScreenRect.x + kScreenRect.w - 10.f * kScreenRect.w / 149.f);
}

void test_curve_paths_inside_screen() {
  const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
  const double dnan = std::numeric_limits<double>::quiet_NaN(), dinf = std::numeric_limits<double>::infinity();

  // EQ views, with the plain, the busy and the extreme settings, and odd pull-backs
  Norm lo(kNumParams, 0.0), hi(kNumParams, 1.0);
  Norm extremeEq = eqSettingB();
  setIdx(extremeEq, kLmGain, 16); setIdx(extremeEq, kMidGain, 16); setIdx(extremeEq, kHmGain, 16); setIdx(extremeEq, kLsGain, 16);
  const Norm sets[] = {defaultsNorm(), eqSettingA(), eqSettingB(), eqSettingC(), lo, hi, extremeEq};
  const EqTrace traces[] = {EqTrace::FineSt, EqTrace::FineMid, EqTrace::FineSide, EqTrace::Vibe, EqTrace::VibeStatic};
  const float pulls[] = {0.f, 0.5f, 1.f, 7.f, -7.f, nan, inf, -inf};
  for (const Norm& n : sets) {
    EqCurves curves;
    curves.update(n.data());
    for (EqTrace t : traces) {
      for (float pl : pulls) {
        const std::vector<Point> path = curvePath(curves, t, pl, pl);
        ASSERT_TRUE(path.size() >= 200);
        ASSERT_TRUE(pathInside(path));
        // the curve runs from 20 Hz at the left edge to 20 kHz at the right, left to right
        ASSERT_TRUE(std::fabs(path.front().x - kScreenRect.x) < 1e-3);
        ASSERT_TRUE(std::fabs(path.back().x - (kScreenRect.x + kScreenRect.w)) < 1e-3);
        for (std::size_t i = 1; i < path.size(); ++i) ASSERT_TRUE(path[i].x > path[i - 1].x);
      }
    }
  }
  {  // the flat curve sits on the 0 dB line; a boost goes above it
    EqCurves curves;
    const Norm flat = defaultsNorm();
    curves.update(flat.data());
    const std::vector<Point> p = curvePath(curves, EqTrace::FineSt);
    for (const Point& q : p) ASSERT_TRUE(std::fabs(q.y - eqY(0.0)) < 1e-3);
    Norm boost = defaultsNorm();
    setIdx(boost, kMidOn, 1); setIdx(boost, kMidFreq, 4); setIdx(boost, kMidGain, 16);
    curves.update(boost.data());
    const std::vector<Point> b = curvePath(curves, EqTrace::FineSt);
    float top = b[0].y;
    for (const Point& q : b) top = std::min(top, q.y);
    ASSERT_TRUE(top < eqY(0.0) - 10.f);
  }

  // GLUE / LIMIT history with ordinary and hostile values
  const float evil[] = {nan, inf, -inf, 1e30f, -1e30f, 0.f, -40.f, 20.f};
  for (ScreenView v : {ScreenView::Glue, ScreenView::Limit}) {
    for (int mode = 0; mode < 2; ++mode) {
      ScrollHistory<64> h;
      for (int i = 0; i < 100; ++i) {
        const float a = mode == 0 ? -30.f + static_cast<float>(i % 30) : evil[static_cast<std::size_t>(i) % 8];
        const float b = mode == 0 ? static_cast<float>(i % 13) : evil[static_cast<std::size_t>(i + 3) % 8];
        h.push({a, b, b * 0.5f, a - 3.f});
        for (HistoryTrace t : {HistoryTrace::Level, HistoryTrace::LevelFill, HistoryTrace::Gr, HistoryTrace::NetGr, HistoryTrace::Thresh}) {
          const std::vector<Point> path = curvePath(v, t, h);
          ASSERT_TRUE(path.size() >= h.size());
          ASSERT_TRUE(pathInside(path));
        }
      }
      // newest at the right edge: the last point of a trace is on the right
      const std::vector<Point> lvl = curvePath(v, HistoryTrace::Level, h);
      ASSERT_TRUE(lvl.size() == 64 && std::fabs(lvl.back().x - (kScreenRect.x + kScreenRect.w)) < 1e-3);
      ASSERT_TRUE(std::fabs(lvl.front().x - kScreenRect.x) < 1e-3);
    }
  }
  {  // a short history starts part-way in; an empty one has no path
    ScrollHistory<150> h;
    ASSERT_TRUE(curvePath(ScreenView::Glue, HistoryTrace::Level, h).empty());
    h.push({-20.f, 3.f, 2.f, -10.f});
    h.push({-18.f, 4.f, 2.f, -10.f});
    const std::vector<Point> p = curvePath(ScreenView::Glue, HistoryTrace::Level, h);
    ASSERT_TRUE(p.size() == 2 && p[0].x > kScreenRect.x + 100.f && std::fabs(p[1].x - (kScreenRect.x + kScreenRect.w)) < 1e-3);
    ASSERT_TRUE(p[0].y == levelY(ScreenView::Glue, -20.0));
    const std::vector<Point> g = curvePath(ScreenView::Glue, HistoryTrace::Gr, h);
    ASSERT_TRUE(g[0].y == grY(ScreenView::Glue, 3.0) && g[1].y == grY(ScreenView::Glue, 4.0));
    const std::vector<Point> fill = curvePath(ScreenView::Glue, HistoryTrace::LevelFill, h);
    ASSERT_TRUE(fill.size() == 4 && fill[2].y == kScreenRect.y + kScreenRect.h && fill[3].y == kScreenRect.y + kScreenRect.h);
  }

  // CLIP / FINAL transfer curves, with every setting extreme and hostile ceilings
  const double ceilings[] = {0.966, 1.0, 0.5, 0.0, -1.0, dnan, dinf, 1e9};
  for (const Norm& n : sets) {
    for (double c : ceilings) {
      const std::vector<Point> cp = curvePath(ScreenView::Clip, n.data(), c);
      const std::vector<Point> fp = curvePath(ScreenView::Final, n.data(), c);
      ASSERT_TRUE(cp.size() >= 100 && fp.size() >= 100);
      ASSERT_TRUE(pathInside(cp) && pathInside(fp));
      ASSERT_TRUE(std::fabs(cp.front().x - clipX(0.0)) < 1e-3 && std::fabs(cp.back().x - clipX(Scale::kClipMax)) < 1e-3);
      ASSERT_TRUE(std::fabs(fp.front().x - finalX(0.0)) < 1e-3 && std::fabs(fp.back().x - finalX(Scale::kFinalMax)) < 1e-3);
    }
  }
  // the views without such a path give nothing
  Norm d = defaultsNorm();
  ASSERT_TRUE(curvePath(ScreenView::Eq, d.data(), 1.0).empty() && curvePath(ScreenView::Glue, d.data(), 1.0).empty());
  // the transfer curve follows clipTransfer: at x = 0.5 the point is where clipTransfer says
  Norm clipN = defaultsNorm();
  setIdx(clipN, kClipOn, 1); setIdx(clipN, kClipDrive, 12); setIdx(clipN, kLoudness, 20);
  const std::vector<Point> cp = curvePath(ScreenView::Clip, clipN.data(), 1.0);
  std::size_t best = 0;
  for (std::size_t i = 0; i < cp.size(); ++i)
    if (std::fabs(cp[i].x - clipX(0.9)) < std::fabs(cp[best].x - clipX(0.9))) best = i;
  const double xa = Scale::kClipMax * static_cast<double>(best) / static_cast<double>(cp.size() - 1);
  ASSERT_TRUE(std::fabs(cp[best].y - clipY(std::fabs(clipTransfer(xa, clipN.data())))) < 1e-3);
}

// ---- Preferences, decor, screws, tapes (ui/prefs.h, ui/decor.h, ui/screw_angles.h, ui/tapes.h) ------------------------
namespace fs = std::filesystem;
using FairyTapes::Ui::EditorSize;
using FairyTapes::Ui::WearLevel;

// A scratch folder of its own: the tests only ever use explicit paths, never the real preferences folder.
fs::path scratchDir() {
  static fs::path dir;
  if (dir.empty()) {
    std::error_code ec;
    dir = fs::temp_directory_path(ec) / ("happy_ending_ui_test_" + std::to_string(std::rand()) + "_" + std::to_string(std::time(nullptr)));
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
  }
  return dir;
}
void writeFile(const fs::path& p, const std::string& text) { std::ofstream(p, std::ios::binary) << text; }
bool isDefaultPrefs(const Prefs& p) {
  const Prefs d;
  return p.wear == d.wear && p.notes == d.notes && p.tapeFont == d.tapeFont && p.handwrittenValues == d.handwrittenValues &&
         p.size == d.size;
}

void test_prefs_parse_defaults_and_bad_values() {
  const Prefs d;
  ASSERT_TRUE(d.wear == WearLevel::Light && d.notes && d.tapeFont == TapeFont::Zeyada && d.handwrittenValues && d.size == EditorSize::S100);
  ASSERT_TRUE(isDefaultPrefs(parsePrefs("")));
  ASSERT_TRUE(isDefaultPrefs(parsePrefs("wear\n=Heavy\nnoequals here\n# wear=Off\nunknownKey=1\n# size=75\n")));
  const Prefs p = parsePrefs("wear=Rusty\nnotes=Hide\ntapeFont=Comic\nvaluesFont=Standard\nsize=33\n");
  ASSERT_TRUE(p.wear == WearLevel::Light && !p.notes && p.tapeFont == TapeFont::Zeyada && !p.handwrittenValues && p.size == EditorSize::S100);
  const Prefs q = parsePrefs("# only wear\n  wear = Heavy  \r\nnotes=maybe\nvaluesFont=Mono\n  size = 125 \r\n");
  ASSERT_TRUE(q.wear == WearLevel::Heavy && q.notes && q.handwrittenValues && q.size == EditorSize::S125);
  // a bad size falls back on its own, without touching the other keys
  const Prefs r = parsePrefs("wear=Off\nsize=big\n");
  ASSERT_TRUE(r.wear == WearLevel::Off && r.size == EditorSize::S100);
  ASSERT_TRUE(parsePrefs("size=75\n").size == EditorSize::S75 && parsePrefs("size=90\n").size == EditorSize::S90);
}

void test_prefs_roundtrip() {
  const WearLevel wears[] = {WearLevel::Off, WearLevel::Light, WearLevel::Heavy};
  const TapeFont fonts[] = {TapeFont::Zeyada, TapeFont::RockSalt, TapeFont::HomemadeApple};
  const EditorSize sizes[] = {EditorSize::S75, EditorSize::S90, EditorSize::S100, EditorSize::S125};
  for (WearLevel w : wears)
    for (TapeFont f : fonts)
      for (EditorSize z : sizes)
        for (int b = 0; b < 4; ++b) {
          Prefs p;
          p.wear = w;
          p.tapeFont = f;
          p.size = z;
          p.notes = (b & 1) != 0;
          p.handwrittenValues = (b & 2) != 0;
          const Prefs q = parsePrefs(serializePrefs(p));
          ASSERT_TRUE(q.wear == w && q.tapeFont == f && q.size == z && q.notes == p.notes &&
                      q.handwrittenValues == p.handwrittenValues);
        }
  // the key is written as "size=125"
  Prefs p;
  p.size = EditorSize::S125;
  ASSERT_TRUE(serializePrefs(p).find("\nsize=125\n") != std::string::npos);
}

void test_prefs_file_missing_or_oversized_or_binary() {
  const fs::path file = scratchDir() / "cfg" / "FairyTapes" / "HappyEnding.prefs";   // no environment changes: explicit paths
  ASSERT_TRUE(isDefaultPrefs(loadPrefs(file)));   // absent
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  writeFile(file, "wear=Heavy\n" + std::string(kMaxPrefsBytes, '#'));   // oversized, with a valid line in it
  ASSERT_TRUE(isDefaultPrefs(loadPrefs(file)));
  writeFile(file, std::string("wear=Off\n\0\0\0binary", 18));   // binary
  ASSERT_TRUE(isDefaultPrefs(loadPrefs(file)));
  writeFile(file, std::string("wear=Heavy\nsize=75\n") + std::string(kMaxPrefsBytes - 40, '#'));   // large but allowed
  ASSERT_TRUE(loadPrefs(file).wear == WearLevel::Heavy && loadPrefs(file).size == EditorSize::S75);
  fs::remove(file, ec);
  Prefs p;
  p.wear = WearLevel::Off;
  p.notes = false;
  p.tapeFont = TapeFont::RockSalt;
  p.handwrittenValues = false;
  p.size = EditorSize::S90;
  ASSERT_TRUE(savePrefs(file, p));
  const Prefs q = loadPrefs(file);
  ASSERT_TRUE(q.wear == WearLevel::Off && !q.notes && q.tapeFont == TapeFont::RockSalt && !q.handwrittenValues &&
              q.size == EditorSize::S90);
  ASSERT_TRUE(!fs::exists(fs::path(file) += ".tmp", ec));
  int files = 0;
  for (const auto& e : fs::directory_iterator(file.parent_path(), ec)) { (void)e; ++files; }
  ASSERT_TRUE(files == 1);   // only HappyEnding.prefs
  writeFile(scratchDir() / "blocker", "x");   // a file where the folder should be, and a folder as target: both fail cleanly
  ASSERT_TRUE(!savePrefs(scratchDir() / "blocker" / "HappyEnding.prefs", p));
  ASSERT_TRUE(!savePrefs(fs::path(), p));
  ASSERT_TRUE(!savePrefs(file.parent_path(), p));
  ASSERT_TRUE(isDefaultPrefs(loadPrefs(fs::path())) && isDefaultPrefs(loadPrefs(file.parent_path())));
}

void test_prefs_path_per_os() {
#if defined(_WIN32)
  ASSERT_TRUE(prefsPathFor("C:/Users/u", "", "C:/Users/u/AppData/Roaming") ==
              fs::path("C:/Users/u/AppData/Roaming") / "FairyTapes" / "HappyEnding.prefs");
#elif defined(__APPLE__)
  ASSERT_TRUE(prefsPathFor("/Users/u", "", "") == fs::path("/Users/u/Library/Application Support/FairyTapes/HappyEnding.prefs"));
#else
  ASSERT_TRUE(prefsPathFor("/home/u", "", "") == fs::path("/home/u/.config/FairyTapes/HappyEnding.prefs"));
  ASSERT_TRUE(prefsPathFor("/home/u", "/xdg", "") == fs::path("/xdg/FairyTapes/HappyEnding.prefs"));
  ASSERT_TRUE(prefsPathFor("/home/u", "relative/xdg", "") == fs::path("/home/u/.config/FairyTapes/HappyEnding.prefs"));
#endif
  ASSERT_TRUE(prefsPathFor("", "", "").empty());
  ASSERT_TRUE(prefsPath().empty() || prefsPath().filename() == "HappyEnding.prefs");
}

// ---- decor ------------------------------------------------------------------------------------------------------------
bool tapeInsidePanel(const TapePlacement& t, const Rect& p) {
  const float r = t.rotDeg * 3.14159265f / 180.f, c = std::cos(r), s = std::sin(r);
  for (int i = 0; i < 4; ++i) {
    const float lx = (i & 1 ? 0.5f : -0.5f) * t.w, ly = (i & 2 ? 0.5f : -0.5f) * t.h;
    const float x = t.cx + lx * c - ly * s, y = t.cy + lx * s + ly * c;
    if (x < p.x || x > p.x + p.w || y < p.y || y > p.y + p.h) return false;
  }
  return true;
}

void test_decor_layout_deterministic() {
  const Rect panel = kPanel;
  int sawMissing = 0;
  for (uint32_t seed = 0; seed < 3000; ++seed) {
    for (WearLevel lv : {WearLevel::Off, WearLevel::Light, WearLevel::Heavy}) {
      const WearLayout a = layoutFromSeed(seed, lv, panel), b = layoutFromSeed(seed, lv, panel);
      ASSERT_TRUE(a.tapes.size() == b.tapes.size() && a.screws.size() == 4);
      for (size_t i = 0; i < a.tapes.size(); ++i)
        ASSERT_TRUE(a.tapes[i].cx == b.tapes[i].cx && a.tapes[i].cy == b.tapes[i].cy && a.tapes[i].rotDeg == b.tapes[i].rotDeg &&
                    a.tapes[i].slot == b.tapes[i].slot && tapeInsidePanel(a.tapes[i], panel));
      const size_t n = a.tapes.size();
      if (lv == WearLevel::Off) ASSERT_TRUE(n == 0);
      if (lv == WearLevel::Light) ASSERT_TRUE(n >= 1 && n <= 2);
      if (lv == WearLevel::Heavy) ASSERT_TRUE(n >= 2 && n <= 3);
      int missing = 0;
      for (const auto& s : a.screws) missing += s.missing ? 1 : 0;
      ASSERT_TRUE(missing <= 1);
      sawMissing += missing;
      std::set<int> slots;   // distinct slots, valid index
      for (const auto& t : a.tapes) ASSERT_TRUE(t.slot >= 0 && t.slot < kNumTapeSlots && slots.insert(t.slot).second);
    }
  }
  ASSERT_TRUE(sawMissing > 0);
  // Light is a prefix of Heavy; screws do not depend on the level; the seed matters
  for (uint32_t seed = 0; seed < 500; ++seed) {
    const WearLayout l = layoutFromSeed(seed, WearLevel::Light, panel), h = layoutFromSeed(seed, WearLevel::Heavy, panel);
    ASSERT_TRUE(l.tapes.size() <= h.tapes.size());
    for (size_t i = 0; i < l.tapes.size(); ++i) ASSERT_TRUE(l.tapes[i].slot == h.tapes[i].slot && l.tapes[i].cx == h.tapes[i].cx);
    for (int i = 0; i < 4; ++i) ASSERT_TRUE(l.screws[i].angleDeg == h.screws[i].angleDeg && l.screws[i].missing == h.screws[i].missing);
  }
  bool differs = false;
  const WearLayout a = layoutFromSeed(5u, WearLevel::Heavy, panel);
  for (uint32_t s = 6; s < 40 && !differs; ++s) {
    const WearLayout c = layoutFromSeed(s, WearLevel::Heavy, panel);
    differs = c.tapes.size() != a.tapes.size() || c.tapes[0].cx != a.tapes[0].cx || c.screws[0].angleDeg != a.screws[0].angleDeg;
  }
  ASSERT_TRUE(differs);
  // a different panel size scales the placement and keeps the tapes inside
  const Rect small{10, 20, 640, 445};
  for (uint32_t seed = 0; seed < 300; ++seed)
    for (const auto& t : layoutFromSeed(seed, WearLevel::Heavy, small).tapes) ASSERT_TRUE(tapeInsidePanel(t, small));
  // the drawn tape (ragged ends and shadow) stays inside the panel too, at the edge where the clamp acts
  const Rect tight{0, 0, 120, 80};
  for (uint32_t seed = 0; seed < 300; ++seed)
    for (const auto& t : layoutFromSeed(seed, WearLevel::Heavy, tight).tapes) {
      const auto b = FairyTapes::Ui::tapeBounds(t.cx, t.cy, t.w, t.h, t.rotDeg, FairyTapes::Ui::kTapeRaggedX + 1.0);
      ASSERT_TRUE(b.left >= -1e-3 && b.top >= -1e-3 && b.right <= tight.w + 1e-3 && b.bottom <= tight.h + 1e-3);
    }
}

// What a generated tape must never touch: the control views, the screens, the meter and GR rows, the clip and ACT LEDs,
// the LUFS screen, the target LED (fixedReadouts), the MATCH and view actions, and the screws.
bool overlapsBox(const FairyTapes::Ui::TapeBox& b, const Rect& r) {
  return b.left < r.x + r.w && b.right > r.x && b.top < r.y + r.h && b.bottom > r.y;
}
FairyTapes::Ui::TapeBox drawnBox(const TapePlacement& t) {   // rotated rectangle plus ragged ends and shadow
  return FairyTapes::Ui::tapeBounds(t.cx, t.cy, t.w, t.h, t.rotDeg, FairyTapes::Ui::kTapeRaggedX + 1.0);
}
Rect asRect(const FairyTapes::Ui::TapeBox& b) {
  return {static_cast<float>(b.left), static_cast<float>(b.top), static_cast<float>(b.right - b.left), static_cast<float>(b.bottom - b.top)};
}

void test_decor_tapes_never_cover_controls() {
  struct Obstacle {
    Rect rect;
    std::string what;
  };
  std::vector<Obstacle> obstacles;
  for (const Rect& r : fixedReadouts()) obstacles.push_back({r, "a screen, meter or LED"});
  for (const ControlSpec& c : kControls) obstacles.push_back({viewRect(c), std::string("control ") + (c.name ? c.name : "?")});
  for (const ActionSpec& a : kActions) obstacles.push_back({viewRect(a), std::string("action ") + a.name});
  obstacles.push_back({kLufsScreenRect, "the LUFS screen"});
  obstacles.push_back({kScreenRect, "the screen"});
  for (const MeterRow& r : kMeterRows) obstacles.push_back({meterBarRect(r), "a peak row"});
  std::set<std::string> clashes;   // "slot N covers X", all of them listed before failing
  size_t tapesSeen = 0;
  for (uint32_t seed = 0; seed < 200; ++seed) {
    for (WearLevel lv : {WearLevel::Light, WearLevel::Heavy, WearLevel::Off}) {
      const WearLayout l = layoutFromSeed(seed, lv, kPanel);
      for (size_t i = 0; i < l.tapes.size(); ++i) {
        ++tapesSeen;
        const auto b = drawnBox(l.tapes[i]);
        const std::string slot = "slot " + std::to_string(l.tapes[i].slot) + " covers ";
        for (const Obstacle& o : obstacles)
          if (overlapsBox(b, o.rect)) clashes.insert(slot + o.what);
        for (const ScrewPlacement& s : l.screws)
          if (overlapsBox(b, Rect{s.x - 7, s.y - 7, 14, 14})) clashes.insert(slot + "a screw");
        for (size_t j = i + 1; j < l.tapes.size(); ++j)
          if (overlapsBox(b, asRect(drawnBox(l.tapes[j])))) clashes.insert(slot + "tape " + std::to_string(l.tapes[j].slot));
      }
    }
  }
  for (const std::string& c : clashes) std::fprintf(stderr, "  %s\n", c.c_str());
  ASSERT_TRUE(tapesSeen > 400);   // the loop did look at tapes
  ASSERT_TRUE(clashes.empty());
}

void test_screw_look_matches_shared_generator() {
  const Rect panel = kPanel;
  for (uint32_t k = 0; k < 5000; ++k) {
    const uint32_t seed = k * 2654435761u;
    const auto shared = FairyTapes::Ui::generateScrews(seed);
    const WearLayout l = layoutFromSeed(seed, WearLevel::Light, panel);
    ASSERT_TRUE(l.screws.size() == shared.size());
    for (size_t i = 0; i < shared.size(); ++i) {
      ASSERT_TRUE(l.screws[i].cross == shared[i].cross && l.screws[i].missing == shared[i].missing &&
                  l.screws[i].angleDeg == static_cast<float>(shared[i].initialAngle));
      ASSERT_TRUE(l.screws[i].x == kScrewPos[i][0] && l.screws[i].y == kScrewPos[i][1]);
    }
    const auto specs = screwSpecs(seed);   // screw_angles.h: the same look at the kScrewPos
    for (size_t i = 0; i < shared.size(); ++i)
      ASSERT_TRUE(specs[i].x == kScrewPos[i][0] && specs[i].y == kScrewPos[i][1] && specs[i].cross == shared[i].cross &&
                  specs[i].missing == shared[i].missing && specs[i].initialAngle == shared[i].initialAngle);
  }
}

bool nearD(double a, double b) { return std::fabs(a - b) < 1e-4; }

void test_screw_angles() {
  ASSERT_TRUE(nearD(shownScrewAngle(30.0, 0.f), 30) && nearD(shownScrewAngle(30.0, 45.5f), 75.5) && nearD(shownScrewAngle(170.0, 300.f), 110));
  ASSERT_TRUE(nearD(shownScrewAngle(30.0, std::nanf("")), 30) && nearD(shownScrewAngle(30.0, INFINITY), 30));
  ASSERT_TRUE(nearD(userScrewAngle(75.5, 30.0), 45.5) && nearD(userScrewAngle(10, 30.0), 340) && userScrewAngle(30, 30.0) == 0.f);
  for (double initial : {0.0, 8.0, 91.0, 180.0})
    for (double shown : {0.0, 0.25, 33.3, 179.9, 359.75}) {
      const float user = userScrewAngle(shown, initial);
      ASSERT_TRUE(user >= 0.f && user < 360.f);
      const double back = shownScrewAngle(initial, user);
      ASSERT_TRUE(nearD(back, shown) || nearD(std::fabs(back - shown), 360));
    }
  Decor d;
  d.seed = 77;
  d.screwAngles[1] = 12.f;
  d.tapeTexts = {"VOX"};
  const auto e = withScrewAngle(d, 2, 400.f);
  ASSERT_TRUE(e.seed == 77 && e.tapeTexts == d.tapeTexts && e.screwAngles[0] == 0.f && e.screwAngles[1] == 12.f &&
              e.screwAngles[2] == 40.f && e.screwAngles[3] == 0.f);
  ASSERT_TRUE(withScrewAngle(d, -1, 5.f).screwAngles[0] == 0.f && withScrewAngle(d, 4, 5.f).screwAngles[3] == 0.f);
  ASSERT_TRUE(withScrewAngle(d, 0, std::nanf("")).screwAngles[0] == 0.f);
}

void test_tape_texts_and_ink() {
  using FairyTapes::Ui::TapeFontId;
  ASSERT_TRUE(tapeFontIdOf(TapeFont::Zeyada) == TapeFontId::Zeyada && tapeFontIdOf(TapeFont::RockSalt) == TapeFontId::RockSalt &&
              tapeFontIdOf(TapeFont::HomemadeApple) == TapeFontId::HomemadeApple);
  Decor d;
  ASSERT_TRUE(tapeTextFor(d, 0).empty());
  d.tapeTexts = {"VOX", "", "keep!"};
  ASSERT_TRUE(tapeTextFor(d, 0) == "VOX" && tapeTextFor(d, 2) == "keep!" && tapeTextFor(d, 3).empty() && tapeTextFor(d, -1).empty());
  Decor e = withTapeText(Decor{}, 4, "  TAKE 3  ");
  ASSERT_TRUE(e.tapeTexts.size() == 5 && e.tapeTexts[4] == "TAKE 3");
  e = withTapeText(e, 4, "a very long text indeed");
  ASSERT_TRUE(e.tapeTexts[4] == "a very long te");
  ASSERT_TRUE(withTapeText(e, kNumTapeSlots, "x").tapeTexts == e.tapeTexts && withTapeText(e, -1, "x").tapeTexts == e.tapeTexts);
  const TapeLook a = tapeLook(5, 2), b = tapeLook(5, 2), c = tapeLook(5, 3);
  ASSERT_TRUE(a.paperSeed == b.paperSeed && a.inkSeed == b.inkSeed && a.paperSeed != c.paperSeed);
  for (uint32_t s = 0; s < 200; ++s)
    for (int slot = 0; slot < kNumTapeSlots; ++slot) {
      const TapeLook l = tapeLook(s, slot);
      ASSERT_TRUE(l.textSize >= 11 && l.textSize <= 14 && std::fabs(l.textRotDeg) <= 3);
    }
  for (TapeFontId f : {TapeFontId::Zeyada, TapeFontId::RockSalt, TapeFontId::HomemadeApple})
    for (const char* t : {"VOX", "WWWWWWWWWWWWWW", "@@@@@@@@@@@@@@", "7/12 keep!"})
      for (double w : {58.0, 70.0, 96.0})
        for (uint32_t seed = 0; seed < 20; ++seed) {
          const TapeLook look = tapeLook(seed, static_cast<int>(seed % kNumTapeSlots));
          const TapeInk ink = tapeInk(f, t, w, look);
          ASSERT_TRUE(ink.emPx > 0);
          double lo = 0, hi = 0;
          for (const auto& p : FairyTapes::Ui::layoutText(f, ink.text, ink.emPx, look.inkSeed, 1.0)) {
            lo = std::min(lo, p.x);
            hi = std::max(hi, p.x + p.glyph->advance / 1000.0 * ink.emPx * p.scale);
          }
          ASSERT_TRUE(hi - lo <= w - 2 * kTapeTextEdge);
        }
  ASSERT_TRUE(tapeInk(TapeFontId::Zeyada, "", 96, tapeLook(1, 0)).text.empty());
}

} // namespace

int main() {
  RUN_TEST(test_editor_size);
  RUN_TEST(test_layout_binds_every_param_once);
  RUN_TEST(test_layout_rects_inside);
  RUN_TEST(test_layout_no_control_overlap);
  RUN_TEST(test_in_buttons_top_right);
  RUN_TEST(test_titles_bottom_and_tags);
  RUN_TEST(test_dimming_rules);
  RUN_TEST(test_final_mode_visibility);
  RUN_TEST(test_silkscreen_items_cover_controls);
  RUN_TEST(test_tape_slots_clear);
  RUN_TEST(test_value_text_golden);
  RUN_TEST(test_value_text_hp_side_scale);
  RUN_TEST(test_value_text_parse_roundtrip);
  RUN_TEST(test_lufs_readouts);
  RUN_TEST(test_release_text_follows_tuning);
  RUN_TEST(test_eq_curves_match_engine);
  RUN_TEST(test_eq_curves_ms);
  RUN_TEST(test_vibe_pull_back);
  RUN_TEST(test_listen_not_in_curves);
  RUN_TEST(test_transfer_functions_match_dsp);
  RUN_TEST(test_scroll_history_ring);
  RUN_TEST(test_axis_scales);
  RUN_TEST(test_curve_paths_inside_screen);
  RUN_TEST(test_prefs_parse_defaults_and_bad_values);
  RUN_TEST(test_prefs_roundtrip);
  RUN_TEST(test_prefs_file_missing_or_oversized_or_binary);
  RUN_TEST(test_prefs_path_per_os);
  RUN_TEST(test_decor_layout_deterministic);
  RUN_TEST(test_decor_tapes_never_cover_controls);
  RUN_TEST(test_screw_look_matches_shared_generator);
  RUN_TEST(test_screw_angles);
  RUN_TEST(test_tape_texts_and_ink);
  std::error_code ec;
  fs::remove_all(scratchDir(), ec);   // the scratch folder of the prefs tests
  std::printf("%d passed, %d failed\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
