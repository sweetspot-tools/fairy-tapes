// Happy Ending GUI: the plugin editor. Layout from the preview (docs/mockups/happy_ending_rack.html, ui/layout.h);
// structure from Deep Throat's editor (layers in a frame or a plain container, one view per control, Parameter
// dependents applied on the UI timer, typed values, the content scale), with the window also scaled by this plug-in's
// SIZE.
#include "ui/editor.h"

#include "controller.h"
#include "display.h"
#include "ui/level_views.h"
#include "ui/panel_view.h"
#include "ui/readouts.h"
#include "ui/screen_model.h"
#include "ui/value_text.h"

#include "fairy_ui/draw_util.h"
#include "fairy_ui/knob_math.h"
#include "fairy_ui/meters.h"
#include "fairy_ui/rack_controls.h"
#include "fairy_ui/sanitize.h"
#include "fairy_ui/silkscreen_view.h"

#include "base/source/updatehandler.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/controls/ctextedit.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/events.h"

#if SMTG_OS_LINUX
#include "public.sdk/source/vst/vstgui_linux_runloop_support.h"
#include "vstgui/lib/platform/linux/linuxfactory.h"
#include "vstgui/lib/platform/platform_x11.h"
#include "vstgui/lib/platform/platformfactory.h"
#endif

#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace FairyTapes::HappyEnding::Ui {

using namespace VSTGUI;
using FairyTapes::Ui::MeterKind;
using FairyTapes::Ui::NormalizedControl;
using FairyTapes::Ui::RackCycle;
using FairyTapes::Ui::RackKnob;
using FairyTapes::Ui::RackLed;
using FairyTapes::Ui::RackLitChoice;
using FairyTapes::Ui::RackToggle;
using FairyTapes::Ui::SilkscreenView;
using Steinberg::Vst::ParamID;

namespace {

constexpr uint32_t kTimerMs = 30;
constexpr double kTimerSeconds = kTimerMs / 1000.0;   // the step of the meters' peak hold
constexpr CColor kFrameBackground(0x16, 0x16, 0x17);
// Tags of the views that are not a parameter (outside every parameter, hidden and display id).
constexpr int32_t kValueEditTag = 9100, kMatchTag = 9200, kViewsTag = 9300;
constexpr int kNumViews = 5;   // ScreenView: EQ, GLUE, CLIP, LIMIT, FINAL (kActions[1..5])
static_assert(static_cast<int>(ScreenView::Final) == kNumViews - 1, "one view button per screen view");

// The user's preferences file; empty when the folder is unknown or the environment cannot be read.
std::filesystem::path userPrefsFile() {
  try {
    return prefsPath();
  } catch (...) {
    return {};
  }
}

CRect toCRect(const Rect& r) { return CRect(r.x, r.y, r.x + r.w, r.y + r.h); }

// The view of a LED row: centred on the row, 2r + 1 high, so the shared MeterBar (LED radius min(pitch / 2 - 1, height / 2
// - 0.5)) draws LEDs of the preview's radius r.
CRect ledRowRect(const Rect& bar, float radius) {
  const double cy = bar.y + bar.h / 2.0;
  return CRect(bar.x, cy - radius - 0.5, bar.x + bar.w, cy + radius + 0.5);
}

// A parameter the controls edit: the stored table, LISTEN and SET.
bool editable(int32_t tag) { return (tag >= 0 && tag < static_cast<int32_t>(kNumParams)) || tag == kListenBand || tag == kSetActive; }

double defaultNorm(Steinberg::Vst::EditController* ctl, ParamID id) {
  const auto* p = ctl->getParameterObject(id);
  return p ? p->getInfo().defaultNormalizedValue : 0.0;
}

double controllerNorm(Steinberg::Vst::EditController* ctl, ParamID id) {
  return FairyTapes::Ui::sanitizeNorm(ctl->getParamNormalized(id), defaultNorm(ctl, id));
}

// The controller's stored table (kNumParams values, ParamID order), sanitized.
void readNorms(Steinberg::Vst::EditController* ctl, double* norm) {
  for (ParamID id = 0; id < kNumParams; ++id) norm[id] = controllerNorm(ctl, id);
}

// A stored value on its step: the position the DSP, the SDK texts and the host read (indexFromNorm), as a grid value.
double onStep(ParamID id, double norm) {
  const auto p = static_cast<FairyTapes::HappyEnding::ParamID>(id);
  return normFromIndex(p, indexFromNorm(p, norm));
}

// The value printed under a knob or a cycling selector: formatValue (HI PASS FREQ on the scale of its M/S), but LIMIT
// RELEASE prints its real time in ms for the current style (releaseText) and the M/S selectors their sigla (ST / MID /
// SIDE, as in the preview; the host shows Stereo / Mid / Side).
std::string valueText(ParamID id, const double* norm) {
  const auto p = static_cast<FairyTapes::HappyEnding::ParamID>(id);
  if (FairyTapes::HappyEnding::Ui::detail::isMsParam(p)) return FairyTapes::HappyEnding::Ui::detail::msSigla(indexFromNorm(p, norm[id]));
  if (id == kLimitRelease) return releaseText(indexFromNorm(kLimitStyle, norm[kLimitStyle]), indexFromNorm(kLimitRelease, norm[kLimitRelease]));
  return formatValue(static_cast<FairyTapes::HappyEnding::ParamID>(id), norm[id], norm);
}

// The text field of a typed value (The Stripper's pattern): Escape cancels, Enter or leaving the field confirms.
// CTextEdit itself would still report the typed text when Escape comes through the platform field.
class ValueEdit : public CTextEdit {
public:
  using CTextEdit::CTextEdit;
  bool cancelled = false;

  void onKeyboardEvent(KeyboardEvent& event) override {
    if (event.type == EventType::KeyDown && event.virt == VirtualKey::Escape) cancelled = true;
    CTextEdit::onKeyboardEvent(event);
  }

protected:
  ~ValueEdit() noexcept override = default;
  void platformOnKeyboardEvent(KeyboardEvent& event) override {
    if (event.type == EventType::KeyDown && event.virt == VirtualKey::Escape) cancelled = true;
    CTextEdit::platformOnKeyboardEvent(event);
  }
};

// MATCH: an on/off face with a red LED that is lit only while pressed; the press calls onPress. It has no parameter and
// tells the listener nothing (the action is the controller's doMatch, one host edit of LOUDNESS).
class MomentaryButton : public RackToggle {
public:
  MomentaryButton(const CRect& size, int32_t tag, const FairyTapes::Ui::Skin* skin)
  : RackToggle(size, nullptr, tag, RackLed::Red, false, skin) {}
  std::function<void()> onPress;

  CMouseEventResult onMouseDown(CPoint&, const CButtonState& buttons) override {
    if (!buttons.isLeftButton()) return kMouseEventNotHandled;
    press(true);
    if (onPress) onPress();
    return kMouseEventHandled;   // the release comes back to this view
  }
  CMouseEventResult onMouseUp(CPoint&, const CButtonState&) override {
    press(false);
    return kMouseEventHandled;
  }
  CMouseEventResult onMouseCancel() override {
    press(false);
    return kMouseEventHandled;
  }

private:
  void press(bool down) {
    setValue(down ? 1.f : 0.f);
    invalid();
  }
};

// DYN: the small on/off button of a VIBE EQ group header (docs/DESIGN_SYSTEM.md, the header exception): no LED, the name
// on the face, the face lit amber while on (the preview's headBtn: 26 x 18, 9 px bold). The view is the face.
class HeadToggle : public FairyTapes::Ui::ToggleButton {
public:
  HeadToggle(const CRect& size, IControlListener* listener, int32_t tag, const char* name, const FairyTapes::Ui::Skin* skin)
  : ToggleButton(size, listener, tag, std::string(), skin), name_(name ? name : "") {}

  void draw(CDrawContext* ctx) override {
    static const SharedPointer<CFontDesc> font = makeOwned<CFontDesc>("Arial", 9, kBoldFace);
    ctx->saveGlobalState();
    ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
    if (isDimmed()) ctx->setGlobalAlpha(ctx->getGlobalAlpha() * PanelView::kDimmedAlpha);
    const bool on = getValue() >= 0.5f;
    const CRect& r = getViewSize();
    CRect shadow(r);
    shadow.offset(0, on ? 1.5 : 4.0);
    if (auto path = owned(ctx->createRoundRectGraphicsPath(shadow, 2))) {
      ctx->setFillColor(CColor(0, 0, 0, on ? 140 : 215));
      ctx->drawGraphicsPath(path, CDrawContext::kPathFilled);
    }
    CRect f(r);
    f.offset(0, on ? FairyTapes::Ui::Rack::kPressDepth : 0.0);
    if (auto path = owned(ctx->createRoundRectGraphicsPath(f, 2))) {
      ctx->setFillColor(on ? CColor(0xf0, 0xb4, 0x29) : CColor(0x8e, 0x8e, 0x91));
      ctx->drawGraphicsPath(path, CDrawContext::kPathFilled);
      ctx->setFrameColor(CColor(0xb8, 0xb8, 0xbb));
      ctx->setLineWidth(1);
      ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
    }
    ctx->setFont(font);
    ctx->setFontColor(on ? CColor(0x2a, 0x1d, 0x05) : CColor(0x1a, 0x1a, 0x1c));
    const CCoord tw = ctx->getStringWidth(name_.c_str());
    ctx->drawString(name_.c_str(), CPoint(f.left + (f.getWidth() - tw) / 2, f.top + f.getHeight() / 2 + FairyTapes::Ui::Rack::kLitTextBaseline));
    ctx->restoreGlobalState();
    setDirty(false);
  }

private:
  std::string name_;
};

} // namespace

Editor::Editor(Steinberg::Vst::EditController* controller)
: VSTGUIEditor(controller), owner_(dynamic_cast<Controller*>(controller)) {
  // Parameter dependents need the global update handler (VST3Editor creates it the same way).
  Steinberg::UpdateHandler::instance();
  loadAppearance(userPrefsFile());   // SIZE, so the host's first getSize is the window it will open
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
  const PixelSize px = windowSize();
  rect->right = rect->left + px.width;
  rect->bottom = rect->top + px.height;
  return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Editor::setContentScaleFactor(ScaleFactor factor) {
#if SMTG_OS_MACOS
  (void)factor;   // Cocoa backing scale handles Retina; VST3Editor keeps 1 there as well
  return Steinberg::kResultFalse;
#else
  const double s = sanitizeContentScale(factor);
  if (s == scale_) return Steinberg::kResultTrue;
  scale_ = s;
  applyWindowSize();
  return Steinberg::kResultTrue;
#endif
}

void Editor::applyWindowSize() {
  const PixelSize px = windowSize();
  Steinberg::ViewRect r(rect.left, rect.top, rect.left + px.width, rect.top + px.height);
  setRect(r);   // getSize reports the scaled size even before the view is attached
  if (frame) frame->setZoom(FairyTapes::Ui::frameZoom(scale_, prefs_.size));   // views keep their logical coordinates
  if (root_ && plugFrame) plugFrame->resizeView(this, &r);   // shown: the host answers with onSize
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

  // The preferences again: SIZE may have changed in another instance since the view was made.
  loadAppearance(userPrefsFile());

  frame = new CFrame(CRect(0, 0, kWidth, kHeight), this);
  frame->setBackgroundColor(kFrameBackground);
  createViews(frame);
  const double zoom = FairyTapes::Ui::frameZoom(scale_, prefs_.size);
  if (zoom != 1.0) frame->setZoom(zoom);   // VSTGUIEditor::attached then reports the scaled size
  if (!frame->open(parent, platformType, config)) {
    close();
    return false;
  }
  startTimer();
  return true;
}

void Editor::startTimer() {
  timer_ = makeOwned<CVSTGUITimer>([this](CVSTGUITimer*) { onTimer(); }, kTimerMs, true);
}

void PLUGIN_API Editor::close() {
  // LISTEN and SET live only while the editor is shown (spec: never left on, never stored).
  switchSessionParametersOff();
  // Timer first: nothing may touch the views once they start going away.
  if (timer_) {
    timer_->stop();
    timer_ = nullptr;
  }
  unwatchParameters();
  closeValueEdit(true);   // what was typed is kept, as when the field loses the focus
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
  root_ = nullptr;
  panel_ = nullptr;
  silk_ = nullptr;
  controls_.fill(nullptr);
  match_ = nullptr;
  views_ = nullptr;
  meters_.fill(nullptr);
  grBars_.fill(nullptr);
  clips_.fill(nullptr);
  grTags_ = nullptr;
  lufs_ = nullptr;
  actLeds_.fill(nullptr);
  valueEdit_ = nullptr;
  valueEditId_ = kNumParams;
}

void Editor::createViews(CViewContainer* parent) {
  auto* ctl = getController();
  const CRect all(0, 0, kWidth, kHeight);
  root_ = parent;

  // 1-2: panel body, grain, boxes, header lines, printed texts and engine tags.
  panel_ = new PanelView(all, &skin_);
  parent->addView(panel_);

  // 3: the silkscreen: names, scales, values and button labels of the controls (clicks go through).
  silk_ = new SilkscreenView(all, silkscreenItems());
  // The knob values at the preview's place (kKnobValueBaselineBelowCap), with the silkscreen's value look; the values
  // of the cycling selectors stay where the silkscreen puts them.
  silk_->setValueDrawer([this](CDrawContext* ctx, const FairyTapes::Ui::PrintText& t, const CPoint& origin, std::size_t item) {
    static const SharedPointer<CFontDesc> font = makeOwned<CFontDesc>("Arial", FairyTapes::Ui::kSilkValueSize, kBoldFace);
    if (!silk_) return false;
    const FairyTapes::Ui::PrintedItem& it = silk_->model().item(item);
    if (it.kind != FairyTapes::Ui::PrintKind::Knob) return false;
    ctx->setFont(font);
    ctx->setFontColor(FairyTapes::Ui::kAmber);
    const CCoord w = ctx->getStringWidth(t.text.c_str());
    ctx->drawString(t.text.c_str(), CPoint(origin.x + FairyTapes::Ui::alignedLeft(t.x, w, t.align),
                                           origin.y + it.cy + it.capRadius + kKnobValueBaselineBelowCap));
    return true;
  });
  parent->addView(silk_);

  // The controls, one view per ControlSpec (the three LISTEN buttons share kListenBand as their tag).
  for (int i = 0; i < kNumControls; ++i) {
    const ControlSpec& spec = kControls[i];
    const CRect r = toCRect(viewRect(spec));
    const auto tag = static_cast<int32_t>(spec.id);
    NormalizedControl* control = nullptr;
    switch (spec.kind) {
    case Kind::Knob: {
      auto* knob = new RackKnob(r, this, tag, spec.capRadius, &skin_);
      const ParamID id = spec.id;
      knob->onValueClick = [this, id] { editValue(id); };
      control = knob;
      break;
    }
    case Kind::LitChoice: {
      const LitChoiceSpec* l = litChoiceOf(spec.id);
      std::vector<std::string> names;
      if (l)
        for (int k = 0; k < l->count; ++k) names.emplace_back(l->names[k]);
      control = new RackLitChoice(r, this, tag, std::move(names), 1, l ? l->faceW : FairyTapes::Ui::Rack::kLitFaceWidth, &skin_);
      break;
    }
    case Kind::Cycle:
      control = new RackCycle(r, this, tag, spec.id < kNumParams ? kParams[spec.id].steps : 2, &skin_);
      break;
    case Kind::HeadToggle: control = new HeadToggle(r, this, tag, spec.name, &skin_); break;
    case Kind::Toggle:
    case Kind::Action: control = new RackToggle(r, this, tag, RackLed::Red, false, &skin_); break;
    }
    // A LISTEN button is lit when its bell is heard: off by default like the parameter.
    control->setDefaultValue(spec.id == kListenBand ? 0.f : static_cast<float>(defaultNorm(ctl, spec.id)));
    parent->addView(control);
    controls_[static_cast<std::size_t>(i)] = control;
  }

  // MATCH and the screen view buttons (no parameter).
  auto* match = new MomentaryButton(toCRect(viewRect(actionOf(Action::Match))), kMatchTag, &skin_);
  match->onPress = [this] {
    if (owner_) owner_->doMatch();   // one host edit of LOUDNESS; the dependent flag brings it to the knob
  };
  match_ = match;
  parent->addView(match);
  std::vector<std::string> viewNames;
  for (const ActionSpec& a : kActions)
    if (a.kind == Kind::LitChoice) viewNames.emplace_back(a.name);
  const Rect column{kViewCx - kViewFaceW / 2, kViewFirstCy - kViewPitch / 2, kViewFaceW, kViewPitch * kNumViews};
  views_ = new RackLitChoice(toCRect(column), this, kViewsTag, std::move(viewNames), 1, kViewFaceW, &skin_);
  views_->setValueNormalized(static_cast<float>(
      FairyTapes::Ui::normFromChoiceIndex(owner_ ? static_cast<int>(owner_->screenView()) : 0, kNumViews)));
  parent->addView(views_);

  // LEVEL: the peak rows, the GR rows with their tags, the nine clip LEDs (a click on one clears all), the LUFS screen
  // (a click on the integrated loudness restarts it) and the ACT LEDs of the DYN buttons. They show the display
  // parameters (updateDisplays on the timer) and dim by the UiState (refreshFromController).
  for (int i = 0; i < 4; ++i) {
    meters_[static_cast<std::size_t>(i)] = new LevelBar(ledRowRect(meterBarRect(kMeterRows[i]), kMeterLedRadius), MeterKind::Level, kMeterLeds);
    parent->addView(meters_[static_cast<std::size_t>(i)]);
  }
  for (int i = 0; i < 5; ++i) {
    grBars_[static_cast<std::size_t>(i)] =
        new LevelBar(ledRowRect(grBarRect(kGrRows[i]), kGrLedRadius), MeterKind::GainReduction, kMeterLeds);
    parent->addView(grBars_[static_cast<std::size_t>(i)]);
  }
  grTags_ = new GrTags();
  parent->addView(grTags_);
  for (int i = 0; i < 9; ++i) {
    auto* clip = new FairyTapes::Ui::ClipLed(toCRect(clipLedRect(kClipLeds[i])), kClipLeds[i].label);
    clip->onClick = [this] { clearClips(); };
    clips_[static_cast<std::size_t>(i)] = clip;
    parent->addView(clip);
  }
  lufs_ = new LufsView(toCRect(kLufsScreenRect));
  lufs_->onReset = [this] { resetIntegrated(); };
  parent->addView(lufs_);
  for (int i = 0; i < 2; ++i) {
    actLeds_[static_cast<std::size_t>(i)] = new ActLed(kActLeds[i]);
    parent->addView(actLeds_[static_cast<std::size_t>(i)]);
  }

  watchParameters();   // every value, text, dimming and lock from the controller
  updateDisplays();
}

void Editor::watchParameters() {
  unwatchParameters();
  auto* ctl = getController();
  for (int k = 0; k < kNumWatched; ++k) {
    const ParamID id = k < static_cast<int>(kNumParams) ? static_cast<ParamID>(k) : (k == static_cast<int>(kNumParams) ? kListenBand : kSetActive);
    if (auto* p = ctl->getParameterObject(id)) {
      watched_[static_cast<std::size_t>(k)] = p;   // keeps the Parameter alive until removeDependent
      p->addDependent(this);
    }
  }
  pending_.store(false);
  refreshFromController();
}

void Editor::unwatchParameters() {
  for (auto& p : watched_) {
    if (p) {
      p->removeDependent(this);
      p = nullptr;
    }
  }
  pending_.store(false);
}

void PLUGIN_API Editor::update(Steinberg::FUnknown*, Steinberg::int32 message) {
  if (message == Steinberg::IDependent::kChanged) pending_.store(true);
}

void Editor::onTimer() {
  if (pending_.exchange(false)) refreshFromController();
  updateDisplays();
  // The field reported what was typed when it lost the focus (Enter, a click elsewhere): it is done.
  if (valueEdit_ && frame && frame->getFocusView() != valueEdit_) closeValueEdit(false);
}

// The display parameters (hidden, written by the processor about every 33 ms) to the LEVEL views. A GR row that is
// dimmed (BYPASS or its stage's IN off) shows no reduction, as the preview; the peak rows, the IN and OUT clip LEDs and
// the LUFS screen are always live.
void Editor::updateDisplays() {
  if (!root_) return;
  auto* ctl = getController();
  auto display = [ctl](ParamID id) { return FairyTapes::Ui::sanitizeNorm(ctl->getParamNormalized(id), 0.0); };
  for (int i = 0; i < 4; ++i)
    if (LevelBar* bar = meters_[static_cast<std::size_t>(i)]) bar->setLevel(display(kMeterRows[i].id), kTimerSeconds);   // -60..+6 dB, the scale printed under the rows
  for (int i = 0; i < 5; ++i)
    if (LevelBar* bar = grBars_[static_cast<std::size_t>(i)]) bar->setLevel(grRowDimmed(i, state_) ? 0.0 : display(kGrRows[i].id), kTimerSeconds);
  for (int i = 0; i < 9; ++i)
    if (auto* clip = clips_[static_cast<std::size_t>(i)]) clip->setLit(display(kClipLeds[i].id) >= 0.5);
  if (lufs_)
    lufs_->setReadout(lufsFromNorm(display(kDispLufsShort)), lufsFromNorm(display(kDispLufsInt)),
                      indexFromNorm(kTarget, FairyTapes::Ui::sanitizeNorm(ctl->getParamNormalized(kTarget), defaultNorm(ctl, kTarget))));
  for (int i = 0; i < 2; ++i)
    if (ActLed* led = actLeds_[static_cast<std::size_t>(i)]) led->setLevel(display(kActLedIds[i]));
}

void Editor::refreshFromController() {
  if (!root_) return;
  auto* ctl = getController();
  double norm[kNumParams];
  readNorms(ctl, norm);
  const int listen = listenFromNorm(FairyTapes::Ui::sanitizeNorm(ctl->getParamNormalized(kListenBand), kDefaultListenNorm));
  const bool set = FairyTapes::Ui::sanitizeNorm(ctl->getParamNormalized(kSetActive), kDefaultSetNorm) >= 0.5;
  const UiState s = uiStateFrom(norm, listen, set);
  state_ = s;
  if (panel_) panel_->setUiState(s);
  // LEVEL: the GR rows and their tags dim with BYPASS and their stage's IN, the stage clip LEDs likewise, the ACT LEDs with
  // BYPASS and VIBE EQ's IN; the peak rows, the IN / OUT clip LEDs and the LUFS screen never do.
  for (int i = 0; i < 5; ++i) {
    if (LevelBar* bar = grBars_[static_cast<std::size_t>(i)]) bar->setDimmed(grRowDimmed(i, s));
    if (grTags_) grTags_->setDimmed(i, grRowDimmed(i, s));
  }
  for (int i = 0; i < 9; ++i)
    if (auto* clip = clips_[static_cast<std::size_t>(i)]) clip->setDimmed(clipLedDimmed(i, s));
  for (ActLed* led : actLeds_)
    if (led) led->setDimmed(actLedDimmed(s));

  const int heard = listenedBand(s);
  const bool side = indexFromNorm(kHpMs, norm[kHpMs]) == 2;
  for (int i = 0; i < kNumControls; ++i) {
    const ControlSpec& spec = kControls[i];
    const bool dimmed = controlDimmed(spec, s), locked = controlLocked(spec, s), visible = controlVisible(spec, s);
    if (NormalizedControl* control = controls_[static_cast<std::size_t>(i)]) {
      // LISTEN: the LED of the bell really heard (off while its band or FINE EQ is off), as the engine.
      const double v = spec.id == kListenBand ? (heard == spec.choiceIndex + 2 ? 1.0 : 0.0)
                       : spec.id == kSetActive ? (set ? 1.0 : 0.0)
                                               : onStep(spec.id, norm[spec.id]);
      if (std::fabs(control->getValueNormalized() - v) > 1e-9) {
        control->setValueNormalized(static_cast<float>(v));
        control->invalid();
      }
      if (control->isDimmed() != dimmed) {
        control->setDimmed(dimmed);
        control->invalid();
      }
      control->setMouseEnabled(!locked);
      if (control->isVisible() != visible) control->setVisible(visible);
    }
    if (!silk_ || spec.kind == Kind::LitChoice || spec.kind == Kind::HeadToggle) continue;   // names on the faces
    const int key = printId(spec);
    silk_->setDimmed(key, dimmed);
    if (spec.kind == Kind::Knob || spec.kind == Kind::Cycle) silk_->setValueText(key, valueText(spec.id, norm));
    if (spec.id == kHpFreq) {   // the print with the scale of the current M/S
      silk_->setHidden(key, !visible || side);
      silk_->setDimmed(kHpSidePrintId, dimmed);
      silk_->setHidden(kHpSidePrintId, !visible || !side);
      silk_->setValueText(kHpSidePrintId, valueText(spec.id, norm));
    } else {
      silk_->setHidden(key, !visible);
    }
  }

  // MATCH and the view buttons: dimmed and locked by BYPASS only.
  for (NormalizedControl* a : {match_, views_}) {
    if (!a) continue;
    const bool dimmed = actionDimmed(actionOf(a == match_ ? Action::Match : Action::ViewEq), s);
    if (a->isDimmed() != dimmed) {
      a->setDimmed(dimmed);
      a->invalid();
    }
    a->setMouseEnabled(!dimmed);
  }
  if (silk_) silk_->setDimmed(actionPrintId(Action::Match), actionDimmed(actionOf(Action::Match), s));
}

// ---- Control listener -----------------------------------------------------------------------------------------------
void Editor::valueChanged(CControl* control) {
  if (!control) return;
  if (control == valueEdit_) {   // a typed value: read unless Escape was pressed
    valueEditReported_ = true;
    if (!static_cast<ValueEdit*>(valueEdit_)->cancelled) commitTypedValue(valueEditId_, valueEdit_->getText().getString());
    return;
  }
  if (control == views_) {   // editor state, not a parameter: the controller keeps it while the instance lives
    if (owner_)
      owner_->setScreenView(static_cast<ScreenView>(FairyTapes::Ui::choiceIndexFromNorm(control->getValueNormalized(), kNumViews)));
    return;
  }
  const auto tag = control->getTag();
  if (!editable(tag)) return;
  auto* ctl = getController();
  const auto id = static_cast<ParamID>(tag);
  double v = 0.0;
  if (id == kListenBand) {
    // The bell of this button; lit = listen to it, unlit (a click on the lit one) = LISTEN off.
    int bell = -1;
    for (int i = 0; i < kNumControls; ++i)
      if (controls_[static_cast<std::size_t>(i)] == control) bell = kControls[i].choiceIndex;
    v = normFromListen(control->getValueNormalized() >= 0.5f ? bell : -1);
  } else if (id == kSetActive) {
    v = FairyTapes::Ui::snapToggle(control->getValueNormalized());
  } else {
    v = onStep(id, FairyTapes::Ui::sanitizeNorm(control->getValueNormalized(), control->getDefaultValue()));
  }
  ctl->setParamNormalized(id, v);   // the dependent flag this raises only re-applies the same values: no echo
  ctl->performEdit(id, v);
  refreshFromController();          // the other LISTEN buttons, dimming, locks, FINAL's controls, the printed values
}

void Editor::controlBeginEdit(CControl* control) {
  const auto tag = control ? control->getTag() : -1;
  if (control != valueEdit_ && editable(tag)) getController()->beginEdit(static_cast<ParamID>(tag));
}

void Editor::controlEndEdit(CControl* control) {
  const auto tag = control ? control->getTag() : -1;
  if (control != valueEdit_ && editable(tag)) getController()->endEdit(static_cast<ParamID>(tag));
}

void Editor::editParameter(ParamID id, double norm) {
  auto* ctl = getController();
  ctl->beginEdit(id);
  ctl->setParamNormalized(id, norm);
  ctl->performEdit(id, norm);
  ctl->endEdit(id);
}

void Editor::switchSessionParametersOff() {
  auto* ctl = getController();
  if (!ctl) return;
  if (listenFromNorm(FairyTapes::Ui::sanitizeNorm(ctl->getParamNormalized(kListenBand), kDefaultListenNorm)) >= 0)
    editParameter(kListenBand, kDefaultListenNorm);
  if (FairyTapes::Ui::sanitizeNorm(ctl->getParamNormalized(kSetActive), kDefaultSetNorm) >= 0.5)
    editParameter(kSetActive, kDefaultSetNorm);
}

// ---- Typed values ---------------------------------------------------------------------------------------------------
void Editor::editValue(ParamID id) {
  if (!root_ || valueEdit_ || id >= kNumParams) return;
  RackKnob* knob = nullptr;
  for (int i = 0; i < kNumControls && !knob; ++i)
    if (kControls[i].id == id) knob = dynamic_cast<RackKnob*>(controls_[static_cast<std::size_t>(i)]);
  if (!knob) return;
  double norm[kNumParams];
  readNorms(getController(), norm);
  CRect area = knob->valueArea();
  area.inset(-4, -2);
  auto* edit = new ValueEdit(area, this, kValueEditTag, formatValue(static_cast<FairyTapes::HappyEnding::ParamID>(id), norm[id], norm).c_str());
  edit->setFont(makeOwned<CFontDesc>("Arial", 12, kBoldFace));
  edit->setFontColor(FairyTapes::Ui::kAmber);
  edit->setBackColor(CColor(0x05, 0x05, 0x05));
  edit->setFrameColor(FairyTapes::Ui::kAmber);
  edit->setHoriAlign(kCenterText);
  valueEdit_ = edit;
  valueEditId_ = id;
  valueEditReported_ = false;
  root_->addView(edit);
  if (frame) frame->setFocusView(edit);   // starts the editing
}

void Editor::closeValueEdit(bool confirm) {
  if (!valueEdit_) return;
  auto* edit = static_cast<ValueEdit*>(valueEdit_);
  if (!confirm) edit->cancelled = true;
  if (frame && frame->getFocusView() == edit) {
    frame->setFocusView(nullptr);   // the field reports unless cancelled
  } else if (confirm && !valueEditReported_ && !edit->cancelled) {
    commitTypedValue(valueEditId_, edit->getText().getString());   // no focus to lose (no window): confirmed here
  }
  valueEdit_ = nullptr;
  valueEditId_ = kNumParams;
  if (root_ && root_->isChild(edit)) root_->removeView(edit, true);
}

// A value typed in goes the way of a turn of the knob: parseValue (a step's text or a number, HI PASS FREQ on the scale
// of its M/S), then one undoable edit; a text that is no value is ignored.
void Editor::commitTypedValue(ParamID id, const std::string& typed) {
  if (id >= kNumParams) return;
  double norm[kNumParams];
  readNorms(getController(), norm);
  double v = 0.0;
  if (!parseValue(static_cast<FairyTapes::HappyEnding::ParamID>(id), typed, norm, v)) return;
  editParameter(id, v);
  refreshFromController();
}

void Editor::loadAppearance(const std::filesystem::path& file) {
  Prefs p;
  try {
    if (!file.empty()) p = loadPrefs(file);
  } catch (...) {
    p = Prefs{};
  }
  prefs_ = p;
  applyWindowSize();
}

} // namespace FairyTapes::HappyEnding::Ui
