// Pillow Talk GUI: the plugin editor. Layout from the approved mockup (spec section 5).
#include "ui/editor.h"

#include "ui/duck_scope.h"
#include "ui/panel_view.h"

#include "fairy_ui/controls.h"
#include "fairy_ui/sanitize.h"

#include "base/source/updatehandler.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "vstgui/lib/cframe.h"

#if SMTG_OS_LINUX
#include "public.sdk/source/vst/vstgui_linux_runloop_support.h"
#include "vstgui/lib/platform/linux/linuxfactory.h"
#include "vstgui/lib/platform/platform_x11.h"
#include "vstgui/lib/platform/platformfactory.h"
#endif

#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace FairyTapes::PillowTalk::Ui {

using namespace VSTGUI;
using FairyTapes::Ui::AnalogKnob;
using FairyTapes::Ui::ChoiceButtons;
using FairyTapes::Ui::NormalizedControl;
using FairyTapes::Ui::ToggleButton;
using Steinberg::Vst::ParamID;

namespace {

static_assert(kNumParams <= 32, "pending_ holds one bit per persistent parameter");

constexpr uint32_t kTimerMs = 30;
constexpr CColor kFrameBackground(0x2a, 0x29, 0x26);

// ---------------------------------------------------------------------------
// Layout (px, editor coordinates). Every control position comes from this table.

enum class Kind { Knob, Toggle, Notes };

struct ControlSpec {
  ParamID id;
  Kind kind;
  CRect rect;
  const char* label;  // knob or button text; unused for Notes (labels come from the parameter)
};

constexpr CCoord kButtonHeight = 24;
constexpr CCoord kNoteWidth = 52;
constexpr CCoord kNoteGap = 6;
constexpr int kNoteCount = 4;

constexpr CRect knobAt(CCoord cx, CCoord cy) {
  return CRect(cx - AnalogKnob::kPreferredWidth / 2, cy - AnalogKnob::kCenterOffsetY,
               cx + AnalogKnob::kPreferredWidth / 2, cy - AnalogKnob::kCenterOffsetY + AnalogKnob::kPreferredHeight);
}
constexpr CRect buttonAt(CCoord x, CCoord y, CCoord w) { return CRect(x, y, x + w, y + kButtonHeight); }
constexpr CRect notesAt(CCoord x, CCoord y) {
  return buttonAt(x, y, kNoteCount * kNoteWidth + (kNoteCount - 1) * kNoteGap);
}

constexpr CPoint kScopeOrigin(24, 64);
constexpr CCoord kColumn[] = {62, 138, 214, 290, 366, 442};  // knob centres
constexpr CCoord kDelayKnobY = 266;
constexpr CCoord kDuckKnobY = 436;
constexpr CCoord kTimeRowY = 336;
constexpr CCoord kHoldRowY = 506;
constexpr CCoord kSyncX = 70;
constexpr CCoord kSyncWidth = 58;
constexpr CCoord kNotesX = 138;

constexpr ControlSpec kControls[] = {
    {kSidechain, Kind::Toggle, buttonAt(304, 18, 70), "Sidechain"},
    {kBypass, Kind::Toggle, buttonAt(382, 18, 74), "Bypass"},
    // Delay
    {kTimeMs, Kind::Knob, knobAt(kColumn[0], kDelayKnobY), "Time"},
    {kFeedback, Kind::Knob, knobAt(kColumn[1], kDelayKnobY), "Feedback"},
    {kTone, Kind::Knob, knobAt(kColumn[2], kDelayKnobY), "Tone"},
    {kSaturation, Kind::Knob, knobAt(kColumn[3], kDelayKnobY), "Saturation"},
    {kWowFlutter, Kind::Knob, knobAt(kColumn[4], kDelayKnobY), "Wow/Flutter"},
    {kMix, Kind::Knob, knobAt(kColumn[5], kDelayKnobY), "Mix"},
    {kTimeSync, Kind::Toggle, buttonAt(kSyncX, kTimeRowY, kSyncWidth), "Sync"},
    {kTimeNote, Kind::Notes, notesAt(kNotesX, kTimeRowY), nullptr},
    // Auto Duck
    {kThreshold, Kind::Knob, knobAt(kColumn[0], kDuckKnobY), "Threshold"},
    {kDuckDepth, Kind::Knob, knobAt(kColumn[1], kDuckKnobY), "Depth"},
    {kHoldMs, Kind::Knob, knobAt(kColumn[2], kDuckKnobY), "Hold"},
    {kRelease, Kind::Knob, knobAt(kColumn[3], kDuckKnobY), "Release"},
    {kHoldSync, Kind::Toggle, buttonAt(kSyncX, kHoldRowY, kSyncWidth), "Sync"},
    {kHoldNote, Kind::Notes, notesAt(kNotesX, kHoldRowY), nullptr},
};
static_assert(std::size(kControls) == kNumParams, "every persistent parameter has one control");

// Sync toggle -> the millisecond knob it replaces and the note buttons it enables.
struct SyncGroup {
  ParamID sync, ms, note;
};
constexpr SyncGroup kSyncGroups[] = {{kTimeSync, kTimeMs, kTimeNote}, {kHoldSync, kHoldMs, kHoldNote}};

constexpr uint32_t bit(ParamID id) { return 1u << id; }

std::string paramString(Steinberg::Vst::EditController* ctl, ParamID id, double norm) {
  Steinberg::Vst::String128 s{};
  if (ctl->getParamStringByValue(id, norm, s) != Steinberg::kResultOk) return {};
  return Steinberg::Vst::StringConvert::convert(s);
}

// Note labels as the controller formats them, one per list entry.
std::vector<std::string> choiceLabels(Steinberg::Vst::EditController* ctl, ParamID id) {
  std::vector<std::string> labels;
  Steinberg::Vst::Parameter* p = ctl->getParameterObject(id);
  const int steps = p ? p->getInfo().stepCount : 0;
  for (int i = 0; i <= steps && steps > 0; ++i)
    labels.push_back(paramString(ctl, id, static_cast<double>(i) / steps));
  return labels;
}

} // namespace

Editor::Editor(Steinberg::Vst::EditController* controller)
: VSTGUIEditor(controller) {
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

  frame = new CFrame(CRect(0, 0, kWidth, kHeight), this);
  frame->setBackgroundColor(kFrameBackground);
  createViews();
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
  scope_ = nullptr;
}

void Editor::createViews() {
  auto* ctl = getController();
  frame->addView(new PanelView(CRect(0, 0, kWidth, kHeight), &skin_));
  scope_ = new DuckScope(kScopeOrigin);
  frame->addView(scope_);

  for (const auto& spec : kControls) {
    NormalizedControl* control = nullptr;
    switch (spec.kind) {
    case Kind::Knob: {
      auto* knob = new AnalogKnob(spec.rect, this, static_cast<int32_t>(spec.id), spec.label, &skin_);
      const ParamID id = spec.id;
      knob->setValueFormatter([ctl, id](float v) { return paramString(ctl, id, v); });
      control = knob;
      break;
    }
    case Kind::Toggle:
      control = new ToggleButton(spec.rect, this, static_cast<int32_t>(spec.id), spec.label, &skin_);
      break;
    case Kind::Notes:
      control = new ChoiceButtons(spec.rect, this, static_cast<int32_t>(spec.id), choiceLabels(ctl, spec.id), &skin_);
      break;
    }
    if (auto* p = ctl->getParameterObject(spec.id))
      control->setDefaultValue(static_cast<float>(p->getInfo().defaultNormalizedValue));
    frame->addView(control);
    controls_[spec.id] = control;
  }
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
  const uint32_t changed = pending_.exchange(0);
  for (ParamID id = 0; id < kNumParams; ++id)
    if (changed & bit(id)) syncFromController(id);

  // Display values: neutral (0 / 1 / 0) until the host forwards the processor's output.
  if (scope_) {
    auto* ctl = getController();
    scope_->push(static_cast<float>(ctl->getParamNormalized(kDispVoice)),
                 static_cast<float>(ctl->getParamNormalized(kDispSend)),
                 static_cast<float>(ctl->getParamNormalized(kDispEcho)));
  }
}

void Editor::syncFromController(ParamID id) {
  auto* ctl = getController();
  const auto* p = ctl->getParameterObject(id);
  const double fallback = p ? p->getInfo().defaultNormalizedValue : 0.0;
  const double v = FairyTapes::Ui::sanitizeNorm(ctl->getParamNormalized(id), fallback);
  if (auto* control = controls_[id]) {
    control->setValueNormalized(static_cast<float>(v));
    control->invalid();
  }
  applySideEffects(id, v);
}

void Editor::applySideEffects(ParamID id, double norm) {
  for (const auto& g : kSyncGroups) {
    if (id != g.sync) continue;
    const bool sync = norm >= 0.5;
    if (auto* ms = controls_[g.ms]) ms->setDimmed(sync);
    if (auto* note = controls_[g.note]) note->setDimmed(!sync);
  }
  if (id == kThreshold && scope_) scope_->setThreshold(static_cast<float>(norm));
}

void Editor::valueChanged(CControl* control) {
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

} // namespace FairyTapes::PillowTalk::Ui
