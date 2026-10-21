// The Stripper GUI: the TONE3000 tab of the model picker. Layout from src/the-stripper/docs/mockups/stripper_model_browser.html.
#include "ui/tone3000_tab.h"

#include "ui/back_widgets.h"
#include "ui/browser_widgets.h"
#include "fairy_ui/draw_util.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"

#include <algorithm>

namespace FairyTapes::TheStripper::Ui {

using FairyTapes::Ui::drawTitledBox;
using FairyTapes::Ui::kAmber;
using FairyTapes::Ui::kDimWhite;
using FairyTapes::Ui::kWhite;
using Tone3000::ClientState;
using Tone3000::ListKind;

using namespace VSTGUI;

namespace {
constexpr CColor kRed(0xe8, 0x39, 0x2b);
constexpr CColor kGreen(0x37, 0xd0, 0x4a);

SharedPointer<CFontDesc> bold(CCoord size, bool italic = false) {
  return makeOwned<CFontDesc>("Arial", size, kBoldFace | (italic ? kItalicFace : 0));
}

// popup coordinates (the popup is 760 x 430)
constexpr double kStatusY = 76;                       // "Signed in as ..." line
constexpr double kBoxTop = 100, kBoxHeight = 256;
constexpr double kTonesX = 20, kTonesW = 360, kModelsX = 394, kModelsW = 346;
constexpr ListKind kKinds[] = {ListKind::Favorited, ListKind::Downloaded, ListKind::Created, ListKind::Trending, ListKind::Latest};
constexpr double kKindWidths[] = {70, 84, 64, 66, 56};
constexpr const char* kCatalogUrl = "https://www.tone3000.com/";
constexpr double kTop = 64;          // the tab starts under the header of the popup, so the header buttons stay reachable
constexpr double kPopupW = 760;
} // namespace

Tone3000Tab::Tone3000Tab(const CRect& popup, Tone3000Actions actions)
: CViewContainer(CRect(popup.left, popup.top + kTop, popup.right, popup.bottom)), actions_(std::move(actions)), popup_(popup) {
  openUrl = actions_.openUrl;   // links of the tab: only tone3000.com addresses are ever passed
  build();
  state_ = actions_.snapshot ? actions_.snapshot() : ClientState{};
  apply();
}

CRect Tone3000Tab::at(double x, double y, double w, double h) const {
  return CRect(x, y - kTop, x + w, y - kTop + h);   // x, y in popup coordinates; the views are placed in the tab's own
}

Tone3000Tab::Mode Tone3000Tab::mode() const {
  if (!actions_.configured) return Mode::NotConfigured;
  switch (state_.phase) {
  case ClientState::Phase::SignedIn: return Mode::SignedIn;
  case ClientState::Phase::SigningIn: return Mode::SigningIn;
  default: return Mode::SignedOut;
  }
}

void Tone3000Tab::build() {
  signIn_ = new PushButton(at(300, 196, 160, 30), "SIGN IN");
  signIn_->onClick = [this] { if (actions_.signIn) actions_.signIn(); };
  addView(signIn_);
  cancel_ = new PushButton(at(300, 196, 160, 30), "CANCEL");
  cancel_->setFontSize(11);
  cancel_->onClick = [this] { if (actions_.cancel) actions_.cancel(); };
  addView(cancel_);

  signOut_ = new PushButton(at(kModelsX + kModelsW - 90, kStatusY - 6, 90, 22), "SIGN OUT");
  signOut_->setFontSize(11);
  signOut_->onClick = [this] { if (actions_.signOut) actions_.signOut(); };
  addView(signOut_);

  double x = kTonesX + 10;
  for (std::size_t i = 0; i < 5; ++i) {
    auto* b = new PushButton(at(x, kBoxTop + 14, kKindWidths[i], 22), listKindLabel(kKinds[i]));
    b->setFontSize(10);
    const ListKind kind = kKinds[i];
    b->onClick = [this, kind] { if (actions_.showList) actions_.showList(kind, 1); };
    addView(b);
    kindButtons_.push_back(b);
    x += kKindWidths[i] + 4;
  }
  toneList_ = new ListBox(at(kTonesX + 10, kBoxTop + 44, kTonesW - 20, 168));
  toneList_->onSelect = [this](int i) { selectTone(i); };
  addView(toneList_);
  prev_ = new PushButton(at(kTonesX + 10, kBoxTop + kBoxHeight - 34, 28, 22), "<");
  prev_->onClick = [this] { if (actions_.showList) actions_.showList(state_.listKind, std::max(1, state_.listPage - 1)); };
  addView(prev_);
  next_ = new PushButton(at(kTonesX + 44, kBoxTop + kBoxHeight - 34, 28, 22), ">");
  next_->onClick = [this] { if (actions_.showList) actions_.showList(state_.listKind, state_.listPage + 1); };
  addView(next_);
  choose_ = new PushButton(at(kTonesX + kTonesW - 10 - 190, kBoxTop + kBoxHeight - 34, 190, 22), "CHOOSE ON TONE3000");
  choose_->setFontSize(11);
  choose_->onClick = [this] { if (actions_.chooseTone) actions_.chooseTone(); };
  addView(choose_);

  modelList_ = new ListBox(at(kModelsX + 10, kBoxTop + 74, kModelsW - 20, 116));
  modelList_->onSelect = [this](int i) { selectModel(i); };
  modelList_->onActivate = [this](int i) { selectModel(i); downloadSelected(); };
  addView(modelList_);
  download_ = new PushButton(at(kModelsX + kModelsW - 10 - 150, kBoxTop + kBoxHeight - 34, 150, 22), "DOWNLOAD & LOAD");
  download_->setFontSize(11);
  download_->onClick = [this] { downloadSelected(); };
  addView(download_);
  toneLink_ = new TextLink(at(kModelsX + 10, kBoxTop + kBoxHeight - 34, 120, 22), "View on TONE3000");
  toneLink_->setLeftAligned(true);
  toneLink_->onClick = [this] {
    if (openUrl && state_.tone && !state_.tone->url.empty()) openUrl(state_.tone->url);
  };
  addView(toneLink_);

  powered_ = new TextLink(at(20, 404, 130, 20), "Powered by TONE3000");
  powered_->setLeftAligned(true);
  powered_->setActive(true);
  powered_->onClick = [this] { if (openUrl) openUrl(kCatalogUrl); };
  addView(powered_);
  catalog_ = new TextLink(at(160, 404, 150, 20), "Browse the full catalog");
  catalog_->setLeftAligned(true);
  catalog_->onClick = [this] { if (openUrl) openUrl(kCatalogUrl); };
  addView(catalog_);
}

void Tone3000Tab::setState(const ClientState& state) {
  state_ = state;
  apply();
}

void Tone3000Tab::poll() {
  if (!actions_.snapshot) return;
  ClientState s = actions_.snapshot();
  if (s.version == lastVersion_) return;
  state_ = std::move(s);
  apply();
}

void Tone3000Tab::apply() {
  lastVersion_ = state_.version;
  const Mode m = mode();
  const bool signedIn = m == Mode::SignedIn;
  signIn_->setVisible(m == Mode::SignedOut);
  cancel_->setVisible(m == Mode::SigningIn);
  signOut_->setVisible(signedIn);
  for (auto* b : kindButtons_) b->setVisible(signedIn);
  toneList_->setVisible(signedIn);
  modelList_->setVisible(signedIn);
  prev_->setVisible(signedIn);
  next_->setVisible(signedIn);
  choose_->setVisible(signedIn);
  download_->setVisible(signedIn);
  toneLink_->setVisible(signedIn && state_.tone && !state_.tone->url.empty());
  catalog_->setVisible(m != Mode::NotConfigured);
  signIn_->setEnabled(m == Mode::SignedOut && !state_.busy);

  if (signedIn) {
    for (std::size_t i = 0; i < kindButtons_.size(); ++i) {
      kindButtons_[i]->setSelected(state_.listLoaded && kKinds[i] == state_.listKind);
      kindButtons_[i]->setEnabled(!state_.busy);
    }
    std::vector<ListBox::Row> rows;
    int selected = -1;
    for (std::size_t i = 0; i < state_.list.size(); ++i) {
      rows.push_back({toneRowText(state_.list[i]), toneRowMeta(state_.list[i]), false});
      if (state_.tone && state_.list[i].id == state_.tone->id) selected = static_cast<int>(i);
    }
    toneList_->setEmptyText(state_.busy ? "" : state_.listLoaded ? "Nothing here yet." : "Pick a list above or choose a tone on TONE3000.");
    toneList_->setRows(std::move(rows));
    toneList_->setSelected(selected);
    const bool paged = listKindIsPaged(state_.listKind);
    prev_->setEnabled(paged && !state_.busy && state_.listPage > 1);
    next_->setEnabled(paged && !state_.busy && state_.listPage < state_.listTotalPages);
    choose_->setEnabled(!state_.busy);

    std::vector<ListBox::Row> models;
    int keep = -1;
    for (std::size_t i = 0; i < state_.models.size(); ++i) {
      models.push_back({modelRowText(state_.models[i]), modelRowMeta(state_.models[i]), false});
      if (selectedModel_ >= 0 && static_cast<int>(i) == selectedModel_) keep = selectedModel_;
    }
    selectedModel_ = keep >= 0 ? keep : (state_.models.size() == 1 ? 0 : -1);
    modelList_->setEmptyText(state_.tone ? "No models for this tone." : "Choose a tone to see its models.");
    modelList_->setRows(std::move(models));
    modelList_->setSelected(selectedModel_);
    download_->setEnabled(canDownload(state_, selectedModel_));
    cancel_->setVisible(state_.busy);
    if (state_.busy) cancel_->setViewSize(at(kModelsX + kModelsW - 10 - 70, kBoxTop + kBoxHeight - 62, 70, 20));
    cancel_->setMouseableArea(cancel_->getViewSize());

    if (state_.download == ClientState::DownloadPhase::Done && !state_.downloadPath.empty() && state_.downloadPath != lastLoaded_) {
      lastLoaded_ = state_.downloadPath;
      if (onLoad) onLoad(state_.downloadPath);
    }
  } else if (m == Mode::SigningIn) {
    cancel_->setViewSize(at(300, 196, 160, 30));
    cancel_->setMouseableArea(cancel_->getViewSize());
  }
  invalid();
}

void Tone3000Tab::selectTone(int index) {
  if (index < 0 || index >= static_cast<int>(state_.list.size())) return;
  const std::string& id = state_.list[static_cast<std::size_t>(index)].id;
  if (state_.tone && state_.tone->id == id) return;   // already showing it
  selectedModel_ = -1;
  if (actions_.showTone) actions_.showTone(id);
}

void Tone3000Tab::selectModel(int index) {
  if (index < 0 || index >= static_cast<int>(state_.models.size())) return;
  selectedModel_ = index;
  download_->setEnabled(canDownload(state_, selectedModel_));
  invalid();
}

void Tone3000Tab::downloadSelected() {
  if (!canDownload(state_, selectedModel_) || !actions_.download) return;
  actions_.download(state_.models[static_cast<std::size_t>(selectedModel_)].id);
}

void Tone3000Tab::drawBackgroundRect(CDrawContext* ctx, const CRect&) {
  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
  const Mode m = mode();
  auto text = [&](const std::string& s, double x, double y, double size, const CColor& c, CHoriTxtAlign align = kLeftText) {
    ctx->setFont(bold(size));
    ctx->setFontColor(c);
    ctx->drawString(s.c_str(), CRect(x, y - kTop - size, x + (align == kLeftText ? 700 : 0), y - kTop + 4), align);
  };
  auto centered = [&](const std::string& s, double y, double size, const CColor& c) {
    ctx->setFont(bold(size));
    ctx->setFontColor(c);
    ctx->drawString(s.c_str(), CRect(40, y - kTop - size, kPopupW - 40, y - kTop + 4), kCenterText);
  };

  if (m == Mode::NotConfigured) {
    centered("TONE3000 is not set up in this build", 190, 15, kWhite);
    centered("Models you save in your folders still work in the LOCAL tab.", 216, 12, kDimWhite);
  } else if (m == Mode::SignedOut) {
    centered("Sign in to TONE3000", 150, 18, kWhite);
    centered("Choose a tone from your account or from the website and load it into the Drive.", 174, 12, kDimWhite);
    const std::string s = statusText(state_);
    if (!s.empty()) centered(s, 252, 12, kAmber);
  } else if (m == Mode::SigningIn) {
    centered("Waiting for your browser...", 160, 16, kWhite);
    centered("Finish the login on the TONE3000 page, then come back here.", 184, 12, kDimWhite);
  } else {
    text("Signed in as", 20, kStatusY + 10, 12, kDimWhite);
    ctx->setFont(bold(12));
    const CCoord w = ctx->getStringWidth("Signed in as ");
    text(state_.userName.empty() ? "TONE3000 user" : state_.userName, 20 + w + 2, kStatusY + 10, 12, kWhite);
    drawTitledBox(ctx, at(kTonesX, kBoxTop, kTonesW, kBoxHeight), "TONES", true);
    drawTitledBox(ctx, at(kModelsX, kBoxTop, kModelsW, kBoxHeight), "MODELS", true);
    if (state_.tone) {
      // the tone, its author and its licence stay in sight (terms of the API): the title is cut to fit, the author is cut
      // before the licence is, never the other way round
      auto measure = [&](double size) { return [&ctx, size](const std::string& str) { ctx->setFont(bold(size)); return ctx->getStringWidth(str.c_str()); }; };
      text(truncateToWidth(toneRowText(*state_.tone), kModelsW - 28, measure(13)), kModelsX + 14, kBoxTop + 34, 13, kWhite);
      const std::string badge = " \xC2\xB7 " + badgeText(*state_.tone);
      ctx->setFont(bold(11));
      const double room = kModelsW - 28 - ctx->getStringWidth(("by " + badge).c_str());
      const std::string who = truncateToWidth(authorText(*state_.tone), std::max(40.0, room), measure(11));
      text(creditLine(*state_.tone, who), kModelsX + 14, kBoxTop + 50, 11, kDimWhite);
      const std::string note = licenseNote(state_.tone->license);
      if (!note.empty()) text(truncateToWidth(note, kModelsW - 28, measure(10)), kModelsX + 14, kBoxTop + 65, 10, kRed);
      if (state_.tone->format == "ir") text("Impulse responses cannot be loaded here.", kModelsX + 14, kBoxTop + 65, 10, kRed);
    } else {
      text("No tone chosen", kModelsX + 14, kBoxTop + 34, 12, kDimWhite);
    }
    const std::string status = statusText(state_);
    if (!status.empty()) {
      const bool bad = state_.download == ClientState::DownloadPhase::Failed || (!state_.message.empty() && !state_.busy);
      text(truncateToWidth(status, kModelsW - 28 - (state_.busy ? 100 : 0), [&](const std::string& s) { ctx->setFont(bold(11)); return ctx->getStringWidth(s.c_str()); }),
           kModelsX + 14, kBoxTop + kBoxHeight - 40, 11, bad ? kRed : kDimWhite);
    }
  }
  ctx->restoreGlobalState();
}

} // namespace FairyTapes::TheStripper::Ui
