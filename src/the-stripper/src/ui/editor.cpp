// The Stripper GUI: the plugin editor. Layout from the approved preview (spec section 4).
#include "ui/editor.h"

#include "controller.h"
#include "ui/model_browser.h"
#include "ui/model_source.h"
#include "ui/model_strip.h"
#include "ui/silkscreen_items.h"
#include "ui/recall_marks.h"
#include "ui/back_content.h"
#include "ui/buttons_tab.h"
#include "fairy_ui/back_view.h"
#include "fairy_ui/back_widgets.h"
#include "ui/recall_marks_view.h"
#include "fairy_ui/screw_view.h"
#include "ui/tape_piece_view.h"
#include "ui/value_text.h"
#include "preferences.h"
#include "ui/tape_kit.h"
#include "ui/panel_view.h"

#include "fairy_ui/controls.h"
#include "fairy_ui/editor_size.h"
#include "fairy_ui/draw_util.h"
#include "fairy_ui/meters.h"
#include "fairy_ui/silkscreen_view.h"
#include "fairy_ui/wear_view.h"
#include "fairy_ui/rack_controls.h"
#include "fairy_ui/sanitize.h"

#include "public.sdk/source/vst/utility/stringconvert.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/controls/ctextedit.h"

#if SMTG_OS_LINUX
#include "public.sdk/source/vst/vstgui_linux_runloop_support.h"
#include "vstgui/lib/platform/linux/linuxfactory.h"
#include "vstgui/lib/platform/platform_x11.h"
#include "vstgui/lib/platform/platformfactory.h"
#endif

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace FairyTapes::TheStripper::Ui {

using namespace VSTGUI;
using FairyTapes::Ui::AnalogKnob;
using FairyTapes::Ui::ChoiceButtons;
using FairyTapes::Ui::ClipLed;
using FairyTapes::Ui::MeterBar;
using FairyTapes::Ui::ScrewView;
using FairyTapes::Ui::generateScrews;
using FairyTapes::Ui::kScrewCount;
using FairyTapes::Ui::MeterKind;
using FairyTapes::Ui::RackLitChoice;
using FairyTapes::Ui::RackKnob;
using FairyTapes::Ui::RackLed;
using FairyTapes::Ui::RackToggle;
using FairyTapes::Ui::generateWear;
using FairyTapes::Ui::WearView;
using FairyTapes::Ui::wearAmount;
using FairyTapes::Ui::NormalizedControl;
using FairyTapes::Ui::ToggleButton;
using Steinberg::Vst::ParamID;

namespace {

constexpr uint32_t kTimerMs = 30;
constexpr CColor kFrameBackground(0x16, 0x16, 0x17);

// The text field of a typed value: Escape cancels, Enter or leaving the field confirms.
class ValueEdit : public CTextEdit {
public:
  using CTextEdit::CTextEdit;
  bool cancelled = false;

protected:
  ~ValueEdit() noexcept override = default;
  void platformOnKeyboardEvent(KeyboardEvent& event) override {
    if (event.virt == VirtualKey::Escape) cancelled = true;
    CTextEdit::platformOnKeyboardEvent(event);
  }
};

CRect toCRect(const Rect& r) { return CRect(r.x, r.y, r.x + r.w, r.y + r.h); }

std::string paramString(Controller* ctl, ParamID id, double norm) {
  Steinberg::Vst::String128 s{};
  if (ctl->getParamStringByValue(id, norm, s) != Steinberg::kResultOk) return {};
  return Steinberg::Vst::StringConvert::convert(s);
}

double defaultNorm(Controller* ctl, ParamID id) {
  Steinberg::Vst::ParameterInfo info{};
  for (Steinberg::int32 i = 0; i < ctl->getParameterCount(); ++i)
    if (ctl->getParameterInfo(i, info) == Steinberg::kResultOk && info.id == id) return info.defaultNormalizedValue;
  return 0.0;
}

} // namespace

Editor::Editor(Controller* controller)
: VSTGUIEditor(nullptr), ctl_(controller) {
  ctl_->addRef();
  for (const auto& c : kControls) specs_[c.id] = &c;
  seen_.fill(std::nan(""));
  const auto px = FairyTapes::Ui::scaledSize(kWidth, kHeight, scale_);
  setRect(Steinberg::ViewRect(0, 0, px.width, px.height));
}

Editor::~Editor() {
  if (frame) close();
  ctl_->release();
}

Steinberg::tresult PLUGIN_API Editor::isPlatformTypeSupported(Steinberg::FIDString type) {
#if SMTG_OS_LINUX
  // X11 embedding only: a Wayland surface would need host-provided Wayland interfaces.
  if (type && std::strcmp(type, Steinberg::kPlatformTypeWaylandSurfaceID) == 0) return Steinberg::kInvalidArgument;
#endif
  return VSTGUIEditor::isPlatformTypeSupported(type);
}

Steinberg::tresult PLUGIN_API Editor::checkSizeConstraint(Steinberg::ViewRect* rect) {
  if (!rect) return Steinberg::kInvalidArgument;
  const auto px = FairyTapes::Ui::scaledSize(kWidth, kHeight, scale_);
  rect->right = rect->left + px.width;
  rect->bottom = rect->top + px.height;
  return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Editor::setContentScaleFactor(ScaleFactor factor) {
#if SMTG_OS_MACOS
  (void)factor;   // Cocoa backing scale handles Retina
  return Steinberg::kResultFalse;
#else
  const double s = FairyTapes::Ui::sanitizeContentScale(factor);
  if (s == scale_) return Steinberg::kResultTrue;
  scale_ = s;
  const auto px = FairyTapes::Ui::scaledSize(kWidth, kHeight, s);
  Steinberg::ViewRect r(rect.left, rect.top, rect.left + px.width, rect.top + px.height);
  setRect(r);   // getSize reports the scaled size even before the view is attached
  if (frame) {
    frame->setZoom(s);   // views keep their logical coordinates
    if (plugFrame) plugFrame->resizeView(this, &r);   // the host answers with onSize
  }
  return Steinberg::kResultTrue;
#endif
}

bool PLUGIN_API Editor::open(void* parent, const PlatformType& platformType) {
  if (frame) return false;

  IPlatformFrameConfig* config = nullptr;
#if SMTG_OS_LINUX
  // VSTGUI's X11 frame needs the host run loop (from the factory host context or the plug frame).
  X11::FrameConfig x11Config;
  auto* linuxFactory = getPlatformFactory().asLinuxFactory();
  if (!linuxFactory) return false;
  if (!linuxFactory->getRunLoop()) Steinberg::Linux::setupVSTGUIRunloop(plugFrame);
  x11Config.runLoop = linuxFactory->getRunLoop();
  if (!x11Config.runLoop) return false;
  config = &x11Config;
#endif

  frame = new CFrame(CRect(0, 0, kWidth, kHeight), this);
  frame->setBackgroundColor(kFrameBackground);
  front_ = new CViewContainer(CRect(0, 0, kWidth, kHeight));
  createViews(front_);
  frame->addView(front_);
  {
    BackActions act;
    act.changePreference = [this](const std::function<void(Preferences&)>& mutate) { changePreference(mutate); };
    act.reroll = [this] {
      ctl_->rerollWear();   // a new seed: new wear, dirt and writing
      placeScrews(false);
      rebuildPieces();
      applyAppearance(prefs_);
    };
    act.manageFolders = [this] { openBrowser(); };
    act.openTone3000 = [this] { openBrowser(true); };
    act.recallModeChanged = [this](int mode) { ctl_->setRecallMode(mode); };
    act.markNow = [this] {
      ctl_->markRecallNow();
      updateRecallMarks();
    };
    FairyTapes::Ui::BackView::Callbacks cb;
    cb.back = [this] { startFlip(false); };
    cb.tabChanged = [this](int tab) { changePreference([tab](Preferences& p) { p.backTab = backTabOfIndex(tab); }); };
    backView_ = new FairyTapes::Ui::BackView(CRect(0, 0, kWidth, kHeight), &skin_, makeBackContent(prefs_, ctl_->recallMode(), act),
                                             std::move(cb), backTabIndex(prefs_.backTab));
    backView_->setCustomDrawer(1, drawButtonsTab);   // the BUTTONS tab
    backView_->setVisible(false);
    frame->addView(backView_);
    flip_ = FairyTapes::Ui::PanelFlip(frame, front_, backView_);
    applyAppearance(prefs_);   // the back's wear
  }
  if (scale_ != 1.0) frame->setZoom(scale_);   // VSTGUIEditor::attached then reports the scaled size
  if (!frame->open(parent, platformType, config)) {
    close();
    return false;
  }
  syncFromController(true);
  timer_ = makeOwned<CVSTGUITimer>([this](CVSTGUITimer*) { onTimer(); }, kTimerMs, true);
  return true;
}

void PLUGIN_API Editor::close() {
  // Timer first: nothing may touch the views once they start going away.
  if (timer_) {
    timer_->stop();
    timer_ = nullptr;
  }
  if (frame) {
    // Removing the views closes any open knob gesture (controlEndEdit -> endEdit).
    frame->removeAll(true);
    if (frame->getNbReference() == 1)
      frame->close();
    else
      frame->forget();
    frame = nullptr;
  }
  controls_.fill(nullptr);
  marks_ = nullptr;
  screws_.fill(nullptr);
  front_ = nullptr;
  flip_.reset();   // the views go away with the frame
  backView_ = nullptr;
  wearView_ = nullptr;
  silk_ = nullptr;
  pieces_.fill(nullptr);
  tapeEdit_ = nullptr;
  tapeEditIndex_ = -1;
  valueEdit_ = nullptr;
  valueEditSpec_ = nullptr;
  strip_ = nullptr;
  modelGpu_ = false;
  browser_ = nullptr;
  browserClosing_ = false;
  modelState_ = -1;
  bars_.fill(nullptr);
  clips_.fill(nullptr);
  seen_.fill(std::nan(""));
}

void Editor::createViews(CViewContainer* parent) {
  parent->addView(new PanelView(CRect(0, 0, kWidth, kHeight), &skin_));
  silk_ = new FairyTapes::Ui::SilkscreenView(CRect(0, 0, kWidth, kHeight), silkscreenItems(), silkscreenTags());   // the printed parts, below the wear and the controls
  parent->addView(silk_);
  // The wear: seeded marks over the silkscreen, below the controls. Level from the user preferences (Off adds nothing).
  // The seed is made when the instance is created and saved with the project (controller state), so the wear is always the same.
  const Preferences prefs = loadPreferences(preferencesPath("TheStripper"));
  tapeValues_ = prefs.valuesFont == ValuesFont::Tape;
  valuesFont_ = tapeFontIdOf(prefs.tapeFont);
  valuesSeed_ = ctl_->wearSeed();
  // The values under the knobs: standard text, or handwriting in the tape font (preference VALUES FONT).
  silk_->setValueDrawer([this](CDrawContext* ctx, const FairyTapes::Ui::PrintText& t, const CPoint& origin, std::size_t item) {
    if (!tapeValues_) return false;
    std::size_t control = 0;   // the variation follows the control (its place in the layout table), not the item
    const int id = silk_->model().item(item).id;
    for (std::size_t i = 0; i < std::size(kControls); ++i)
      if (static_cast<int>(kControls[i].id) == id) { control = i; break; }
    drawTapeText(ctx, valuesFont_, t.text, CPoint(origin.x + t.x, origin.y + t.y), normalizedFontSize(valuesFont_, 10.0),
                 FairyTapes::Ui::kAmber, valuesSeed_ + static_cast<std::uint32_t>(control) * 131u, 0.5, TapeAlign::Center);
    return true;
  });
  front_ = parent;
  prefs_ = prefs;
  wearView_ = new WearView(CRect(0, 0, kWidth, kHeight), {});   // the marks come from applyAppearance
  parent->addView(wearView_);
  marks_ = new RecallMarksView(CRect(0, 0, kWidth, kHeight), ctl_->wearSeed());   // pen marks: over the wear, below the controls
  parent->addView(marks_);
  placeScrews(true);   // the screws: over the wear, below the controls
  rebuildPieces();     // pieces of writable tape: part of the wear layer, a click writes on them

  for (const auto& spec : kControls) {
    if (spec.sharesView) continue;   // its parameter is shown by another entry's view
    NormalizedControl* control = nullptr;
    CRect rect = toCRect(spec.rect);
    const auto tag = static_cast<int32_t>(spec.id);
    if (spec.kind == Kind::Toggle && !isBellButton(spec.id)) rect.bottom += kLedZone;   // room for the LED under the face
    switch (spec.kind) {
    case Kind::Knob: {
      const KnobLook* look = knobLookOf(spec.id);
      auto* knob = new RackKnob(rect, this, tag, look ? look->radius : FairyTapes::Ui::Rack::kDefaultCapRadius, &skin_);
      const ControlSpec* specPtr = &spec;
      knob->onValueClick = [this, specPtr] { editValue(*specPtr); };
      knob->altDoubleClickTarget = [this, specPtr](float& value) {   // Alt + double click: back to the recalled value
        const ParamID id = specPtr->id == kDriveAmount ? driveParam_ : specPtr->id;
        const double cur = std::isnan(seen_[id]) ? 0.0 : seen_[id], rec = ctl_->recallValue(id);
        if (!recallMarkVisible(cur, rec, ctl_->recallValid())) return false;   // no mark showing: plain default
        value = static_cast<float>(rec);
        return true;
      };
      control = knob;
      break;
    }
    case Kind::Toggle:
      control = new RackToggle(rect, this, tag, RackLed::Red, isBellButton(spec.id), &skin_);
      break;
    case Kind::Choice: {
      std::vector<std::string> names;
      for (int k = 0; k < 3 && spec.choices[k]; ++k) names.emplace_back(spec.choices[k]);
      const ChoiceLook* look = choiceLookOf(spec.id);
      control = new RackLitChoice(rect, this, tag, std::move(names), look ? look->columns : 1,
                                  look ? look->faceWidth : FairyTapes::Ui::Rack::kLitFaceWidth, &skin_);
      break;
    }
    }
    control->setDefaultValue(static_cast<float>(uiValue(spec.inverted, defaultNorm(ctl_, spec.id))));
    parent->addView(control);
    controls_[spec.id] = control;
  }

  strip_ = new ModelStrip(toCRect(kModelStrip));
  strip_->setFont(tapeFontIdOf(prefs.tapeFont));
  strip_->onClick = [this] { openBrowser(); };
  parent->addView(strip_);

  for (std::size_t i = 0; i < bars_.size(); ++i) {
    const MeterRow& row = kMeterRows[i];
    bars_[i] = new MeterBar(toCRect(meterBarRect(row)), row.gainReduction ? MeterKind::GainReduction : MeterKind::Level,
                            kMeterLeds);
    parent->addView(bars_[i]);
  }
  for (int i = 0; i < kClipCount; ++i) {
    clips_[i] = new ClipLed(toCRect(clipRect(i)), kClips[i].label);
    clips_[i]->onClick = [this] {
      // One click clears every clip LED of the plugin (hidden writable parameter).
      ctl_->setParamNormalized(kClearClips, 1.0);
      ctl_->setParamNormalized(kClearClips, 0.0);   // re-arm: the next click is a new edit
    };
    parent->addView(clips_[i]);
  }

  // The way to the back of the panel: a small text at the right of the header, like the other silkscreen.
  const auto link = FairyTapes::Ui::frontBackLinkBox();   // the same place as "<- BACK" on the back of the panel
  auto* back = new FairyTapes::Ui::TextLink(CRect(link.x, link.y, link.x + link.w, link.y + link.h), "BACK  \xE2\x86\x92");
  back->onClick = [this] { startFlip(true); };
  parent->addView(back);

  applyAppearance(prefs);   // wear of the panel and the controls, fonts, meter settings
}

// Wear, fonts and meter settings from the user preferences: applied when the editor opens and live from the back panel.
void Editor::applyAppearance(const Preferences& prefs) {
  prefs_ = prefs;
  const std::uint32_t seed = ctl_->wearSeed();
  if (marks_) {
    marks_->setSeed(seed);
    marks_->setVisible(prefs.notes == NotesLevel::Show);   // the notes layer
  }
  const WearLevel level = prefs.wear;
  tapeValues_ = prefs.valuesFont == ValuesFont::Tape;
  valuesFont_ = tapeFontIdOf(prefs.tapeFont);
  valuesSeed_ = seed;
  if (silk_) silk_->invalid();
  if (strip_) strip_->setFont(valuesFont_);
  if (wearView_) wearView_->setMarks(generateWear(seed, level));
  if (backView_) backView_->setWear(generateWear(seed ^ 0x77u, level));

  // wear of the controls (caps and faces), from a per-control seed; same level as the panel
  for (const auto& spec : kControls) {
    if (spec.sharesView) continue;
    NormalizedControl* control = controls_[spec.id];
    if (!control) continue;
    const std::uint32_t cseed = seed ^ static_cast<std::uint32_t>(spec.id * 2654435761u);
    const double amount = wearAmount(level);
    if (auto* k = dynamic_cast<RackKnob*>(control)) k->setWear(cseed, amount);
    else if (auto* t = dynamic_cast<RackToggle*>(control)) t->setWear(cseed, amount);
    else if (auto* c = dynamic_cast<RackLitChoice*>(control)) c->setWear(cseed, amount);
  }

  // pieces of tape: the level decides how many show
  const int shown = piecesShown(level, seed);
  for (std::size_t i = 0; i < pieces_.size(); ++i) {
    if (!pieces_[i]) continue;
    pieces_[i]->setVisible(static_cast<int>(i) < shown);
    pieces_[i]->setFont(valuesFont_);
    pieces_[i]->invalid();
  }

  // peak-hold marker of the level meters
  for (auto* bar : bars_)
    if (bar) bar->setHoldSettings(prefs.meterHoldSeconds, prefs.meterReleaseDbPerSec);
  if (frame) frame->invalid();
}

// The pieces of tape for the current seed (also after a re-roll): between the wear and the controls.
void Editor::rebuildPieces() {
  if (!front_) return;
  for (auto*& p : pieces_) {
    if (p) front_->removeView(p, true);
    p = nullptr;
  }
  const std::uint32_t seed = ctl_->wearSeed();
  const auto pieces = generateTapePieces(seed);
  CView* before = nullptr;   // the pieces go below the first control
  for (const auto& spec : kControls)
    if (!spec.sharesView && controls_[spec.id]) { before = controls_[spec.id]; break; }
  for (int i = 0; i < kTapePieceCount && i < static_cast<int>(pieces_.size()); ++i) {
    auto* view = new TapePieceView(pieces[static_cast<std::size_t>(i)], i, valuesFont_, seed);
    view->setText(displayedTapeText(pieces[static_cast<std::size_t>(i)], ctl_->tapeText(i)));
    view->onClick = [this](TapePieceView* p) { editTape(p); };
    front_->addView(view, before);
    pieces_[static_cast<std::size_t>(i)] = view;
  }
}

// The pen marks: the current value of every knob against the value recalled at the last load or "Mark now".
void Editor::updateRecallMarks() {
  if (!marks_) return;
  marks_->setMarksExist(ctl_->recallValid());
  for (const auto& c : kControls) {
    if (c.kind != Kind::Knob || c.sharesView) continue;
    const ParamID id = c.id == kDriveAmount ? driveParam_ : c.id;   // the Drive knob shows the engine in use
    const double cur = std::isnan(seen_[id]) ? 0.0 : seen_[id];
    marks_->setValues(c.id, cur, ctl_->recallValue(id));
  }
}

void Editor::placeScrews(bool useSavedAngles) {
  if (!front_) return;
  const auto specs = generateScrews(ctl_->wearSeed());
  double saved[4] = {};
  const bool haveSaved = useSavedAngles && ctl_->screwAngles(saved);
  for (int i = 0; i < kScrewCount; ++i) {
    const double angle = haveSaved ? saved[i] : specs[static_cast<std::size_t>(i)].initialAngle;
    if (screws_[static_cast<std::size_t>(i)]) {   // after RE-ROLL: the same views, new screws
      screws_[static_cast<std::size_t>(i)]->setScrew(specs[static_cast<std::size_t>(i)], angle);
      continue;
    }
    auto* view = new ScrewView(specs[static_cast<std::size_t>(i)], angle);
    view->onTurned = [this](double) { screwsTurned(); };   // the end of a drag: the angles are saved with the project
    front_->addView(view);
    screws_[static_cast<std::size_t>(i)] = view;
  }
}

void Editor::screwsTurned() {
  double angles[4];
  for (int i = 0; i < 4; ++i) angles[i] = screws_[static_cast<std::size_t>(i)] ? screws_[static_cast<std::size_t>(i)]->angle() : 0.0;
  ctl_->setScrewAngles(angles);
}

void Editor::changePreference(const std::function<void(Preferences&)>& mutate) {
  const auto file = preferencesPath("TheStripper");
  Preferences p = loadPreferences(file);   // the other settings (for instance the folders) stay as they are on disk
  mutate(p);
  savePreferences(file, p);
  applyAppearance(p);
  if (backView_)
    for (const auto& r : backRowValues(p)) backView_->setSelected(r.key, r.index);
}

// Turns the panel (the common PanelFlip snapshots both sides, shows the turn and then the new side).
void Editor::startFlip(bool toBack) {
  if (!frame || !front_ || !backView_) return;
  closeTapeEdit();
  if (browser_) browserClosing_ = true;
  flip_.start(toBack);
}

void Editor::onTimer() {
  if (flip_.running()) {   // the turn of the panel
    flip_.tick();
    return;
  }
  if (browserClosing_) {   // the popup asked to close from inside its own mouse handler: remove it now
    browserClosing_ = false;
    if (browser_ && frame) frame->removeView(browser_, true);
    browser_ = nullptr;
  }
  if (tapeEdit_ && frame && frame->getFocusView() != tapeEdit_) closeTapeEdit();   // the field lost the focus: done
  if (valueEdit_ && frame && frame->getFocusView() != valueEdit_) closeValueEdit();
  syncFromController(false);
  updateMeters();
  updateModel();
  if (browser_) browser_->poll();
}

void Editor::updateModel() {
  const int state = ctl_->modelState();
  const std::string name = ctl_->modelName(), error = ctl_->modelError();
  const bool gpu = ctl_->modelGpu();
  if (state == modelState_ && name == modelName_ && error == modelError_ && gpu == modelGpu_) return;
  modelGpu_ = gpu;
  modelState_ = state;
  modelName_ = name;
  modelError_ = error;
  if (strip_) {
    using Kind = ModelStrip::Kind;
    if (state == 1) strip_->setModel(Kind::Loading, {});
    else if (state == 2 && !name.empty()) strip_->setModel(Kind::Loaded, displayName(name));
    else if (state == 3) strip_->setModel(Kind::Error, error.empty() ? "Could not load model" : error);
    else strip_->setModel(Kind::NoModel, {});
  }
  refreshModules();
}

// A click on the value under a knob: a text field with the number; Enter or leaving it confirms, Escape cancels.
void Editor::editValue(const ControlSpec& spec) {
  if (!frame || valueEdit_ || tapeEdit_ || browser_ || flip_.running()) return;
  const ParamID id = spec.id == kDriveAmount ? driveParam_ : spec.id;   // the shared Drive knob edits the engine in use
  NormalizedControl* knobView = controls_[spec.id];
  if (!knobView) return;
  const double norm = std::isnan(seen_[id]) ? defaultNorm(ctl_, id) : seen_[id];
  const auto* knob = dynamic_cast<RackKnob*>(knobView);
  if (!knob) return;
  CRect area = knob->valueArea();
  area.inset(-4, -2);
  valueEdit_ = new ValueEdit(area, this, 9100, editFieldText(paramString(ctl_, id, norm)).c_str());
  valueEditSpec_ = specs_[id];
  valueEdit_->setFont(makeOwned<CFontDesc>("Arial", 12, kBoldFace));
  valueEdit_->setFontColor(FairyTapes::Ui::kAmber);
  valueEdit_->setBackColor(CColor(0x05, 0x05, 0x05));
  valueEdit_->setFrameColor(FairyTapes::Ui::kAmber);
  valueEdit_->setHoriAlign(kCenterText);
  frame->addView(valueEdit_);
  frame->setFocusView(valueEdit_);   // starts the editing
}

void Editor::closeValueEdit() {
  if (!valueEdit_) return;
  if (frame) frame->removeView(valueEdit_, true);
  valueEdit_ = nullptr;
  valueEditSpec_ = nullptr;
}

// A value typed in goes the way of a turn of the knob: begin, perform, end (one undoable edit), then the views follow.
void Editor::commitValue(const ControlSpec& spec, double paramNorm) {
  const double v = FairyTapes::Ui::sanitizeNorm(paramNorm, defaultNorm(ctl_, spec.id));
  ctl_->beginEdit(spec.id);
  ctl_->performEdit(spec.id, v);
  ctl_->endEdit(spec.id);
  seen_[spec.id] = ctl_->getParamNormalized(spec.id);
  const ParamID viewId = spec.id == kEqSaturation ? kDriveAmount : spec.id;
  if (NormalizedControl* control = controls_[viewId]) {
    control->setValueNormalized(static_cast<float>(uiValue(spec.inverted, seen_[spec.id])));
    control->invalid();
  }
  showValue(spec, seen_[spec.id]);
}

void Editor::editTape(TapePieceView* piece) {
  if (!frame || !piece || tapeEdit_) return;
  const CRect t = piece->tapeRect();
  CRect box(t);
  box.inset(-4, -3);
  tapeEdit_ = new CTextEdit(box, this, 9000 + piece->index(), piece->text().c_str());
  tapeEditIndex_ = piece->index();
  tapeEdit_->setFont(makeOwned<CFontDesc>("Arial", 13, kBoldFace));
  tapeEdit_->setFontColor(CColor(0x2b, 0x26, 0x20));
  tapeEdit_->setBackColor(CColor(0xd9, 0xcf, 0xa6));
  tapeEdit_->setFrameColor(CColor(0x8f, 0x85, 0x60));
  tapeEdit_->setHoriAlign(kCenterText);
  tapeEdit_->setImmediateTextChange(true);
  frame->addView(tapeEdit_);
  frame->setFocusView(tapeEdit_);   // starts the editing
}

void Editor::closeTapeEdit() {
  if (!tapeEdit_) return;
  if (frame) frame->removeView(tapeEdit_, true);
  tapeEdit_ = nullptr;
  tapeEditIndex_ = -1;
}

void Editor::openBrowser(bool tone3000Tab) {
  if (!frame || browser_) return;
  browser_ = new ModelBrowser(CRect(0, 0, kWidth, kHeight), ctl_->modelPath(), ctl_->tone3000Actions());
  browser_->onLoad = [this](const std::string& path) { ctl_->requestModelLoad(path); };
  browser_->onClose = [this] { browserClosing_ = true; };
  if (tone3000Tab) browser_->showTab(true);
  frame->addView(browser_);
}

// Meter values come from the read-only parameters the processor writes (controller.meterValue()).
void Editor::updateMeters() {
  const auto now = std::chrono::steady_clock::now();
  const double dt = lastTick_.time_since_epoch().count() == 0 ? 0.0 : std::chrono::duration<double>(now - lastTick_).count();
  lastTick_ = now;
  for (std::size_t i = 0; i < bars_.size(); ++i) {
    if (!bars_[i]) continue;
    double v = ctl_->meterValue(kMeterRows[i].id);
    if (kMeterRows[i].gainReduction)
      v = grBarNorm(v, kMeterRows[i].id == kMeterGrComp ? kGrCompMaxDb : kMeterRows[i].id == kMeterGrLimiter ? kGrLimiterMaxDb : kGrGateMaxDb);
    bars_[i]->setLevel(v, dt);
    bars_[i]->setDimmed(meterDimmed(kMeterRows[i].alwaysLit, globalBypass_));
  }
  for (int i = 0; i < kClipCount; ++i) {
    if (!clips_[i]) continue;
    clips_[i]->setLit(ctl_->meterValue(kClips[i].id) >= 0.5);
    clips_[i]->setDimmed(meterDimmed(kClips[i].alwaysLit, globalBypass_));
  }
}

void Editor::syncFromController(bool force) {
  bool changed = force;
  for (ParamID id = 0; id < kNumParams; ++id) {
    const ControlSpec* spec = specs_[id];
    NormalizedControl* control = controls_[id];
    if (!spec) continue;   // (a shared parameter has a spec but no view of its own)
    const double v = FairyTapes::Ui::sanitizeNorm(ctl_->getParamNormalized(id), defaultNorm(ctl_, id));
    if (!force && v == seen_[id]) continue;
    seen_[id] = v;
    changed = true;
    if (control) {
      control->setValueNormalized(static_cast<float>(uiValue(spec->inverted, v)));
      control->invalid();
    }
    if (id != kDriveAmount && id != kEqSaturation) showValue(*spec, v);   // the Drive knob is handled by syncDriveKnob
  }
  if (changed) {
    syncDriveKnob();
    refreshModules();
    updateRecallMarks();
  }
}

void Editor::showValue(const ControlSpec& spec, double paramNorm) {
  if (!silk_ || spec.kind != Kind::Knob) return;
  const ParamID id = spec.id == kEqSaturation ? kDriveAmount : spec.id;   // the shared Drive knob has one printed value
  silk_->setValueText(static_cast<int>(id), paramString(ctl_, spec.id, paramNorm));
}

void Editor::syncDriveKnob() {
  NormalizedControl* knob = controls_[kDriveAmount];
  if (!knob) return;
  const bool namOn = seen_[kDriveNamOn] >= 0.5 || std::isnan(seen_[kDriveNamOn]);
  const Steinberg::Vst::ParamID active = driveEngineParam(namOn);
  driveParam_ = active;
  knob->setTag(static_cast<int32_t>(active));
  knob->setDefaultValue(static_cast<float>(defaultNorm(ctl_, active)));
  const double v = std::isnan(seen_[active]) ? defaultNorm(ctl_, active) : seen_[active];
  knob->setValueNormalized(static_cast<float>(v));
  knob->invalid();
  if (specs_[active]) showValue(*specs_[active], v);
}


// Dimming (module IN off or global bypass) and the Channel-only controls.
void Editor::refreshModules() {
  ModuleState state;
  state.globalBypass = seen_[kBypass] >= 0.5;
  globalBypass_ = state.globalBypass;
  for (const Module m : kModules) {
    const ControlSpec* in = inButtonOf(m);
    if (in && !std::isnan(seen_[in->id])) state.in[static_cast<int>(m)] = uiValue(in->inverted, seen_[in->id]) >= 0.5;
  }
  channelMode_ = isChannelMode(seen_[kStripMode]);
  namOn_ = seen_[kDriveNamOn] >= 0.5 || std::isnan(seen_[kDriveNamOn]);
  if (strip_) {
    strip_->setSaturate(stripShowsSaturate(namOn_));
    stripTip_ = stripTooltip(modelState_, modelName_, modelError_, stripShowsSaturate(namOn_));
    strip_->setTooltipText(stripTip_.c_str());
  }
  if (silk_) {
    const double comp = std::isnan(seen_[kCompressorMode]) ? 0.0 : seen_[kCompressorMode];
    for (const Module m : kModules) {
      silk_->setTag(static_cast<int>(m), blockTag(m, channelMode_, comp, namOn_));
      silk_->setTagDimmed(static_cast<int>(m), moduleDimmed(m, state));
    }
  }

  for (const auto& spec : kControls) {
    NormalizedControl* control = controls_[spec.id];
    if (!control || spec.sharesView) continue;
    bool dim = false;
    if (spec.module != Module::None) dim = spec.isIn ? state.globalBypass : moduleDimmed(spec.module, state);
    if (spec.id == kDriveAmount && driveKnobDimmed(namOn_, modelState_ == 2)) dim = true;   // NAM without a model passes through
    const bool inUse = controlInUse(spec.id, channelMode_);
    if (!inUse) dim = true;
    control->setMouseEnabled(inUse);
    control->setDimmed(dim);
    if (silk_) silk_->setDimmed(static_cast<int>(spec.id), dim);
    const bool visible = visibleInMode(spec.id, channelMode_);
    if (silk_) silk_->setHidden(static_cast<int>(spec.id), !visible);
    if (control->isVisible() != visible) {
      control->setVisible(visible);
      if (frame) frame->invalidRect(control->getViewSize());
    }
  }
}

const ControlSpec* Editor::specOf(CControl* control) const {
  const auto id = static_cast<ParamID>(control->getTag());
  return id < kNumParams ? specs_[id] : nullptr;
}

void Editor::valueChanged(CControl* control) {
  if (control == valueEdit_ && valueEditSpec_) {   // a typed value
    auto* edit = static_cast<ValueEdit*>(valueEdit_);
    if (!edit->cancelled) {
      const ControlSpec& spec = *valueEditSpec_;
      const auto typed = parseTypedNumber(valueEdit_->getText().getString());
      if (typed) {
        Controller* ctl = ctl_;
        const ParamID id = spec.id;
        commitValue(spec, solveNormalized([ctl, id](double v) { return paramString(ctl, id, v); }, *typed));
      }
    }
    return;
  }
  if (control == tapeEdit_) {   // the writing on a piece of tape: at most 14 characters, saved with the project
    const std::string typed = tapeEdit_->getText().getString();
    const std::string clamped = FairyTapes::Ui::sanitizeTapeText(typed);
    if (clamped != typed) tapeEdit_->setText(clamped.c_str());
    ctl_->setTapeText(tapeEditIndex_, clamped);
    if (tapeEditIndex_ >= 0 && tapeEditIndex_ < static_cast<int>(pieces_.size()) && pieces_[static_cast<std::size_t>(tapeEditIndex_)])
      pieces_[static_cast<std::size_t>(tapeEditIndex_)]->setText(clamped);
    return;
  }
  const ControlSpec* spec = specOf(control);
  if (!spec) return;
  const double ui = FairyTapes::Ui::sanitizeNorm(control->getValueNormalized(), control->getDefaultValue());
  const double v = paramValue(spec->inverted, ui);
  ctl_->performEdit(spec->id, v);
  seen_[spec->id] = ctl_->getParamNormalized(spec->id);
  showValue(*spec, seen_[spec->id]);
  if (spec->id == kDriveNamOn) syncDriveKnob();   // the Drive knob changes engine
  refreshModules();
}

void Editor::controlBeginEdit(CControl* control) {
  if (const ControlSpec* spec = specOf(control)) ctl_->beginEdit(spec->id);
}

void Editor::controlEndEdit(CControl* control) {
  if (const ControlSpec* spec = specOf(control)) ctl_->endEdit(spec->id);
}

} // namespace FairyTapes::TheStripper::Ui
