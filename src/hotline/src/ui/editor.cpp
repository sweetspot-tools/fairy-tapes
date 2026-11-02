// Hotline GUI: the plugin editor. Layout from the approved rack preview (docs/mockups/hotline_rack.html, ui/layout.h).
#include "ui/editor.h"

#include "controller.h"
#include "state.h"
#include "ui/back_settings.h"
#include "ui/decor.h"
#include "ui/devices_tab.h"
#include "ui/panel_view.h"
#include "ui/prefs.h"
#include "ui/screw_angles.h"
#include "ui/tape_view.h"
#include "ui/tapes.h"

#include "fairy_ui/back_view.h"
#include "fairy_ui/back_widgets.h"
#include "fairy_ui/draw_util.h"
#include "fairy_ui/meters.h"
#include "fairy_ui/rack_controls.h"
#include "fairy_ui/sanitize.h"
#include "fairy_ui/screw_view.h"
#include "fairy_ui/silkscreen_view.h"
#include "fairy_ui/wear_view.h"

#include "base/source/updatehandler.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/ctextedit.h"
#include "vstgui/lib/cviewcontainer.h"

#if SMTG_OS_LINUX
#include "public.sdk/source/vst/vstgui_linux_runloop_support.h"
#include "vstgui/lib/platform/linux/linuxfactory.h"
#include "vstgui/lib/platform/platform_x11.h"
#include "vstgui/lib/platform/platformfactory.h"
#endif

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace FairyTapes::Hotline::Ui {

using namespace VSTGUI;
using FairyTapes::Ui::BackContent;
using FairyTapes::Ui::BackView;
using FairyTapes::Ui::ClipLed;
using FairyTapes::Ui::MeterBar;
using FairyTapes::Ui::MeterKind;
using FairyTapes::Ui::NormalizedControl;
using FairyTapes::Ui::RackKnob;
using FairyTapes::Ui::RackLed;
using FairyTapes::Ui::RackLitChoice;
using FairyTapes::Ui::RackToggle;
using FairyTapes::Ui::ScrewView;
using FairyTapes::Ui::SilkscreenView;
using FairyTapes::Ui::TextLink;
using FairyTapes::Ui::WearLevel;
using FairyTapes::Ui::WearMark;
using FairyTapes::Ui::WearView;
using Steinberg::Vst::ParamID;

namespace {

static_assert(kNumParams <= 32, "pending_ holds one bit per persistent parameter");
static_assert(kMaxTapeTexts == kNumTapeSlots, "one stored tape text per tape slot");
static_assert(kNumScrews == static_cast<int>(sizeof(kScrewPos) / sizeof(kScrewPos[0])), "one stored angle per screw");
static_assert(Editor::kNumScrewViews == kNumScrews, "one view per stored screw angle");

constexpr uint32_t kTimerMs = 30;
constexpr double kTimerSeconds = kTimerMs / 1000.0;
constexpr CColor kFrameBackground(0x16, 0x16, 0x17);
constexpr uint32_t kBackWearSalt = 0x77u;   // the back's wear: another pattern from the same seed (as The Stripper)
constexpr int32_t kTapeEditTag = 9000;   // + slot; outside the parameter ids
constexpr CCoord kTapeEditMinWidth = 120, kTapeEditHeight = 24;

// The text field of a tape (the pattern of Pillow Talk's and The Stripper's): Escape cancels, Enter or leaving the field
// confirms. CTextEdit itself would still report the typed text when Escape comes through the platform field.
class TapeEdit : public CTextEdit {
public:
  using CTextEdit::CTextEdit;
  bool cancelled = false;

  void onKeyboardEvent(KeyboardEvent& event) override {
    if (event.type == EventType::KeyDown && event.virt == VirtualKey::Escape) cancelled = true;
    CTextEdit::onKeyboardEvent(event);
  }

protected:
  ~TapeEdit() noexcept override = default;
  void platformOnKeyboardEvent(KeyboardEvent& event) override {
    if (event.type == EventType::KeyDown && event.virt == VirtualKey::Escape) cancelled = true;
    CTextEdit::platformOnKeyboardEvent(event);
  }
};

constexpr uint32_t bit(ParamID id) { return 1u << id; }

CRect toCRect(const Rect& r) { return CRect(r.x, r.y, r.x + r.w, r.y + r.h); }

double controllerNorm(Steinberg::Vst::EditController* ctl, ParamID id, double fallback) {
  return FairyTapes::Ui::sanitizeNorm(ctl->getParamNormalized(id), fallback);
}

} // namespace

Editor::Editor(Steinberg::Vst::EditController* controller)
: VSTGUIEditor(controller), owner_(dynamic_cast<Controller*>(controller)) {
  const PixelSize px = scaledEditorSize(scale_);
  setRect(Steinberg::ViewRect(0, 0, px.width, px.height));
  // Parameter dependents need the global update handler (VST3Editor creates it the same way).
  Steinberg::UpdateHandler::instance();
}

Editor::~Editor() {
  if (frame) close();
  unwatchParameters();
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
  const PixelSize px = scaledEditorSize(scale_);
  rect->right = rect->left + px.width;
  rect->bottom = rect->top + px.height;
  return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Editor::setContentScaleFactor(ScaleFactor factor) {
#if SMTG_OS_MACOS
  (void)factor;  // Cocoa backing scale handles Retina; VST3Editor keeps 1 there as well
  return Steinberg::kResultFalse;
#else
  const double s = sanitizeContentScale(factor);
  if (s == scale_) return Steinberg::kResultTrue;
  scale_ = s;
  const PixelSize px = scaledEditorSize(s);
  Steinberg::ViewRect r(rect.left, rect.top, rect.left + px.width, rect.top + px.height);
  setRect(r);  // getSize reports the scaled size even before the view is attached
  if (frame) {
    frame->setZoom(s);  // views keep their logical coordinates
    if (plugFrame) plugFrame->resizeView(this, &r);  // the host answers with onSize
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

  // Appearance from the user preferences (missing or bad file: defaults), seed, screws and tape texts from the decor.
  try {
    prefs_ = loadPrefs(prefsPath());
  } catch (...) {
    prefs_ = Prefs{};
  }
  wearLevel_ = prefs_.wear;
  tapeFont_ = tapeFontIdOf(prefs_.tapeFont);
  refreshDecor();

  frame = new CFrame(CRect(0, 0, kWidth, kHeight), this);
  frame->setBackgroundColor(kFrameBackground);
  // The two sides of the panel: the front (controls) and the back (settings, devices, routing, credits), turned by flip_.
  front_ = new CViewContainer(CRect(0, 0, kWidth, kHeight));
  front_->setBackgroundColor(kFrameBackground);
  createViews(front_);
  frame->addView(front_);
  buildBack(frame);
  flip_ = FairyTapes::Ui::PanelFlip(frame, front_, back_, scale_);
  if (scale_ != 1.0) frame->setZoom(scale_);  // VSTGUIEditor::attached then reports the scaled size
  if (!frame->open(parent, platformType, config)) {
    close();
    return false;
  }
  watchParameters();
  timer_ = makeOwned<CVSTGUITimer>([this](CVSTGUITimer*) { onTimer(); }, kTimerMs, true);
  return true;
}

void PLUGIN_API Editor::close() {
  // Timer first: nothing may touch the views once they start going away.
  if (timer_) {
    timer_->stop();
    timer_ = nullptr;
  }
  unwatchParameters();
  closeTapeEdit(true);   // what was typed is kept, as when the field loses the focus
  flip_.reset();         // a running turn: its view goes away with the frame's
  if (frame) {
    // Removing the views closes any open knob gesture (controlEndEdit -> endEdit).
    frame->removeAll(true);
    if (frame->getNbReference() == 1)
      frame->close();
    else
      frame->forget();
    frame = nullptr;
  }
  resetViewPointers();
}

void Editor::resetViewPointers() {
  controls_.fill(nullptr);
  bars_.fill(nullptr);
  clips_.fill(nullptr);
  silk_ = nullptr;
  wear_ = nullptr;
  root_ = nullptr;
  tapes_.clear();
  screws_.fill(nullptr);
  backScrews_.fill(nullptr);
  tapeEdit_ = nullptr;
  tapeEditSlot_ = -1;
  front_ = nullptr;
  back_ = nullptr;
  backLink_ = nullptr;
}

const std::vector<WearMark>& Editor::wearMarks(uint32_t seed, WearLevel level) {
  if (!marksValid_ || seed != marksSeed_ || level != marksLevel_) {
    marks_ = panelWear(seed, level);
    marksSeed_ = seed;
    marksLevel_ = level;
    marksValid_ = true;
  }
  return marks_;
}

void Editor::refreshDecor() {
  if (!owner_) return;
  const uint32_t rev = owner_->decorRevision();
  if (decorKnown_ && rev == decorRevision_) return;
  const Decor d = owner_->decor();   // generates the seed if the controller has none yet
  decorRevision_ = owner_->decorRevision();
  decorKnown_ = true;
  const bool textsChanged = d.tapeTexts != tapeTexts_;
  tapeTexts_ = d.tapeTexts;
  for (int i = 0; i < kNumScrewViews; ++i) screwAngles_[static_cast<std::size_t>(i)] = d.screwAngles[i];
  if (d.seed != 0 && d.seed != seed_) {
    seed_ = d.seed;
    applyWear();   // new marks, control wear and tapes (with the new texts)
  } else if (textsChanged) {
    refreshTapeTexts();
  }
  placeScrews();   // new seed or rotations (state recall, undo)
}

void Editor::setWearLevel(WearLevel level) {
  if (level == wearLevel_) return;
  wearLevel_ = level;
  applyWear();
}

void Editor::applyWear() {
  if (wear_) wear_->setMarks(wearMarks(seed_, wearLevel_));
  applyBackWear();
  applyControlWear();
  rebuildTapes();
}

void Editor::applyBackWear() {
  if (back_) back_->setWear(FairyTapes::Ui::generateWear(seed_ ^ kBackWearSalt, wearLevel_, kWidth, kHeight));
}

// Caps and button faces: a per-control seed from the instance seed, the same amount as the panel (Off/Light/Heavy).
void Editor::applyControlWear() {
  const double amount = FairyTapes::Ui::wearAmount(wearLevel_);
  for (const ControlSpec& spec : kControls) {
    NormalizedControl* control = controls_[spec.id];
    if (!control) continue;
    const uint32_t cseed = controlWearSeed(seed_, spec.id);
    if (auto* k = dynamic_cast<RackKnob*>(control)) k->setWear(cseed, amount);
    else if (auto* t = dynamic_cast<RackToggle*>(control)) t->setWear(cseed, amount);
    else if (auto* c = dynamic_cast<RackLitChoice*>(control)) c->setWear(cseed, amount);
    control->invalid();
  }
}

void Editor::setTapeFont(FairyTapes::Ui::TapeFontId font) {
  tapeFont_ = font;
  for (TapeView* t : tapes_) t->setFont(font);
}

void Editor::rebuildTapes() {
  if (!root_) return;
  closeTapeEdit(true);
  for (TapeView* t : tapes_) root_->removeView(t, true);
  tapes_.clear();
  const WearLayout layout =
      layoutFromSeed(seed_, wearLevel_, Rect{0.f, 0.f, static_cast<float>(kWidth), static_cast<float>(kHeight)});
  CView* firstControl = controls_[kControls[0].id];
  for (const TapePlacement& p : layout.tapes) {
    auto* view = new TapeView(p, tapeFont_, seed_);
    view->setText(p.slot < static_cast<int>(tapeTexts_.size()) ? tapeTexts_[static_cast<std::size_t>(p.slot)] : std::string());
    view->onClick = [this](TapeView* t) { editTape(t); };
    // above the wear, below the screws and the controls
    if (CView* above = screws_[0] ? static_cast<CView*>(screws_[0]) : firstControl) root_->addView(view, above);
    else root_->addView(view);
    tapes_.push_back(view);
  }
}

void Editor::refreshTapeTexts() {
  for (TapeView* t : tapes_) {
    const int slot = t->slot();
    t->setText(slot < static_cast<int>(tapeTexts_.size()) ? tapeTexts_[static_cast<std::size_t>(slot)] : std::string());
  }
}

// A click on a tape: a text field over it with what is written; Enter or leaving it confirms, Escape cancels.
void Editor::editTape(TapeView* tape) {
  if (!frame || !tape || tapeEdit_) return;
  CRect box = tape->tapeRect();
  const CPoint c = box.getCenter();
  const CCoord w = std::max(box.getWidth() + 8, kTapeEditMinWidth);
  box = CRect(c.x - w / 2, c.y - kTapeEditHeight / 2, c.x + w / 2, c.y + kTapeEditHeight / 2);
  if (box.left < 0) box.offset(-box.left, 0);   // stays in the window
  if (box.right > kWidth) box.offset(kWidth - box.right, 0);
  tapeEdit_ = new TapeEdit(box, this, kTapeEditTag + tape->slot(), tape->text().c_str());
  tapeEditSlot_ = tape->slot();
  tapeEdit_->setFont(makeOwned<CFontDesc>("Arial", 13, kBoldFace));
  tapeEdit_->setFontColor(CColor(0x2b, 0x26, 0x20));
  tapeEdit_->setBackColor(CColor(0xd9, 0xcf, 0xa6));
  tapeEdit_->setFrameColor(CColor(0x8f, 0x85, 0x60));
  tapeEdit_->setHoriAlign(kCenterText);
  frame->addView(tapeEdit_);
  frame->setFocusView(tapeEdit_);   // starts the editing
}

void Editor::commitTapeText(int slot, const std::string& typed) {
  if (!owner_) return;
  const Decor d = withTapeText(owner_->decor(), slot, typed);
  tapeTexts_ = d.tapeTexts;
  owner_->editDecor(d);
  decorRevision_ = owner_->decorRevision();
  refreshTapeTexts();
}

void Editor::closeTapeEdit(bool confirm) {
  if (!tapeEdit_) return;
  auto* edit = static_cast<TapeEdit*>(tapeEdit_);
  if (frame && frame->getFocusView() == tapeEdit_) {
    if (!confirm) edit->cancelled = true;
    frame->setFocusView(nullptr);   // the field reports what was typed (valueChanged) unless cancelled
  }
  if (frame) frame->removeView(tapeEdit_, true);
  tapeEdit_ = nullptr;
  tapeEditSlot_ = -1;
}

void Editor::placeScrews() {
  if (!root_) return;
  const auto specs = screwSpecs(seed_);
  CView* firstControl = controls_[kControls[0].id];
  for (int i = 0; i < kNumScrewViews; ++i) {
    const auto k = static_cast<std::size_t>(i);
    const double shown = shownScrewAngle(specs[k].initialAngle, screwAngles_[k]);
    if (ScrewView* v = screws_[k]) {
      if (v->spec().cross != specs[k].cross || v->spec().missing != specs[k].missing ||
          v->spec().initialAngle != specs[k].initialAngle)
        v->setScrew(specs[k], shown);   // another seed: another screw
      else
        v->setAngle(shown);   // left alone while the user is turning it
      continue;
    }
    auto* view = new ScrewView(specs[k], shown);
    view->onTurned = [this, i](double angle) { screwTurned(i, angle); };
    if (firstControl) root_->addView(view, firstControl);   // over the wear and the tapes, below the controls
    else root_->addView(view);
    screws_[k] = view;
  }
  // The same four screws on the back (over its wear and contents; the corners are free of text).
  if (!back_) return;
  for (int i = 0; i < kNumScrewViews; ++i) {
    const auto k = static_cast<std::size_t>(i);
    const double shown = shownScrewAngle(specs[k].initialAngle, screwAngles_[k]);
    if (ScrewView* v = backScrews_[k]) {
      if (v->spec().cross != specs[k].cross || v->spec().missing != specs[k].missing ||
          v->spec().initialAngle != specs[k].initialAngle)
        v->setScrew(specs[k], shown);
      else
        v->setAngle(shown);
      continue;
    }
    auto* view = new ScrewView(specs[k], shown);
    view->onTurned = [this, i](double angle) { screwTurned(i, angle); };
    back_->addView(view);
    backScrews_[k] = view;
  }
}

// The end of a drag: the rotation from the seeded angle goes to the instance decor (one edit, one dirty mark).
void Editor::screwTurned(int index, double shownDeg) {
  if (!owner_ || index < 0 || index >= kNumScrews || !screws_[static_cast<std::size_t>(index)]) return;
  const float user = userScrewAngle(shownDeg, screws_[static_cast<std::size_t>(index)]->spec().initialAngle);
  const Decor d = withScrewAngle(owner_->decor(), index, user);
  screwAngles_[static_cast<std::size_t>(index)] = d.screwAngles[index];
  owner_->editDecor(d);
  decorRevision_ = owner_->decorRevision();
  placeScrews();   // the same screw on the other side follows
}

void Editor::createViews(CViewContainer* parent) {
  auto* ctl = getController();
  const CRect all(0, 0, kWidth, kHeight);

  // 1-2: panel, grain, boxes and fixed texts. 3: silkscreen. 4: wear and paper tapes. 5: screws. 6: the controls and
  // displays.
  root_ = parent;
  parent->addView(new PanelView(all, &skin_));
  silk_ = new SilkscreenView(all, silkscreenItems());
  parent->addView(silk_);
  wear_ = new WearView(all, wearMarks(seed_, wearLevel_));
  parent->addView(wear_);
  placeScrews();    // appended: the controls come after
  rebuildTapes();   // inserted before the screws

  for (const ControlSpec& spec : kControls) {
    const CRect rect = toCRect(viewRect(spec));
    const auto tag = static_cast<int32_t>(spec.id);
    NormalizedControl* control = nullptr;
    switch (spec.kind) {
    case Kind::Knob:
      {
        auto* knob = new RackKnob(rect, this, tag, spec.capRadius, &skin_);
        knob->setCenterOffsetY(knobCentreOffsetY(spec.capRadius));   // the big cap's view is taller above its centre
        control = knob;
      }
      break;
    case Kind::Toggle:
      control = new RackToggle(rect, this, tag, spec.led == Led::Yellow ? RackLed::Yellow : RackLed::Red, false, &skin_);
      break;
    case Kind::Choice: {
      std::vector<std::string> names(spec.choices, spec.choices + spec.choiceCount);
      control = new RackLitChoice(rect, this, tag, std::move(names), spec.columns, spec.faceWidth, &skin_);
      break;
    }
    }
    if (auto* p = ctl->getParameterObject(spec.id))
      control->setDefaultValue(static_cast<float>(p->getInfo().defaultNormalizedValue));
    parent->addView(control);
    controls_[spec.id] = control;
  }

  for (int i = 0; i < kMeterRowCount; ++i) {
    bars_[i] = new MeterBar(toCRect(meterBarRect(kMeterRows[i])), MeterKind::Level, kMeterLeds,
                            FairyTapes::Ui::MeterScale{kMeterMinDb, kMeterMaxDb});
    parent->addView(bars_[i]);
  }
  for (int i = 0; i < kClipCount; ++i) {
    clips_[i] = new ClipLed(toCRect(clipRect(kClips[i])), kClips[i].label);
    clips_[i]->onClick = [this] { clearClips(); };
    parent->addView(clips_[i]);
  }

  applyControlWear();   // caps and faces of the instance

  // The way to the back of the panel: a small text at the right of the header (DESIGN_SYSTEM.md, one link only).
  const FairyTapes::Ui::BackBox link = FairyTapes::Ui::frontBackLinkBox();
  backLink_ = new TextLink(CRect(link.x, link.y, link.x + link.w, link.y + link.h), "BACK  \xE2\x86\x92");
  backLink_->onClick = [this] { startFlip(true); };
  parent->addView(backLink_);
}

// ---- The back of the panel ------------------------------------------------------------------------------------------
void Editor::buildBack(CViewContainer* host) {
  if (!host) return;
  BackContent content = backContent(prefs_);
  for (auto& item : content.items) {
    const int key = item.key;
    item.onChange = [this, key](int index) { changeSetting(key, index); };
  }
  BackView::Callbacks cb;
  cb.back = [this] { startFlip(false); };
  cb.tabChanged = [this](int tab) { backTab_ = tab; };
  back_ = new BackView(CRect(0, 0, kWidth, kHeight), &skin_, std::move(content), std::move(cb), backTab_);
  back_->setCustomDrawer(kDevicesTab, drawDevicesTab);
  back_->setVisible(false);
  host->addView(back_);
  applyBackWear();
  placeScrews();   // the back's screws
}

// Turns the panel. A text field open on a tape is closed first, keeping what was typed.
void Editor::startFlip(bool toBack) {
  closeTapeEdit(true);
  flip_.start(toBack);
}

// WEAR and TAPE FONT: stored in the user preferences (a failed write is ignored: the choice still applies to this editor).
void Editor::changeSetting(int key, int index) {
  Prefs p = prefs_;
  if (!applySetting(key, index, p)) return;
  prefs_ = p;
  try {
    savePrefs(prefsPath(), prefs_);
  } catch (...) {
  }
  setWearLevel(p.wear);
  setTapeFont(tapeFontIdOf(p.tapeFont));
}

// One click clears the three clip LEDs: the hidden writable parameter goes to the processor, which clears the flags and
// answers with 0.
void Editor::clearClips() {
  auto* ctl = getController();
  ctl->setParamNormalized(kClipClear, 1.0);
  ctl->beginEdit(kClipClear);
  ctl->performEdit(kClipClear, 1.0);
  ctl->endEdit(kClipClear);
}

void Editor::watchParameters() {
  auto* ctl = getController();
  for (ParamID id = 0; id < kNumParams; ++id) {
    if (auto* p = ctl->getParameterObject(id)) {
      watched_[id] = p;  // keeps the Parameter alive until removeDependent
      p->addDependent(this);
    }
  }
  pending_.store(0);
  for (ParamID id = 0; id < kNumParams; ++id) syncFromController(id);
}

void Editor::unwatchParameters() {
  for (auto& p : watched_) {
    if (p) {
      p->removeDependent(this);
      p = nullptr;
    }
  }
  pending_.store(0);
}

void PLUGIN_API Editor::update(Steinberg::FUnknown* changedUnknown, Steinberg::int32 message) {
  if (message != IDependent::kChanged) return;
  if (auto* p = Steinberg::FCast<Steinberg::Vst::Parameter>(changedUnknown)) {
    const ParamID id = p->getInfo().id;
    if (id < kNumParams) pending_.fetch_or(bit(id));
  }
}

void Editor::onTimer() {
  flip_.tick();   // the turn of the panel, if one is running
  const uint32_t changed = pending_.exchange(0);
  for (ParamID id = 0; id < kNumParams; ++id)
    if (changed & bit(id)) syncFromController(id);
  refreshDecor();
  if (tapeEdit_ && frame && frame->getFocusView() != tapeEdit_) closeTapeEdit(false);   // the field is done
  updateDisplays();
}

// Display values (neutral until the host forwards the processor's output): LEVEL rows and clip LEDs.
void Editor::updateDisplays() {
  auto* ctl = getController();
  for (int i = 0; i < kMeterRowCount; ++i)
    if (bars_[i]) bars_[i]->setLevel(controllerNorm(ctl, kMeterRows[i].id, 0.0), kTimerSeconds);
  for (int i = 0; i < kClipCount; ++i)
    if (clips_[i]) clips_[i]->setLit(controllerNorm(ctl, kClips[i].id, 0.0) >= 0.5);
}

void Editor::syncFromController(ParamID id) {
  auto* ctl = getController();
  const auto* p = ctl->getParameterObject(id);
  const double v = controllerNorm(ctl, id, p ? p->getInfo().defaultNormalizedValue : 0.0);
  if (auto* control = controls_[id]) {
    control->setValueNormalized(static_cast<float>(v));
    control->invalid();
  }
  applySideEffects(id, v);
}

void Editor::applySideEffects(ParamID id, double norm) {
  const ControlSpec* spec = controlOf(id);
  if (silk_ && spec && spec->kind == Kind::Knob) silk_->setValueText(static_cast<int>(id), panelValueText(id, norm));
  if (id == kBypass || id == kCharacterIn) {
    if (id == kBypass) state_.bypass = norm >= 0.5;
    else state_.characterIn = norm >= 0.5;
    refreshDimming();
  }
}

void Editor::refreshDimming() {
  for (const ControlSpec& c : kControls) {
    const bool dimmed = controlDimmed(c, state_);
    if (auto* control = controls_[c.id]) control->setDimmed(dimmed);
    if (silk_) silk_->setDimmed(static_cast<int>(c.id), dimmed);
  }
  for (int i = 0; i < kMeterRowCount; ++i)
    if (bars_[i]) bars_[i]->setDimmed(meterDimmed(kMeterRows[i], state_));
  for (int i = 0; i < kClipCount; ++i)
    if (clips_[i]) clips_[i]->setDimmed(clipDimmed(kClips[i], state_));
}

void Editor::valueChanged(CControl* control) {
  if (control == tapeEdit_) {   // the writing on a tape: confirmed unless Escape was pressed
    if (!static_cast<TapeEdit*>(tapeEdit_)->cancelled) commitTapeText(tapeEditSlot_, tapeEdit_->getText().getString());
    return;
  }
  const auto id = static_cast<ParamID>(control->getTag());
  if (id >= kNumParams) return;
  auto* ctl = getController();
  const double v = FairyTapes::Ui::sanitizeNorm(control->getValueNormalized(), control->getDefaultValue());
  ctl->setParamNormalized(id, v);
  ctl->performEdit(id, v);
  applySideEffects(id, v);
}

void Editor::controlBeginEdit(CControl* control) {
  const auto id = static_cast<ParamID>(control->getTag());
  if (id < kNumParams) getController()->beginEdit(id);
}

void Editor::controlEndEdit(CControl* control) {
  const auto id = static_cast<ParamID>(control->getTag());
  if (id < kNumParams) getController()->endEdit(id);
}

} // namespace FairyTapes::Hotline::Ui
