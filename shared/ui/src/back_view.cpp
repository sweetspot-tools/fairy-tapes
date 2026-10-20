// Fairy Tapes shared UI: the back of the panel. Layout from the approved previews (stripper_rack.html, pillow_rack.html).
#include "fairy_ui/back_view.h"

#include "fairy_ui/back_widgets.h"
#include "fairy_ui/draw_util.h"
#include "fairy_ui/wear_view.h"

#include "vstgui/lib/cdrawcontext.h"

namespace FairyTapes::Ui {

using namespace VSTGUI;

namespace {
CRect toCRect(const BackBox& b) { return CRect(b.x, b.y, b.x + b.w, b.y + b.h); }
SharedPointer<CFontDesc> bold(CCoord size, int32_t extra = 0) { return makeOwned<CFontDesc>("Arial", size, kBoldFace | extra); }
constexpr const char* kBrand = "FAIRY TAPES";
constexpr const char* kBackText = "\xE2\x86\x90  BACK";
constexpr const char* kAppearanceSubtitle = "saved for you, in every project";
constexpr const char* kBehaviourSubtitle = "saved with the project";
}  // namespace

BackView::BackView(const CRect& size, const Skin* skin, BackContent content, Callbacks callbacks, int tab)
: CViewContainer(size), skin_(skin), content_(std::move(content)), cb_(std::move(callbacks)) {
  setBackgroundColor(kTransparentCColor);
  titleFont_ = bold(Back::kTitleSize, kItalicFace);
  f10_ = bold(10);
  f11_ = bold(11);
  f12_ = bold(12);
  f13_ = bold(13);
  f14_ = bold(14);
  for (BackItem& it : content_.items)
    if (it.kind != BackItemKind::Button) it.selected = clampIndex(it.selected, static_cast<int>(it.options.size()));
  build();
  setTab(tab);
}

BackView::~BackView() = default;

void BackView::build() {
  wear_ = new WearView(CRect(0, 0, getViewSize().getWidth(), getViewSize().getHeight()), {});
  addView(wear_);

  // tab links and the way back
  const int n = static_cast<int>(content_.tabs.size());
  for (int i = 0; i < n; ++i) {
    auto* link = new TextLink(toCRect(tabLinkBox(i, n)), content_.tabs[static_cast<std::size_t>(i)].label);
    link->onClick = [this, i] {
      setTab(i);
      if (cb_.tabChanged) cb_.tabChanged(i);
    };
    addView(link);
    tabLinks_.push_back(link);
  }
  auto* back = new TextLink(toCRect(backLinkBox()), kBackText);
  back->onClick = [this] {
    if (cb_.back) cb_.back();
  };
  addView(back);

  // SETTINGS: one widget per item, in its row
  for (std::size_t i = 0; i < content_.items.size(); ++i) {
    BackItem& it = content_.items[i];
    const CRect r = toCRect(itemControlBox(it, itemRow(content_.items, i)));
    CView* view = nullptr;
    switch (it.kind) {
    case BackItemKind::Choice: {
      auto* row = new SegmentRow(r, it.options, it.selected);
      row->setEnabled(it.enabled);
      row->onSelect = [this, i](int index) {
        BackItem& item = content_.items[i];
        item.selected = index;
        if (item.onChange) item.onChange(index);
      };
      view = row;
      break;
    }
    case BackItemKind::Stepper: {
      auto* stepper = new OptionStepper(r, it.options, it.selected, it.tapeFontOptions);
      stepper->setEnabled(it.enabled);
      stepper->onStep = [this, i, stepper](int delta) {
        BackItem& item = content_.items[i];
        item.selected = wrapIndex(item.selected, delta, static_cast<int>(item.options.size()));
        stepper->setSelected(item.selected);
        if (item.onChange) item.onChange(item.selected);
      };
      view = stepper;
      break;
    }
    case BackItemKind::Button: {
      auto* button = new PushButton(r, it.options.empty() ? std::string() : it.options.front());
      button->setEnabled(it.enabled);
      button->onClick = [this, i] {
        if (content_.items[i].onPress) content_.items[i].onPress();
      };
      view = button;
      break;
    }
    }
    addView(view);
    itemViews_.push_back(view);
  }
}

void BackView::setSelected(int key, int index) {
  const int i = findItem(content_, key);
  if (i < 0) return;
  BackItem& it = content_.items[static_cast<std::size_t>(i)];
  if (it.kind == BackItemKind::Button) return;
  it.selected = clampIndex(index, static_cast<int>(it.options.size()));
  CView* v = itemViews_[static_cast<std::size_t>(i)];
  if (auto* row = dynamic_cast<SegmentRow*>(v)) row->setSelected(it.selected);
  else if (auto* stepper = dynamic_cast<OptionStepper*>(v)) stepper->setSelected(it.selected);
}

int BackView::selected(int key) const {
  const int i = findItem(content_, key);
  return i < 0 ? -1 : content_.items[static_cast<std::size_t>(i)].selected;
}

void BackView::setItemEnabled(int key, bool enabled) {
  const int i = findItem(content_, key);
  if (i < 0) return;
  content_.items[static_cast<std::size_t>(i)].enabled = enabled;
  CView* v = itemViews_[static_cast<std::size_t>(i)];
  if (auto* row = dynamic_cast<SegmentRow*>(v)) row->setEnabled(enabled);
  else if (auto* stepper = dynamic_cast<OptionStepper*>(v)) stepper->setEnabled(enabled);
  else if (auto* button = dynamic_cast<PushButton*>(v)) button->setEnabled(enabled);
}

BackTabKind BackView::tabKind() const {
  return tab_ >= 0 && tab_ < static_cast<int>(content_.tabs.size()) ? content_.tabs[static_cast<std::size_t>(tab_)].kind
                                                                      : BackTabKind::Settings;
}

void BackView::setTab(int index) {
  tab_ = clampIndex(index, static_cast<int>(content_.tabs.size()));
  for (std::size_t i = 0; i < tabLinks_.size(); ++i) tabLinks_[i]->setActive(static_cast<int>(i) == tab_);
  const bool settings = tabKind() == BackTabKind::Settings;
  for (CView* v : itemViews_) v->setVisible(settings);
  invalid();
}

void BackView::setWear(std::vector<WearMark> marks) {
  if (wear_) wear_->setMarks(std::move(marks));
}

void BackView::setCustomDrawer(int tab, CustomDrawer drawer) {
  customDrawers_[tab] = std::move(drawer);
  invalid();
}

CMouseEventResult BackView::onMouseDown(CPoint& where, const CButtonState& buttons) {
  CViewContainer::onMouseDown(where, buttons);
  return kMouseEventHandled;   // the back covers the front: nothing below receives clicks
}

void BackView::text(CDrawContext* ctx, const std::string& s, double x, double baseline, CFontDesc* font, const CColor& colour,
                    int align) {
  if (s.empty()) return;
  ctx->setFont(font);
  ctx->setFontColor(colour);
  const CCoord w = ctx->getStringWidth(s.c_str());
  ctx->drawString(s.c_str(), CPoint(align == 2 ? x - w : align == 1 ? x - w / 2 : x, baseline));
}

void BackView::drawBackgroundRect(CDrawContext* ctx, const CRect&) {
  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
  const CPoint o = getViewSize().getTopLeft();
  drawPanelBase(ctx, getViewSize(), skin_);

  // header: the current tab as the big title, the brand under it, the plug-in line at the top right
  const std::string title =
      tab_ >= 0 && tab_ < static_cast<int>(content_.tabs.size()) ? content_.tabs[static_cast<std::size_t>(tab_)].label : std::string();
  text(ctx, title, o.x + Back::kTitleX, o.y + Back::kTitleBaseline, titleFont_, kWhite);
  text(ctx, kBrand, o.x + Back::kBrandX, o.y + Back::kBrandBaseline, f11_, kDimWhite);
  text(ctx, content_.headerLine, o.x + Back::kHeaderRight, o.y + Back::kHeaderBaseline, f13_, kWhite, 2);

  switch (tabKind()) {
  case BackTabKind::Settings: drawSettings(ctx, o); break;
  case BackTabKind::Credits: drawCredits(ctx, o); break;
  case BackTabKind::Custom: {
    auto it = customDrawers_.find(tab_);
    if (it != customDrawers_.end() && it->second) {
      ctx->saveGlobalState();
      CRect area = toCRect(Back::kCustomBox);
      area.offset(o.x, o.y);
      it->second(ctx, area);
      ctx->restoreGlobalState();
    }
    break;
  }
  }
  ctx->restoreGlobalState();
}

void BackView::drawSettings(CDrawContext* ctx, const CPoint& o) {
  auto box = [&](const BackBox& b) { CRect r = toCRect(b); r.offset(o.x, o.y); return r; };
  drawTitledBox(ctx, box(Back::kAppearanceBox), "APPEARANCE", true);
  drawTitledBox(ctx, box(Back::kBehaviourBox), "BEHAVIOUR", true);
  const BackBox& a = Back::kAppearanceBox;
  const BackBox& b = Back::kBehaviourBox;
  text(ctx, kAppearanceSubtitle, o.x + a.x + a.w / 2, o.y + Back::kSubtitleBaseline, f10_, kDimWhite, 1);
  text(ctx, kBehaviourSubtitle, o.x + b.x + b.w / 2, o.y + Back::kSubtitleBaseline, f10_, kDimWhite, 1);

  for (std::size_t i = 0; i < content_.items.size(); ++i) {
    const BackItem& it = content_.items[i];
    const int row = itemRow(content_.items, i);
    const double y = itemRowY(row);
    const double x0 = labelX(it.column);
    text(ctx, it.label, o.x + x0, o.y + y + 16, f13_, kWhite);
    text(ctx, it.hint, o.x + x0, o.y + y + 32, f10_, kDimWhite);
    if (!it.note.empty()) {
      const BackBox c = itemControlBox(it, row);
      text(ctx, it.note, o.x + c.x + c.w + Back::kNoteGap, o.y + y + 16, f10_, kDimWhite);
    }
  }

  drawTitledBox(ctx, box(Back::kNotesBox), "NOTES", true);
  for (std::size_t i = 0; i < content_.notes.size() && static_cast<int>(i) < Back::kMaxNotes; ++i)
    text(ctx, content_.notes[i], o.x + Back::kNotesBox.x + Back::kLabelInset,
         o.y + Back::kNotesFirstBaseline + Back::kNotesStep * static_cast<double>(i), f12_, kDimWhite);
}

void BackView::drawCredits(CDrawContext* ctx, const CPoint& o) {
  auto box = [&](const BackBox& b) { CRect r = toCRect(b); r.offset(o.x, o.y); return r; };
  const BackCredits& c = content_.credits;
  const double x = o.x + Back::kCreditNameX;
  drawTitledBox(ctx, box(Back::kCreditsTitleBox), c.title.c_str(), true);
  text(ctx, c.versionLine, x, o.y + Back::kVersionBaseline, f14_, kWhite);
  for (std::size_t i = 0; i < c.licenseLines.size() && static_cast<int>(i) < Back::kMaxLicenseLines; ++i)
    text(ctx, c.licenseLines[i], x, o.y + Back::kLicenseFirstBaseline + Back::kLicenseStep * static_cast<double>(i), f12_, kDimWhite);

  drawTitledBox(ctx, box(Back::kComponentsBox), "COMPONENTS AND LICENSES", true);
  const int n = static_cast<int>(c.entries.size());
  const bool compact = creditRowStep(n) < 24;   // many entries: smaller text so the rows stay apart
  ctx->setLineWidth(1);
  for (int i = 0; i < n; ++i) {
    const CreditEntry& e = c.entries[static_cast<std::size_t>(i)];
    const double y = o.y + creditBaseline(i, n);
    text(ctx, e.name, x, y, compact ? f13_ : f14_, kWhite);
    text(ctx, e.author, o.x + Back::kCreditAuthorX, y, compact ? f12_ : f13_, kDimWhite);
    text(ctx, e.license, o.x + Back::kCreditLicenseRight, y, compact ? f12_ : f13_, kAmber, 2);
    if (i < n - 1) {
      const double ly = y + creditRowStep(n) * 0.4;
      ctx->setFrameColor(CColor(kDimWhite.red, kDimWhite.green, kDimWhite.blue, 64));
      ctx->drawLine(CPoint(x, ly), CPoint(o.x + Back::kCreditLicenseRight, ly));
    }
  }

  drawTitledBox(ctx, box(Back::kNoticesBox), "NOTICES", true);
  for (std::size_t i = 0; i < c.notices.size() && static_cast<int>(i) < Back::kMaxNotices; ++i)
    text(ctx, c.notices[i], x, o.y + Back::kNoticesFirstBaseline + Back::kNoticesStep * static_cast<double>(i), f12_, kDimWhite);
}

} // namespace FairyTapes::Ui
