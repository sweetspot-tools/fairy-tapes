// Happy Ending GUI: the plugin editor. Layout from the preview (docs/mockups/happy_ending_rack.html, ui/layout.h);
// structure from Deep Throat's editor (layers in a frame or a plain container, one view per control, Parameter
// dependents applied on the UI timer, typed values, the content scale), with the window also scaled by this plug-in's
// SIZE.
#include "ui/editor.h"

#include "controller.h"
#include "display.h"
#include "ui/back_layout.h"
#include "ui/back_settings.h"
#include "ui/buttons_text.h"
#include "ui/level_views.h"
#include "ui/panel_view.h"
#include "ui/readouts.h"
#include "ui/screen_model.h"
#include "ui/screen_view.h"
#include "ui/screw_angles.h"
#include "ui/tape_view.h"
#include "ui/tapes.h"
#include "ui/value_text.h"

#include "fairy_ui/back_view.h"
#include "fairy_ui/back_widgets.h"
#include "fairy_ui/draw_util.h"
#include "fairy_ui/knob_math.h"
#include "fairy_ui/meters.h"
#include "fairy_ui/rack_controls.h"
#include "fairy_ui/sanitize.h"
#include "fairy_ui/screw_view.h"
#include "fairy_ui/silkscreen_view.h"
#include "fairy_ui/tape_draw.h"
#include "fairy_ui/wear_view.h"

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

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace FairyTapes::HappyEnding::Ui {

using namespace VSTGUI;
using FairyTapes::Ui::BackContent;
using FairyTapes::Ui::BackItemKind;
using FairyTapes::Ui::BackView;
using FairyTapes::Ui::MeterKind;
using FairyTapes::Ui::NormalizedControl;
using FairyTapes::Ui::RackCycle;
using FairyTapes::Ui::RackKnob;
using FairyTapes::Ui::RackLed;
using FairyTapes::Ui::RackLitChoice;
using FairyTapes::Ui::RackToggle;
using FairyTapes::Ui::ScrewView;
using FairyTapes::Ui::SilkscreenView;
using FairyTapes::Ui::TextLink;
using FairyTapes::Ui::TapeAlign;
using FairyTapes::Ui::WearMark;
using FairyTapes::Ui::WearView;
using Steinberg::Vst::ParamID;

namespace {

constexpr uint32_t kTimerMs = 30;
constexpr double kTimerSeconds = kTimerMs / 1000.0;   // the step of the meters' peak hold
constexpr CColor kFrameBackground(0x16, 0x16, 0x17);
// Tags of the views that are not a parameter (outside every parameter, hidden and display id).
constexpr int32_t kTapeEditTag = 9000;   // + slot
constexpr int32_t kValueEditTag = 9100, kMatchTag = 9200, kViewsTag = 9300;
constexpr CCoord kTapeEditMinWidth = 120, kTapeEditHeight = 24;
static_assert(kMaxTapeTexts == kNumTapeSlots, "one stored tape text per tape slot");
static_assert(Editor::kNumScrewViews == kNumScrews, "one view per stored screw angle");
static_assert(kNumScrews == static_cast<int>(sizeof(kScrewPos) / sizeof(kScrewPos[0])), "one stored angle per screw");
constexpr std::uint32_t kBackWearSalt = 0x77u;   // the back's wear: another pattern from the same seed (the preview's 0x77)
// The seed that varies the handwritten values while the decor has none yet.
constexpr std::uint32_t kFallbackHandSeed = FairyTapes::Ui::kDefaultWearSeed;
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

// ---- The BUTTONS tab of the back (ui/buttons_text.h), drawn in the template's custom area ----
void buttonsText(CDrawContext* ctx, const std::string& s, double x, double baseline, double size, const CColor& colour) {
  if (s.empty()) return;
  ctx->setFont(makeOwned<CFontDesc>("Arial", size, kBoldFace));
  ctx->setFontColor(colour);
  ctx->drawString(s.c_str(), CPoint(x, baseline));
}

// `area` is the template's custom box in view coordinates; the lines are placed at the preview's editor px relative to it.
// A name wider than its column breaks at its spaces (the lines under it are free: its entry has at least as many).
void drawButtonsTab(CDrawContext* ctx, const CRect& area) {
  namespace T = ButtonsTab;
  using FairyTapes::Ui::kAmber;
  using FairyTapes::Ui::kDimWhite;
  using FairyTapes::Ui::kWhite;
  const double ox = area.left - T::kBox.x, oy = area.top - T::kBox.y;
  FairyTapes::Ui::drawTitledBox(ctx, CRect(ox + T::kBox.x, oy + T::kBox.y, ox + T::kBox.x + T::kBox.w, oy + T::kBox.y + T::kBox.h),
                                T::kTitle, true);
  const auto& entries = buttonsEntries();
  const auto rows = buttonsRows();
  const auto nameFont = makeOwned<CFontDesc>("Arial", T::kNameSize, kBoldFace);
  for (std::size_t i = 0; i < entries.size() && i < rows.size(); ++i) {
    const ButtonsEntry& e = entries[i];
    ctx->setFont(nameFont);
    const auto name = FairyTapes::Ui::wrapText(e.name, T::kNameWidth,
                                               [ctx](const std::string& t) { return static_cast<double>(ctx->getStringWidth(t.c_str())); });
    double y = oy + rows[i].nameBaseline;
    for (const std::string& l : name) {
      buttonsText(ctx, l, ox + T::kNameX, y, T::kNameSize, kAmber);
      y += T::kWhatStep;
    }
    y = oy + rows[i].nameBaseline;
    for (const std::string& l : e.what) {
      buttonsText(ctx, l, ox + T::kTextX, y, T::kWhatSize, kWhite);
      y += T::kWhatStep;
    }
    for (const std::string& l : e.details) {
      buttonsText(ctx, l, ox + T::kTextX, y, T::kDetailSize, kDimWhite);
      y += T::kDetailStep;
    }
  }
}

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

// RE-ROLL: the decor with a new seed (new wear, tapes and screws), the screws' rotations back to 0 and the tapes blank.
// A seed of 0 or equal to the current one is replaced by another one (as Pillow Talk's rerolledDecor).
Decor rerolledDecor(Decor d, std::uint32_t newSeed) {
  if (newSeed == 0 || newSeed == d.seed) newSeed = (d.seed ^ 0x9E3779B9u) * 2654435761u + 0x7F4A7C15u;
  if (newSeed == 0 || newSeed == d.seed) newSeed = d.seed + 1u ? d.seed + 1u : 1u;
  d.seed = newSeed;
  for (float& a : d.screwAngles) a = 0.f;
  d.tapeTexts.clear();
  return d;
}

// The text field of a typed value or of a tape (The Stripper's pattern): Escape cancels, Enter or leaving the field confirms.
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

// The line under the OVERSAMPLING row (the preview's BEHAVIOUR text): the shared template has no free text line there, so
// it is a small view of the back that draws only while the back shows SETTINGS and lets every click through.
class BackNote : public CView {
public:
  BackNote(const CRect& size, const BackView* back, std::string text, CCoord baseline)
  : CView(size), back_(back), text_(std::move(text)), baseline_(baseline) {
    setMouseEnabled(false);
  }

  void draw(CDrawContext* ctx) override {
    if (back_ && back_->tab() == kTabSettings) {
      static const SharedPointer<CFontDesc> font = makeOwned<CFontDesc>("Arial", 10, kBoldFace);
      ctx->saveGlobalState();
      ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
      ctx->setFont(font);
      ctx->setFontColor(FairyTapes::Ui::kDimWhite);
      ctx->drawString(text_.c_str(), CPoint(getViewSize().left, baseline_));
      ctx->restoreGlobalState();
    }
    setDirty(false);
  }
  CLASS_METHODS_NOCOPY(BackNote, CView)

private:
  const BackView* back_;   // the back this is drawn on (its tab decides)
  std::string text_;
  CCoord baseline_;
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
  applyWindowSize(s, size_);   // a host that refuses leaves the window as it is
  return Steinberg::kResultTrue;
#endif
}

bool Editor::applyWindowSize(double scale, EditorSize size) {
  const double previousScale = scale_, previousZoom = FairyTapes::Ui::frameZoom(scale_, size_);
  const EditorSize previousSize = size_;
  const Steinberg::ViewRect previous = rect;
  scale_ = scale;   // the host may ask for the size (checkSizeConstraint, getSize) while it answers: these are the new ones
  size_ = size;
  const PixelSize px = windowSize();
  Steinberg::ViewRect r(rect.left, rect.top, rect.left + px.width, rect.top + px.height);
  setRect(r);   // getSize reports the scaled size even before the view is attached
  if (frame) frame->setZoom(FairyTapes::Ui::frameZoom(scale_, size_));   // views keep their logical coordinates
  const bool changed = r.right != previous.right || r.bottom != previous.bottom;
  if (changed && root_ && plugFrame && plugFrame->resizeView(this, &r) != Steinberg::kResultOk) {
    // The host keeps its window: so do we (size, zoom and what getSize reports), and the caller keeps its preference.
    scale_ = previousScale;
    size_ = previousSize;
    setRect(previous);
    if (frame) frame->setZoom(previousZoom);
    return false;
  }
  return true;   // shown: the host answers with onSize
}

void Editor::setSize(EditorSize size) {
  prefs_.size = size;
  if (back_) back_->setSelected(kSetSize, selectedFor(kSetSize, prefs_, 0));
  if (size != size_) applyWindowSize(scale_, size);   // refused: the window stays; the preference applies at the next open
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
  createSides(frame);
  const double zoom = FairyTapes::Ui::frameZoom(scale_, size_);
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
  closeTapeEdit(true);
  flip_.reset();   // a running turn: its view goes away with the frame's
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
  wear_ = nullptr;
  tapes_.clear();
  screws_.fill(nullptr);
  controls_.fill(nullptr);
  match_ = nullptr;
  views_ = nullptr;
  meters_.fill(nullptr);
  grBars_.fill(nullptr);
  clips_.fill(nullptr);
  grTags_ = nullptr;
  lufs_ = nullptr;
  screen_ = nullptr;
  actLeds_.fill(nullptr);
  valueEdit_ = nullptr;
  valueEditId_ = kNumParams;
  tapeEdit_ = nullptr;
  tapeEditSlot_ = -1;
  front_ = nullptr;
  back_ = nullptr;
  backLink_ = nullptr;
  oversamplingNote_ = nullptr;
}

void Editor::createSides(CViewContainer* host) {
  if (!host) return;
  front_ = new CViewContainer(CRect(0, 0, kWidth, kHeight));
  front_->setBackgroundColor(kFrameBackground);
  createViews(front_);
  host->addView(front_);
  buildBack(host);
  flip_ = FairyTapes::Ui::PanelFlip(host, front_, back_, FairyTapes::Ui::frameZoom(scale_, size_));
}

void Editor::createViews(CViewContainer* parent) {
  auto* ctl = getController();
  const CRect all(0, 0, kWidth, kHeight);
  refreshDecor();   // the instance seed, tape texts and screw rotations (no views yet; seed 0 = not ready)
  root_ = parent;

  // 1-2: panel body, grain, boxes, header lines, printed texts and engine tags.
  panel_ = new PanelView(all, &skin_);
  parent->addView(panel_);

  // 3: the silkscreen: names, scales, values and button labels of the controls (clicks go through).
  silk_ = new SilkscreenView(all, silkscreenItems());
  // The values: in the tape font with the normalized digit height while the values are handwritten (the default), else in the
  // silkscreen's value look. The knob values stand at the preview's place (kKnobValueBaselineBelowCap) either way; the values
  // of the cycling selectors stay where the silkscreen puts them.
  silk_->setValueDrawer([this](CDrawContext* ctx, const FairyTapes::Ui::PrintText& t, const CPoint& origin, std::size_t item) {
    static const SharedPointer<CFontDesc> font = makeOwned<CFontDesc>("Arial", FairyTapes::Ui::kSilkValueSize, kBoldFace);
    if (!silk_) return false;
    const FairyTapes::Ui::PrintedItem& it = silk_->model().item(item);
    const bool knob = it.kind == FairyTapes::Ui::PrintKind::Knob;
    const CCoord baseline = knob ? origin.y + it.cy + it.capRadius + kKnobValueBaselineBelowCap : origin.y + t.y;
    if (valuesHandwritten_) {
      const TapeAlign align = t.align == FairyTapes::Ui::TextAlign::Left    ? TapeAlign::Left
                              : t.align == FairyTapes::Ui::TextAlign::Right ? TapeAlign::Right
                                                                            : TapeAlign::Center;
      FairyTapes::Ui::drawTapeText(ctx, valuesFont_, t.text, CPoint(origin.x + t.x, baseline),
                                   FairyTapes::Ui::normalizedFontSize(valuesFont_, FairyTapes::Ui::kSilkValueSize),
                                   FairyTapes::Ui::kAmber, (seed_ ? seed_ : kFallbackHandSeed) + static_cast<std::uint32_t>(it.id) * 131u,
                                   0.5, align);
      return true;
    }
    if (!knob) return false;
    ctx->setFont(font);
    ctx->setFontColor(FairyTapes::Ui::kAmber);
    const CCoord w = ctx->getStringWidth(t.text.c_str());
    ctx->drawString(t.text.c_str(), CPoint(origin.x + FairyTapes::Ui::alignedLeft(t.x, w, t.align), baseline));
    return true;
  });
  parent->addView(silk_);

  // 4-6: wear, paper tapes, screws (the wear lets clicks through; only the tapes and the screws catch them), then the shared
  // screen (clicks go through) and the controls: the screws and the tapes are inserted before the screen. The screen shows
  // the view the controller remembers; its data comes from watchParameters() below and from the timer.
  wear_ = new WearView(all, wearMarks(seed_, wearLevel_));
  parent->addView(wear_);
  screen_ = new ScreenDisplayView();
  if (owner_) screen_->setView(owner_->screenView());
  parent->addView(screen_);
  placeScrews();    // inserted before the screen
  rebuildTapes();   // inserted before the screws

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

  // The way to the back of the panel: a small text at the right of the header (DESIGN_SYSTEM.md, one link only).
  const FairyTapes::Ui::BackBox link = FairyTapes::Ui::frontBackLinkBox(kBackLayout);
  backLink_ = new TextLink(CRect(link.x, link.y, link.x + link.w, link.y + link.h), "BACK  \xE2\x86\x92");
  backLink_->onClick = [this] { startFlip(true); };
  parent->addView(backLink_);

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
  flip_.tick();   // the turn of the panel, if one is running
  if (pending_.exchange(false)) refreshFromController();
  updateDisplays();
  refreshDecor();   // only when the controller's decor revision changed
  // The field reported what was typed when it lost the focus (Enter, a click elsewhere): it is done.
  if (valueEdit_ && frame && frame->getFocusView() != valueEdit_) closeValueEdit(false);
  if (tapeEdit_ && frame && frame->getFocusView() != tapeEdit_) closeTapeEdit(false);
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
  if (screen_) {   // the shared screen: the stage inputs and reductions in dB, the LIMIT threshold, the DYN amounts
    ScreenFrame f;
    f.glueInDb = stageDbFromNorm(display(kDispGlueIn));
    f.glueGrDb = grDbFromNorm(display(kDispGlueGr));
    f.limitInDb = stageDbFromNorm(display(kDispLimitIn));
    f.limitGrDb = grDbFromNorm(display(kGrRows[2].id));   // the L row
    f.limitThreshDb = threshDbFromNorm(display(kDispLimitThresh));
    f.clipInDb = stageDbFromNorm(display(kDispClipIn));
    f.finalInDb = stageDbFromNorm(display(kDispFinalIn));
    f.dynLow = display(kDispDynLow);
    f.dynAir = display(kDispDynAir);
    screen_->tick(f);
  }
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
  if (back_)   // OVERSAMPLING lives on the back: it follows the host (state recall, the generic editor)
    back_->setSelected(kSetOversampling, selectedFor(kSetOversampling, prefs_, indexFromNorm(kOversampling, norm[kOversampling])));
  if (panel_) panel_->setUiState(s);
  if (screen_) screen_->setParams(norm, s);
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
  if (control == tapeEdit_) {   // the writing on a tape: stored unless Escape was pressed
    tapeEditReported_ = true;
    if (!static_cast<ValueEdit*>(tapeEdit_)->cancelled) commitTapeText(tapeEditSlot_, tapeEdit_->getText().getString());
    return;
  }
  if (control == views_) {   // editor state, not a parameter: the controller keeps it while the instance lives
    const auto view = static_cast<ScreenView>(FairyTapes::Ui::choiceIndexFromNorm(control->getValueNormalized(), kNumViews));
    if (owner_) owner_->setScreenView(view);
    if (screen_) screen_->setView(view);
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
  if (!root_ || valueEdit_ || tapeEdit_ || id >= kNumParams) return;
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
  prefs_ = p;   // the fields the editor does not show (notes) are kept for the next save
  prefsFile_ = file;
  setWearLevel(p.wear);
  setTapeFont(p.tapeFont);
  setValuesFont(p.handwrittenValues);
  applyWindowSize(scale_, p.size);
  if (back_) back_->setSelected(kSetSize, selectedFor(kSetSize, prefs_, 0));
}

// ---- Appearance and instance decor: wear, paper tapes, screws ------------------------------------------------------
// The marks of the seed at the level; none while the seed is 0 (the decor is not ready).
const std::vector<WearMark>& Editor::wearMarks(std::uint32_t seed, WearLevel level) {
  static const std::vector<WearMark> none;
  if (seed == 0) return none;
  if (!marksValid_ || seed != marksSeed_ || level != marksLevel_) {
    marks_ = FairyTapes::Ui::generateWear(seed, level, kWidth, kHeight);
    marksSeed_ = seed;
    marksLevel_ = level;
    marksValid_ = true;
  }
  return marks_;
}

void Editor::refreshDecor() {
  if (!owner_) return;
  if (decorKnown_ && owner_->decorRevision() == decorRevision_) return;
  const Decor d = owner_->decor();
  decorRevision_ = owner_->decorRevision();
  decorKnown_ = true;
  const bool textsChanged = d.tapeTexts != tapeTexts_;
  tapeTexts_ = d.tapeTexts;
  for (int i = 0; i < kNumScrewViews; ++i) screwAngles_[static_cast<std::size_t>(i)] = d.screwAngles[i];
  if (d.seed != 0 && d.seed != seed_) {
    seed_ = d.seed;
    applyWear();   // new marks and tapes (with the new texts), the handwriting of the new seed
  } else if (textsChanged) {
    refreshTapeTexts();
  }
  placeScrews();   // a new seed or new rotations (state recall, undo, re-roll); nothing while the seed is 0
}

void Editor::setWearLevel(WearLevel level) {
  prefs_.wear = level;
  if (back_) back_->setSelected(kSetWear, selectedFor(kSetWear, prefs_, 0));
  if (level == wearLevel_) return;
  wearLevel_ = level;
  applyWear();
}

void Editor::setTapeFont(TapeFont font) {
  prefs_.tapeFont = font;
  if (back_) back_->setSelected(kSetTapeFont, selectedFor(kSetTapeFont, prefs_, 0));
  if (font == tapeFont_) return;
  tapeFont_ = font;
  valuesFont_ = tapeFontIdOf(font);
  for (TapeView* t : tapes_) t->setFont(valuesFont_);
  applyHandwriting();
}

void Editor::setValuesFont(bool handwritten) {
  prefs_.handwrittenValues = handwritten;
  if (back_) back_->setSelected(kSetValuesFont, selectedFor(kSetValuesFont, prefs_, 0));
  if (handwritten == valuesHandwritten_) return;
  valuesHandwritten_ = handwritten;
  applyHandwriting();
}

void Editor::applyWear() {
  if (wear_) wear_->setMarks(wearMarks(seed_, wearLevel_));
  applyBackWear();
  rebuildTapes();
  applyHandwriting();   // the handwriting varies with the seed
}

void Editor::applyHandwriting() {
  if (silk_) silk_->invalid();
}

void Editor::reroll() {
  if (!owner_) return;
  closeTapeEdit(true);
  owner_->editDecor(rerolledDecor(owner_->decor(), newRandomSeed()));
  refreshDecor();   // the new revision: new wear, tapes and screws
}

void Editor::rebuildTapes() {
  if (!root_) return;
  closeTapeEdit(true);
  for (TapeView* t : tapes_) root_->removeView(t, true);
  tapes_.clear();
  if (seed_ == 0) return;   // the decor is not ready
  const WearLayout layout = layoutFromSeed(seed_, wearLevel_, Rect{0.f, 0.f, static_cast<float>(kWidth), static_cast<float>(kHeight)});
  CView* above = screen_;   // above the wear, below the screws, the screen and the controls
  for (ScrewView* s : screws_)
    if (s) {
      above = s;
      break;
    }
  for (const TapePlacement& p : layout.tapes) {
    auto* view = new TapeView(p, valuesFont_, seed_);
    view->setText(p.slot < static_cast<int>(tapeTexts_.size()) ? tapeTexts_[static_cast<std::size_t>(p.slot)] : std::string());
    view->onClick = [this](TapeView* t) { editTape(t); };
    if (above) root_->addView(view, above);
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
  if (!root_ || !tape || tapeEdit_ || valueEdit_) return;
  CRect box = tape->tapeRect();
  const CPoint c = box.getCenter();
  const CCoord w = std::max(box.getWidth() + 8, kTapeEditMinWidth);
  box = CRect(c.x - w / 2, c.y - kTapeEditHeight / 2, c.x + w / 2, c.y + kTapeEditHeight / 2);
  if (box.left < 0) box.offset(-box.left, 0);   // stays in the window
  if (box.right > kWidth) box.offset(kWidth - box.right, 0);
  auto* edit = new ValueEdit(box, this, kTapeEditTag + tape->slot(), tape->text().c_str());
  edit->setFont(makeOwned<CFontDesc>("Arial", 13, kBoldFace));
  edit->setFontColor(CColor(0x2b, 0x26, 0x20));
  edit->setBackColor(CColor(0xd9, 0xcf, 0xa6));
  edit->setFrameColor(CColor(0x8f, 0x85, 0x60));
  edit->setHoriAlign(kCenterText);
  tapeEdit_ = edit;
  tapeEditSlot_ = tape->slot();
  tapeEditReported_ = false;
  root_->addView(edit);
  if (frame) frame->setFocusView(edit);   // starts the editing
}

void Editor::commitTapeText(int slot, const std::string& typed) {
  if (!owner_) return;
  const Decor current = owner_->decor();
  const Decor d = withTapeText(current, slot, typed);   // printable ASCII, at most 14 characters
  if (d.tapeTexts == current.tapeTexts) return;          // nothing new: no edit, no dirty mark
  tapeTexts_ = d.tapeTexts;
  owner_->editDecor(d);
  decorRevision_ = owner_->decorRevision();
  refreshTapeTexts();
}

void Editor::closeTapeEdit(bool confirm) {
  if (!tapeEdit_) return;
  CTextEdit* edit = tapeEdit_;
  if (!confirm) static_cast<ValueEdit*>(edit)->cancelled = true;
  if (frame && frame->getFocusView() == edit) {
    frame->setFocusView(nullptr);   // the field reports unless cancelled
  } else if (confirm && !tapeEditReported_ && !static_cast<ValueEdit*>(edit)->cancelled) {
    commitTapeText(tapeEditSlot_, edit->getText().getString());   // no focus to lose (no window yet): confirmed here
  }
  tapeEdit_ = nullptr;
  tapeEditSlot_ = -1;
  if (root_ && root_->isChild(edit)) root_->removeView(edit, true);
}

void Editor::placeScrews() {
  if (!root_ || seed_ == 0) return;   // not before the decor has a seed
  const auto specs = screwSpecs(seed_);
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
    if (screen_) root_->addView(view, screen_);   // over the wear and the tapes, below the screen and the controls
    else root_->addView(view);
    screws_[k] = view;
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
}

// ---- The back of the panel ------------------------------------------------------------------------------------------
int Editor::oversamplingStep() const {
  auto* ctl = getController();
  return ctl ? indexFromNorm(kOversampling, controllerNorm(ctl, kOversampling)) : kParams[kOversampling].defaultIndex;
}

void Editor::buildBack(CViewContainer* host) {
  if (!host) return;
  BackContent content = backContent(prefs_, oversamplingStep());
  for (auto& item : content.items) {
    if (item.kind == BackItemKind::Button) {
      item.onPress = [this] { reroll(); };
    } else {
      const int key = item.key;
      item.onChange = [this, key](int index) { changeSetting(key, index); };
    }
  }
  BackView::Callbacks cb;
  cb.back = [this] { startFlip(false); };
  cb.tabChanged = [this](int tab) { backTab_ = tab; };
  back_ = new BackView(CRect(0, 0, kWidth, kHeight), &skin_, std::move(content), std::move(cb), backTab_, kBackLayout);
  back_->setCustomDrawer(kTabButtons, [](CDrawContext* ctx, const CRect& area) { drawButtonsTab(ctx, area); });
  // The line under OVERSAMPLING, in the BEHAVIOUR box at the preview's place.
  const double x = FairyTapes::Ui::labelX(FairyTapes::Ui::BackColumn::Behaviour, kBackLayout);
  const double right = kBackLayout.behaviour.x + kBackLayout.behaviour.w - FairyTapes::Ui::Back::kLabelInset;
  oversamplingNote_ = new BackNote(CRect(x, kOversamplingNoteBaseline - 12, right, kOversamplingNoteBaseline + 4), back_,
                                   oversamplingNote(), kOversamplingNoteBaseline);
  back_->addView(oversamplingNote_);
  back_->setVisible(false);
  host->addView(back_);
  applyBackWear();
}

void Editor::applyBackWear() {
  if (back_)   // none while the decor is not ready, as on the front
    back_->setWear(seed_ == 0 ? std::vector<WearMark>{}
                              : FairyTapes::Ui::generateWear(seed_ ^ kBackWearSalt, wearLevel_, kWidth, kHeight));
}

// Turns the panel. A text field open on a tape or a value is closed first, keeping what was typed.
void Editor::startFlip(bool toBack) {
  closeValueEdit(true);
  closeTapeEdit(true);
  flip_.start(toBack);
}

void Editor::changeSetting(int key, int index) {
  int step = 0;
  if (oversamplingStepFor(key, index, step)) {   // the stored parameter, one edit; the controller does the rest
    if (step != oversamplingStep()) editParameter(kOversampling, normFromIndex(kOversampling, step));
    return;
  }
  Prefs p = prefs_;
  if (!applySetting(key, index, p)) return;
  if (!prefsFile_.empty()) {
    try {
      savePrefs(prefsFile_, p);   // a failed write is ignored: the choice still applies to this editor
    } catch (...) {
    }
  }
  prefs_ = p;
  setWearLevel(p.wear);
  setTapeFont(p.tapeFont);
  setValuesFont(p.handwrittenValues);
  setSize(p.size);
}

} // namespace FairyTapes::HappyEnding::Ui
