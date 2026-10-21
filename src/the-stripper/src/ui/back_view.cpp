// The Stripper GUI: the back of the panel. Layout from the approved preview (stripper_rack.html).
#include "ui/back_view.h"

#include "ui/back_widgets.h"
#include "ui/browser_widgets.h"
#include "ui/panel_view.h"

#include "fairy_ui/draw_util.h"
#include "fairy_ui/wear_view.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"

#include <algorithm>

namespace FairyTapes::TheStripper::Ui {

using namespace VSTGUI;
using FairyTapes::Ui::drawTitledBox;
using FairyTapes::Ui::kAmber;
using FairyTapes::Ui::kDimWhite;
using FairyTapes::Ui::kWhite;

namespace {

SharedPointer<CFontDesc> bold(CCoord size, bool italic = false) {
  return makeOwned<CFontDesc>("Arial", size, kBoldFace | (italic ? kItalicFace : 0));
}

void text(CDrawContext* ctx, const std::string& s, double x, double baseline, double size, const CColor& colour,
          CHoriTxtAlign align = kLeftText) {
  ctx->setFont(bold(size));
  ctx->setFontColor(colour);
  const CCoord w = ctx->getStringWidth(s.c_str());
  ctx->drawString(s.c_str(), CPoint(align == kRightText ? x - w : align == kCenterText ? x - w / 2 : x, baseline));
}

constexpr BackTab kTabs[] = {BackTab::Settings, BackTab::Buttons, BackTab::Credits};
constexpr double kTabEnd[] = {690, 780, 870};   // right edge of each tab link
constexpr double kBackEnd = 966;

CRect box(double x, double y, double w, double h) { return CRect(x, y, x + w, y + h); }

}  // namespace

BackView::BackView(const CRect& size, const FairyTapes::Ui::Skin* skin, const Preferences& prefs, Callbacks callbacks)
: CViewContainer(size), skin_(skin), cb_(std::move(callbacks)), tab_(prefs.backTab) {
  build(prefs);
  setTab(prefs.backTab);
}

BackView::~BackView() = default;

void BackView::build(const Preferences& prefs) {
  wear_ = new FairyTapes::Ui::WearView(box(0, 0, 1000, 628), {});
  addView(wear_);

  // tabs and the way back
  for (int i = 0; i < 3; ++i) {
    tabLinks_[i] = new TextLink(box(kTabEnd[i] - 90, 62, 90, 22), tabLabel(kTabs[i]));
    tabLinks_[i]->onClick = [this, i] {
      setTab(kTabs[i]);
      choose([i](Preferences& p) { p.backTab = kTabs[i]; });
    };
    addView(tabLinks_[i]);
  }
  auto* back = new TextLink(box(kBackEnd - 90, 62, 90, 22), "\xE2\x86\x90  BACK");
  back->onClick = [this] { if (cb_.back) cb_.back(); };
  addView(back);

  // SETTINGS > APPEARANCE (kept for the user)
  auto add = [&](CView* v) { settingsViews_.push_back(v); addView(v); return v; };
  wearRow_ = new SegmentRow(box(200, 146, 252, 24), {"OFF", "LIGHT", "HEAVY"}, wearIndex(prefs));
  wearRow_->onSelect = [this](int i) { choose([i](Preferences& p) { setWearIndex(p, i); }); };
  add(wearRow_);
  notesRow_ = new SegmentRow(box(200, 194, 168, 24), {"HIDE", "SHOW"}, notesIndex(prefs));
  notesRow_->onSelect = [this](int i) { choose([i](Preferences& p) { setNotesIndex(p, i); }); };
  add(notesRow_);
  fontStepper_ = new FontStepper(box(200, 242, 228, 24), tapeFontIndex(prefs));
  fontStepper_->onStep = [this](int d) {
    const int next = tapeFontStep(fontStepper_->selected(), d);
    fontStepper_->setSelected(next);
    choose([next](Preferences& p) { setTapeFontIndex(p, next); });
  };
  add(fontStepper_);
  valuesRow_ = new SegmentRow(box(200, 290, 236, 24), {"STANDARD", "TAPE FONT"}, valuesFontIndex(prefs));
  valuesRow_->onSelect = [this](int i) { choose([i](Preferences& p) { setValuesFontIndex(p, i); }); };
  add(valuesRow_);
  holdRow_ = new SegmentRow(box(200, 338, 252, 24), {"OFF", "1.5 S", "3 S", "INF"}, holdIndex(prefs));
  holdRow_->onSelect = [this](int i) { choose([i](Preferences& p) { setHoldIndex(p, i); }); };
  add(holdRow_);
  releaseRow_ = new SegmentRow(box(200, 386, 252, 24), {"SLOW", "NORMAL", "FAST"}, releaseIndex(prefs));
  releaseRow_->onSelect = [this](int i) { choose([i](Preferences& p) { setReleaseIndex(p, i); }); };
  add(releaseRow_);
  auto* reroll = new PushButton(box(200, 434, 120, 24), "RE-ROLL");
  reroll->onClick = [this] { if (cb_.reroll) cb_.reroll(); };
  add(reroll);

  // SETTINGS > BEHAVIOUR (saved with the project): what is not available yet is shown dimmed
  auto* inference = new SegmentRow(box(690, 146, 252, 24), {"AUTO", "GPU", "CPU"}, 0);
  inference->setEnabled(false);
  add(inference);
  recallRow_ = new SegmentRow(box(690, 194, 168, 24), {"AUTO", "MANUAL"}, 0);
  recallRow_->onSelect = [this](int i) { if (cb_.recallModeChanged) cb_.recallModeChanged(i); };
  add(recallRow_);
  auto* markNow = new PushButton(box(690, 242, 120, 24), "MARK NOW");
  markNow->onClick = [this] { if (cb_.markNow) cb_.markNow(); };
  add(markNow);
  auto* folders = new PushButton(box(690, 290, 120, 24), "MANAGE...");
  folders->onClick = [this] { if (cb_.manageFolders) cb_.manageFolders(); };
  add(folders);
  auto* tone = new PushButton(box(690, 338, 120, 24), "OPEN...");
  tone->onClick = [this] { if (cb_.openTone3000) cb_.openTone3000(); };
  add(tone);
}

void BackView::choose(const std::function<void(Preferences&)>& mutate) {
  if (cb_.changePreference) cb_.changePreference(mutate);
}

void BackView::setPreferences(const Preferences& prefs) {
  wearRow_->setSelected(wearIndex(prefs));
  notesRow_->setSelected(notesIndex(prefs));
  fontStepper_->setSelected(tapeFontIndex(prefs));
  valuesRow_->setSelected(valuesFontIndex(prefs));
  holdRow_->setSelected(holdIndex(prefs));
  releaseRow_->setSelected(releaseIndex(prefs));
}

void BackView::setRecallMode(int mode) { recallRow_->setSelected(mode == 1 ? 1 : 0); }

void BackView::setWear(std::vector<FairyTapes::Ui::WearMark> marks) { wear_->setMarks(std::move(marks)); }

void BackView::setTab(BackTab tab) {
  tab_ = tab;
  for (int i = 0; i < 3; ++i) tabLinks_[i]->setActive(kTabs[i] == tab);
  for (CView* v : settingsViews_) v->setVisible(tab == BackTab::Settings);
  invalid();
}

CMouseEventResult BackView::onMouseDown(CPoint& where, const CButtonState& buttons) {
  CViewContainer::onMouseDown(where, buttons);
  return kMouseEventHandled;   // the back covers the front: nothing below receives clicks
}

void BackView::drawBackgroundRect(CDrawContext* ctx, const CRect&) {
  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
  const CPoint o = getViewSize().getTopLeft();
  drawPanelBase(ctx, getViewSize(), skin_);

  // header
  ctx->setFont(bold(38, true));
  ctx->setFontColor(kWhite);
  ctx->drawString(tabLabel(tab_), CPoint(o.x + 34, o.y + 58));
  text(ctx, "FAIRY TAPES", o.x + 36, o.y + 78, 11, kDimWhite);
  text(ctx, "THE STRIPPER  \xC2\xB7  CHANNEL STRIP VST3", o.x + 966, o.y + 46, 13, kWhite, kRightText);

  if (tab_ == BackTab::Settings) drawSettings(ctx);
  else if (tab_ == BackTab::Buttons) drawButtons(ctx);
  else drawCredits(ctx);
  ctx->restoreGlobalState();
}

void BackView::drawSettings(CDrawContext* ctx) {
  drawTitledBox(ctx, box(24, 102, 462, 364), "APPEARANCE", true);
  text(ctx, "saved for you, in every project", 255, 130, 10, kDimWhite, kCenterText);
  auto row = [&](const char* label, double y, const char* hint, double x0 = 50) {
    text(ctx, label, x0, y + 16, 13, kWhite);
    if (hint && *hint) text(ctx, hint, x0, y + 32, 10, kDimWhite);
  };
  row("WEAR", 146, "");
  row("NOTES LAYER", 194, "tape and pen marks on the values");
  row("TAPE FONT", 242, "handwriting on the tape");
  row("VALUES FONT", 290, "values under the knobs");
  row("METER HOLD", 338, "peak marker of the level meters");
  row("METER RELEASE", 386, "fall of the peak marker");
  row("LOOK", 434, "");
  text(ctx, "new wear, dirt and writing", 332, 450, 10, kDimWhite);

  drawTitledBox(ctx, box(514, 102, 462, 364), "BEHAVIOUR", true);
  text(ctx, "saved with the project", 745, 130, 10, kDimWhite, kCenterText);
  row("NAM INFERENCE", 146, "needs the GPU build", 540);
  row("RECALL MARKS", 194, "auto: set at every load", 540);
  row("MARK NOW", 242, "set the marks to the current values", 540);
  row("MODEL FOLDERS", 290, "where the picker looks for models", 540);
  row("TONE3000", 338, "sign in and download models", 540);

  drawTitledBox(ctx, box(24, 484, 952, 120), "NOTES", true);
  text(ctx, "Appearance settings belong to you: they are kept in your preferences and apply to every project and every instance.", 50, 522, 12, kDimWhite);
  text(ctx, "Behaviour settings change how the plugin works: they are saved in the project, together with the parameters.", 50, 544, 12, kDimWhite);
  text(ctx, "The wear (scratches, tape, screws) is made once per instance from a saved seed and never changes by itself.", 50, 566, 12, kDimWhite);
  text(ctx, "Double-click on a knob returns it to its default; Alt + double-click returns it to the recalled value (the blue mark).", 50, 588, 12, kDimWhite);
}

void BackView::drawButtons(CDrawContext* ctx) {
  drawTitledBox(ctx, box(24, 102, 952, 508), "WHAT THE BUTTONS DO", true);
  double y = 142;
  const double textX = 270, textWidth = 676;
  for (const HelpEntry& e : helpEntries()) {
    ctx->setFont(bold(11));
    auto measure = [ctx](const std::string& s) { return static_cast<double>(ctx->getStringWidth(s.c_str())); };
    const auto what = wrapText(e.what, textWidth, [&](const std::string& s) { ctx->setFont(bold(12)); return measure(s); });
    ctx->setFont(bold(11));
    const auto imposes = wrapText(e.imposes, textWidth, measure);
    text(ctx, e.title, 50, y, 13, kAmber);
    double ly = y;
    for (const auto& l : what) { text(ctx, l, textX, ly, 12, kWhite); ly += 14; }
    for (const auto& l : imposes) { text(ctx, l, textX, ly, 11, kDimWhite); ly += 13; }
    y = std::max(ly, y + 16) + 12;
  }
}

void BackView::drawCredits(CDrawContext* ctx) {
  drawTitledBox(ctx, box(24, 102, 952, 100), "THE STRIPPER", true);
  text(ctx, std::string("Version ") + kPluginVersion + "  \xC2\xB7  Fairy Tapes  \xC2\xB7  github.com/fairy-tapes", 50, 136, 14, kWhite);
  text(ctx, kLicenseLine, 50, 158, 12, kDimWhite);
  text(ctx, kLicenseLine2, 50, 176, 12, kDimWhite);
  text(ctx, kLicenseLine3, 50, 194, 12, kDimWhite);

  drawTitledBox(ctx, box(24, 220, 952, 292), "COMPONENTS AND LICENSES", true);
  ctx->setFrameColor(CColor(kDimWhite.red, kDimWhite.green, kDimWhite.blue, 64));
  ctx->setLineWidth(1);
  for (int i = 0; i < kCreditCount; ++i) {
    const double y = 252 + i * 20;
    text(ctx, kCredits[i].component, 50, y, 13, kWhite);
    text(ctx, kCredits[i].author, 350, y, 12, kDimWhite);
    text(ctx, kCredits[i].license, 950, y, 12, kAmber, kRightText);
    if (i < kCreditCount - 1) ctx->drawLine(CPoint(50, y + 6), CPoint(950, y + 6));
  }

  drawTitledBox(ctx, box(24, 530, 952, 80), "NOTICES", true);
  double y = 556;
  for (const char* n : kNotices) {
    text(ctx, n, 50, y, 12, kDimWhite);
    y += 18;
  }
}

} // namespace FairyTapes::TheStripper::Ui
