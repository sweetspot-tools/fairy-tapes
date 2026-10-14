/**
 * Fairy Tapes · The Stripper — editor layout table and dimming rules (header-only, no VSTGUI).
 */

#include "ui/layout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>

using namespace FairyTapes::TheStripper;
using namespace FairyTapes::TheStripper::Ui;

namespace {
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

bool overlaps(const Rect& a, const Rect& b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
bool inside(const Rect& inner, const Rect& outer) {
  return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.w <= outer.x + outer.w &&
         inner.y + inner.h <= outer.y + outer.h;
}

void test_controls_are_unique_and_valid() {
  std::set<unsigned> seen;
  for (const auto& c : kControls) {
    CHECK(c.id < kNumParams);
    CHECK(seen.insert(c.id).second);   // one control per parameter
    CHECK(c.rect.w > 0 && c.rect.h > 0);
  }
}

void test_only_inert_parameters_are_unbound() {
  // Gain staging is out of scope (inert); the filters share one IN button (kBypassLPHP), the per-filter switches stay at 1.
  const std::set<unsigned> allowed = {kBypassGainStaging, kGainStagingTrim, kEqHPFOn, kEqLPFOn};
  std::set<unsigned> bound;
  for (const auto& c : kControls) bound.insert(c.id);
  for (unsigned id = 0; id < kNumParams; ++id) CHECK(bound.count(id) || allowed.count(id));
  for (unsigned id : allowed) CHECK(!bound.count(id));
}

void test_controls_fit_window_and_their_box() {
  const Rect window{0, 0, kEditorWidth, kEditorHeight};
  for (const auto& c : kControls) {
    CHECK(inside(c.rect, window));
    if (c.module == Module::None) continue;
    const BoxSpec* box = boxOf(c.module);
    CHECK(box != nullptr);
    if (box) CHECK(inside(c.rect, box->rect));
  }
  for (const auto& b : kBoxes) CHECK(inside(b.rect, window));
}

void test_controls_do_not_overlap() {
  const int n = static_cast<int>(sizeof(kControls) / sizeof(kControls[0]));
  for (int i = 0; i < n; ++i)
    for (int j = i + 1; j < n; ++j) {
      if (kControls[i].sharesView || kControls[j].sharesView) continue;   // the Saturate knob is the Drive knob
      CHECK(!overlaps(kControls[i].rect, kControls[j].rect));
    }
}

void test_bottom_margin() {
  double bottom = 0;
  for (const auto& b : kBoxes) bottom = std::max(bottom, b.rect.y + b.rect.h);
  CHECK(kEditorHeight - bottom >= 28);   // about 30 px under the last row of boxes (spec, Spazio)
}

void test_in_buttons_polarity() {
  // bypass parameters are inverted (IN lit = bypass 0); the filter switches are already positive
  CHECK(inButtonOf(Module::Eq) && inButtonOf(Module::Eq)->inverted);
  CHECK(inButtonOf(Module::Filters) && inButtonOf(Module::Filters)->inverted);   // one IN for both filters
  CHECK(inButtonOf(Module::Filters)->id == kBypassLPHP);
  CHECK(inButtonOf(Module::Input) == nullptr);
  for (const auto& c : kControls) {
    if (c.kind != Kind::Toggle || !c.inverted) continue;
    CHECK(c.id >= kBypassLPHP && c.id <= kBypassGate);   // only the kBypass* parameters are inverted
  }
}

void test_ui_value_mapping() {
  CHECK(uiValue(true, 0.0) == 1.0);
  CHECK(uiValue(true, 1.0) == 0.0);
  CHECK(uiValue(false, 0.25) == 0.25);
  CHECK(paramValue(true, 1.0) == 0.0);
  CHECK(paramValue(true, 0.0) == 1.0);
  CHECK(paramValue(false, 0.7) == 0.7);
}

void test_module_dimming() {
  ModuleState on;   // every IN lit, global bypass off
  for (const auto& m : kModules) CHECK(!moduleDimmed(m, on));

  ModuleState eqOff = on;
  eqOff.in[static_cast<int>(Module::Eq)] = false;
  CHECK(moduleDimmed(Module::Eq, eqOff));
  CHECK(!moduleDimmed(Module::Comp, eqOff));

  ModuleState byp = on;
  byp.globalBypass = true;
  for (const auto& m : kModules) CHECK(moduleDimmed(m, byp));   // global bypass dims every module, even Input/Output

  // A module without an IN button follows the global bypass only
  ModuleState noIn = on;
  noIn.in[static_cast<int>(Module::Input)] = false;
  CHECK(!moduleDimmed(Module::Input, noIn));
}

void test_mode_visibility() {
  // Voice/Drums/Classic exist only in Channel mode
  CHECK(visibleInMode(kCompressorMode, true));
  CHECK(!visibleInMode(kCompressorMode, false));
  CHECK(visibleInMode(kCompressAmount, false));
  CHECK(visibleInMode(kEqLFGain, false));
}
void test_meter_band_fits_level_box() {
  const BoxSpec* level = boxOf(Module::Level);
  CHECK(level != nullptr);
  for (const auto& row : kMeterRows) {
    CHECK(row.id >= kMeterBase && row.id < kNumMeterIDs);
    CHECK(inside(meterBarRect(row), level->rect));
  }
  for (int i = 0; i < kClipCount; ++i) {
    CHECK(kClips[i].id >= kClipInput && kClips[i].id <= kClipOutput);
    CHECK(inside(clipRect(i), level->rect));
    for (int j = i + 1; j < kClipCount; ++j) CHECK(!overlaps(clipRect(i), clipRect(j)));
  }
  // rows must not touch each other
  const int n = static_cast<int>(sizeof(kMeterRows) / sizeof(kMeterRows[0]));
  for (int i = 0; i < n; ++i) {
    for (int j = i + 1; j < n; ++j) CHECK(!overlaps(meterBarRect(kMeterRows[i]), meterBarRect(kMeterRows[j])));
    for (int c = 0; c < kClipCount; ++c) CHECK(!overlaps(meterBarRect(kMeterRows[i]), clipRect(c)));
  }
}

void test_drive_knob_is_shared_by_the_two_engines() {
  const ControlSpec* nam = nullptr; const ControlSpec* sat = nullptr; const ControlSpec* sw = nullptr;
  for (const auto& c : kControls) {
    if (c.id == kDriveAmount) nam = &c;
    if (c.id == kEqSaturation) sat = &c;
    if (c.id == kDriveNamOn) sw = &c;
  }
  CHECK(nam && sat && sw);
  if (!(nam && sat && sw)) return;
  CHECK(!nam->sharesView && sat->sharesView);           // one view, two parameters
  CHECK(nam->rect.x == sat->rect.x && nam->rect.y == sat->rect.y);
  CHECK(std::string(nam->label) == std::string(sat->label));   // same label: the swap is not visible
  // two exclusive buttons NAM / 4K, like Voice/Drums/Classic; the parameter is 1 for the NAM, so the choice is inverted
  CHECK(sw->kind == Kind::Choice && sw->module == Module::Drive && !sw->isIn && sw->inverted);
  CHECK(std::string(sw->choices[0]) == "NAM" && std::string(sw->choices[1]) == "4K" && sw->choices[2] == nullptr);
  CHECK(uiValue(sw->inverted, 1.0) == 0.0 && uiValue(sw->inverted, 0.0) == 1.0);   // NAM = first button = index 0
  CHECK(driveEngineParam(true) == kDriveAmount && driveEngineParam(false) == kEqSaturation);
  CHECK(driveKnobDimmed(true, false) && !driveKnobDimmed(true, true));   // NAM without a model: dimmed
  CHECK(!driveKnobDimmed(false, false) && !driveKnobDimmed(false, true)); // Saturate never needs a model
  CHECK(stripShowsSaturate(false) && !stripShowsSaturate(true));
}

void test_meter_dimming() {
  // global bypass dims GR and the EQ/CMP/DRV/LIM clip LEDs; the level meters and the IN/OUT LEDs stay lit
  for (const auto& row : kMeterRows) {
    const bool gr = row.id == kMeterGrComp || row.id == kMeterGrLimiter || row.id == kMeterGrGate;
    CHECK(meterDimmed(row.alwaysLit, true) == !row.alwaysLit);
    CHECK(row.alwaysLit == !gr);
    CHECK(!meterDimmed(row.alwaysLit, false));
  }
  CHECK(kClips[0].alwaysLit && kClips[5].alwaysLit);
  for (int i = 1; i < 5; ++i) CHECK(!kClips[i].alwaysLit);
}
void test_gate_row_sits_between_gain_reduction_and_clip() {
  const MeterRow* limiter = nullptr; const MeterRow* gate = nullptr;
  for (const auto& r : kMeterRows) {
    if (r.id == kMeterGrLimiter) limiter = &r;
    if (r.id == kMeterGrGate) gate = &r;
  }
  CHECK(limiter && gate);
  if (!(limiter && gate)) return;
  CHECK(gate->gainReduction && !gate->alwaysLit && std::string(gate->tag) == "G");
  CHECK(gate->cy > limiter->cy);
  CHECK(gate->cy < kClipRowY - 8);   // above the clip LEDs
  CHECK(kGrGateMaxDb == 40.0);
}

void test_gr_bar_norm() {
  CHECK(grBarNorm(0.0, 20) == 0.0);
  CHECK(grBarNorm(std::nan(""), 20) == 0.0);
  CHECK(std::fabs(grBarNorm(0.9, 20) - 1.0) < 1e-9);          // 20 dB on a 20 dB scale
  CHECK(std::fabs(grBarNorm(1.0 - std::pow(10.0, -10.0 / 20.0), 20) - 0.5) < 1e-9);   // 10 dB -> half
  CHECK(grBarNorm(0.99, 20) == 1.0);                           // beyond the scale: full
  CHECK(grBarNorm(0.5, 0) == 0.0);
}
} // namespace

int main() {
  test_controls_are_unique_and_valid();
  test_only_inert_parameters_are_unbound();
  test_controls_fit_window_and_their_box();
  test_controls_do_not_overlap();
  test_bottom_margin();
  test_in_buttons_polarity();
  test_ui_value_mapping();
  test_module_dimming();
  test_mode_visibility();
  test_meter_band_fits_level_box();
  test_drive_knob_is_shared_by_the_two_engines();
  test_meter_dimming();
  test_gr_bar_norm();
  test_gate_row_sits_between_gain_reduction_and_clip();
  if (g_failed) { std::fprintf(stderr, "%d check(s) failed\n", g_failed); return 1; }
  std::puts("test_layout: ok");
  return 0;
}
