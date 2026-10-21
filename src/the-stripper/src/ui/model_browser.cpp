// The Stripper GUI: the model picker popup. Layout from src/the-stripper/docs/mockups/stripper_model_browser.html.
#include "ui/model_browser.h"

#include "ui/browser_widgets.h"
#include "ui/tone3000_tab.h"
#include "preferences.h"

#ifdef FAIRY_HAS_TONE3000
#include "tone3000/metadata.h"
#endif
#include "fairy_ui/draw_util.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfileselector.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/ctextedit.h"

#include <cstdlib>
#include <mutex>
#include <thread>

namespace FairyTapes::TheStripper::Ui {

using FairyTapes::Ui::drawTitledBox;
using FairyTapes::Ui::kAmber;
using FairyTapes::Ui::kDimWhite;
using FairyTapes::Ui::kPanelBlack;
using FairyTapes::Ui::kWhite;

using namespace VSTGUI;

// Shared with the scan thread so the view can go away while a scan is still running.
// The licence badge of a model downloaded from TONE3000, from the file kept next to it (only where the client is built).
static void decorateModel(ModelEntry& e) {
#ifdef FAIRY_HAS_TONE3000
  if (const auto meta = Tone3000::readMeta(fromUtf8(e.path))) e.badge = Tone3000::licenseBadge(meta->license);
#else
  (void)e;
#endif
}

struct ScanState {
  std::mutex mutex;
  std::vector<ModelEntry> result;
  bool done = false;
  bool cancelled = false;
};

namespace {

constexpr CColor kScrim(0, 0, 0, 170);
constexpr CColor kPopupEdge(0x2c, 0x2c, 0x2f);
constexpr CColor kFieldBack(0x05, 0x05, 0x05);
constexpr CColor kFieldLine(0x58, 0x58, 0x5c);

// The model folders are a user preference (spec 6): read from the preferences file on first use, saved on every change.
// Until the user touches the list, the picker proposes the existing ones of a short list. Shared by every editor
// instance of the process.
std::vector<std::string> defaultFolders() {
  std::vector<std::string> out;
  if (const char* home = std::getenv("HOME")) {
    for (const char* sub : {"NAM", "Documents/NAM", "Documents/Tone3000"}) {
      const std::string p = std::string(home) + "/" + sub;
      std::error_code ec;
      if (std::filesystem::is_directory(fromUtf8(p), ec)) out.push_back(p);
    }
  }
  return out;
}

std::vector<std::string>& sharedFolders() {
  static std::vector<std::string> folders = [] {
    const Preferences prefs = loadPreferences(preferencesPath("TheStripper"));
    return prefs.modelFoldersConfigured ? prefs.modelFolders : defaultFolders();
  }();
  return folders;
}

void saveSharedFolders(const std::vector<std::string>& folders) {
  sharedFolders() = folders;
  const auto file = preferencesPath("TheStripper");
  Preferences prefs = loadPreferences(file);   // keep the other preferences as they are
  prefs.modelFoldersConfigured = true;
  prefs.modelFolders = folders;
  savePreferences(file, prefs);
}

SharedPointer<CFontDesc> bold(CCoord size, bool italic = false) {
  return makeOwned<CFontDesc>("Arial", size, kBoldFace | (italic ? kItalicFace : 0));
}

constexpr double kBoxTop = 76, kBoxHeight = 280;
constexpr double kFoldersX = 20, kFoldersW = 230, kModelsX = 264, kModelsW = 476;

} // namespace

ModelBrowser::ModelBrowser(const CRect& frameSize, std::string loadedPath, Tone3000Actions tone3000)
: CViewContainer(frameSize), loadedPath_(std::move(loadedPath)), folders_(sharedFolders()), tone3000_(std::move(tone3000)) {
  build();
  rescan();
}

ModelBrowser::~ModelBrowser() {
  if (scan_) {
    std::lock_guard<std::mutex> lock(scan_->mutex);
    scan_->cancelled = true;
  }
}

CRect ModelBrowser::popup() const {
  const CRect r = getViewSize();
  const CCoord x = r.left + (r.getWidth() - kPopupWidth) / 2, y = r.top + (r.getHeight() - kPopupHeight) / 2;
  return CRect(x, y, x + kPopupWidth, y + kPopupHeight);
}

void ModelBrowser::build() {
  const CRect p = popup();
  auto at = [&](double x, double y, double w, double h) { return CRect(p.left + x, p.top + y, p.left + x + w, p.top + y + h); };

  auto* noModel = new PushButton(at(kPopupWidth - 20 - 26 - 8 - 96, 20, 96, 24), "NO MODEL");
  noModel->onClick = [this] {
    if (onLoad) onLoad({});
    if (onClose) onClose();
  };
  addView(noModel);
  auto* close = new PushButton(at(kPopupWidth - 20 - 26, 21, 26, 22), "X");
  close->onClick = [this] { if (onClose) onClose(); };
  addView(close);

  // LOCAL and TONE3000 tabs, next to the title
  localTabButton_ = new PushButton(at(250, 22, 64, 22), "LOCAL");
  localTabButton_->setFontSize(11);
  localTabButton_->onClick = [this] { showTab(false); };
  addView(localTabButton_);
  toneTabButton_ = new PushButton(at(320, 22, 90, 22), "TONE3000");
  toneTabButton_->setFontSize(11);
  toneTabButton_->onClick = [this] { showTab(true); };
  addView(toneTabButton_);

  // Folders
  folderList_ = new ListBox(at(kFoldersX + 10, kBoxTop + 16, kFoldersW - 20, 218));
  folderList_->onSelect = [this](int i) {
    folderIndex_ = i;
    removeButton_->setEnabled(i > 0);
    applyFilter();
  };
  addView(folderList_);
  const double btnY = kBoxTop + 16 + 218 + 8;
  auto* add = new PushButton(at(kFoldersX + 10, btnY, 56, 22), "+ ADD");
  add->onClick = [this] { addFolder(); };
  addView(add);
  removeButton_ = new PushButton(at(kFoldersX + 72, btnY, 74, 22), "REMOVE");
  removeButton_->onClick = [this] { removeFolder(); };
  removeButton_->setEnabled(false);
  addView(removeButton_);
  auto* rescanButton = new PushButton(at(kFoldersX + 152, btnY, 28, 22), "R");
  rescanButton->onClick = [this] { rescan(); };
  addView(rescanButton);

  // Models
  search_ = new CTextEdit(at(kModelsX + 14, kBoxTop + 16, kModelsW - 28, 26), this, 1, "");
  search_->setFont(bold(13));
  search_->setFontColor(kAmber);
  search_->setBackColor(kFieldBack);
  search_->setFrameColor(kFieldLine);
  search_->setPlaceholderString("Search by name");
  search_->setImmediateTextChange(true);
  search_->setHoriAlign(kLeftText);
  addView(search_);
  modelList_ = new ListBox(at(kModelsX + 14, kBoxTop + 50, kModelsW - 28, 196));
  modelList_->onSelect = [this](int i) { selectModel(i); };
  modelList_->onActivate = [this](int i) { selectModel(i); loadSelected(); };
  addView(modelList_);

  // Footer
  auto* cancel = new PushButton(at(kPopupWidth - 20 - 84 - 10 - 84, kPopupHeight - 46, 84, 28), "CANCEL");
  cancel->onClick = [this] { if (onClose) onClose(); };
  addView(cancel);
  loadButton_ = new PushButton(at(kPopupWidth - 20 - 84, kPopupHeight - 46, 84, 28), "LOAD");
  loadButton_->onClick = [this] { loadSelected(); };
  loadButton_->setEnabled(false);
  addView(loadButton_);

  localViews_ = {folderList_, add, removeButton_, rescanButton, search_, modelList_, loadButton_};

  tab_ = new Tone3000Tab(p, tone3000_);
  tab_->onLoad = [this](const std::string& path) {
    if (onLoad) onLoad(path);
    if (onClose) onClose();
  };
  addView(tab_);

  refreshFolders();
  applyTab();
}

void ModelBrowser::showTab(bool tone3000) {
  if (tone3000Tab_ == tone3000) return;
  tone3000Tab_ = tone3000;
  applyTab();
}

void ModelBrowser::applyTab() {
  for (CView* v : localViews_) v->setVisible(!tone3000Tab_);
  tab_->setVisible(tone3000Tab_);
  localTabButton_->setSelected(!tone3000Tab_);
  toneTabButton_->setSelected(tone3000Tab_);
  if (tone3000Tab_) tab_->poll();
  invalid();
}

void ModelBrowser::drawBackgroundRect(CDrawContext* ctx, const CRect&) {
  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
  ctx->setFillColor(kScrim);
  ctx->drawRect(getViewSize(), kDrawFilled);

  const CRect p = popup();
  if (auto path = owned(ctx->createRoundRectGraphicsPath(p, 14))) {
    ctx->setFillColor(kPanelBlack);
    ctx->drawGraphicsPath(path, CDrawContext::kPathFilled);
    ctx->setFrameColor(kPopupEdge);
    ctx->setLineWidth(2);
    ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
  }
  ctx->setFont(bold(22, true));
  ctx->setFontColor(kWhite);
  ctx->drawString("MODEL BROWSER", CPoint(p.left + 20, p.top + 42));

  auto box = [&](double x, double w, const char* title) {
    drawTitledBox(ctx, CRect(p.left + x, p.top + kBoxTop, p.left + x + w, p.top + kBoxTop + kBoxHeight), title, true);
  };
  if (!tone3000Tab_) {
    box(kFoldersX, kFoldersW, "FOLDERS");
    box(kModelsX, kModelsW, "MODELS");

    ctx->setFont(bold(11));
    ctx->setFontColor(kDimWhite);
    ctx->drawString(status_.c_str(), CPoint(p.left + kModelsX + 16, p.top + kBoxTop + kBoxHeight - 10));
    const std::string path = selectedPath_.empty() ? "No selection" : selectedPath_;
    const std::string shown = truncateToWidth(path, kPopupWidth - 40 - 84 * 2 - 10 - 20,
                                              [ctx](const std::string& s) { return ctx->getStringWidth(s.c_str()); });
    ctx->setFont(bold(12));
    ctx->drawString(shown.c_str(), CPoint(p.left + 20, p.top + kPopupHeight - 27));
  }
  ctx->restoreGlobalState();
}

CMouseEventResult ModelBrowser::onMouseDown(CPoint& where, const CButtonState& buttons) {
  CViewContainer::onMouseDown(where, buttons);
  return kMouseEventHandled;   // modal: nothing below the overlay receives clicks
}

void ModelBrowser::valueChanged(CControl* control) {
  if (control == search_) applyFilter();
}

void ModelBrowser::rescan() {
  if (scan_) {
    std::lock_guard<std::mutex> lock(scan_->mutex);
    scan_->cancelled = true;
  }
  auto state = std::make_shared<ScanState>();
  scan_ = state;
  scanning_ = true;
  status_ = "Scanning...";
  invalid();
  std::vector<std::string> folders = folders_;
  const std::string& tones = tone3000_.tonesFolder;   // models saved from TONE3000 are found here without adding the folder
  if (!tones.empty() && std::find(folders.begin(), folders.end(), tones) == folders.end()) folders.push_back(tones);
  std::thread([state, folders] {
    std::vector<ModelEntry> found = scanFolders(folders, 5000, 8, decorateModel);
    std::lock_guard<std::mutex> lock(state->mutex);
    if (!state->cancelled) state->result = std::move(found);
    state->done = true;
  }).detach();
}

void ModelBrowser::poll() {
  if (tone3000Tab_ && tab_) tab_->poll();
  if (!scanning_ || !scan_) return;
  std::lock_guard<std::mutex> lock(scan_->mutex);
  if (!scan_->done) return;
  all_ = std::move(scan_->result);
  scanning_ = false;
  applyFilter();
}

void ModelBrowser::refreshFolders() {
  std::vector<ListBox::Row> rows;
  rows.push_back({"All models", std::to_string(all_.size()), false});
  for (const auto& f : folders_) {
    std::size_t n = 0;
    for (const auto& m : all_) n += m.folder == f;
    std::string label = f;
    const auto slash = label.find_last_of("/\\");
    rows.push_back({slash == std::string::npos ? label : label.substr(slash + 1), std::to_string(n), false});
  }
  folderList_->setRows(std::move(rows));
  if (folderIndex_ > static_cast<int>(folders_.size())) folderIndex_ = 0;
  folderList_->setSelected(folderIndex_);
  removeButton_->setEnabled(folderIndex_ > 0);
}

void ModelBrowser::applyFilter() {
  refreshFolders();
  const std::string folder = folderIndex_ > 0 ? folders_[folderIndex_ - 1] : std::string();
  shown_ = filterModels(all_, search_ ? search_->getText().getString() : std::string(), folder);
  std::vector<ListBox::Row> rows;
  int selected = -1;
  for (std::size_t i = 0; i < shown_.size(); ++i) {
    const ModelEntry& m = shown_[i];
    rows.push_back({displayName(m.name), modelRowMeta(m.badge, m.bytes), m.path == loadedPath_});
    if (m.path == selectedPath_) selected = static_cast<int>(i);
  }
  modelList_->setEmptyText(folders_.empty() ? "Add a folder with your .nam models." : (scanning_ ? "" : "No models found."));
  modelList_->setRows(std::move(rows));
  modelList_->setSelected(selected);
  if (selected < 0) selectedPath_.clear();
  loadButton_->setEnabled(selected >= 0);
  status_ = scanning_ ? "Scanning..." : std::to_string(shown_.size()) + (shown_.size() == 1 ? " model" : " models") +
                                            " \xC2\xB7 the green dot marks the loaded one";
  invalid();
}

void ModelBrowser::selectModel(int index) {
  if (index < 0 || index >= static_cast<int>(shown_.size())) return;
  selectedPath_ = shown_[index].path;
  loadButton_->setEnabled(true);
  invalid();
}

void ModelBrowser::loadSelected() {
  if (selectedPath_.empty()) return;
  if (onLoad) onLoad(selectedPath_);
  if (onClose) onClose();
}

void ModelBrowser::addFolder() {
  CFrame* frame = getFrame();
  if (!frame) return;
  auto* selector = CNewFileSelector::create(frame, CNewFileSelector::kSelectDirectory);
  if (!selector) return;
  selector->setTitle("Add a folder with .nam models");
  if (selector->runModal() && selector->getNumSelectedFiles() > 0) {
    if (const char* dir = selector->getSelectedFile(0)) {
      const std::string folder = dir;
      if (std::find(folders_.begin(), folders_.end(), folder) == folders_.end()) {
        folders_.push_back(folder);
        saveSharedFolders(folders_);
        rescan();
      }
    }
  }
  selector->forget();
}

void ModelBrowser::removeFolder() {
  if (folderIndex_ <= 0 || folderIndex_ > static_cast<int>(folders_.size())) return;
  folders_.erase(folders_.begin() + (folderIndex_ - 1));
  saveSharedFolders(folders_);
  folderIndex_ = 0;
  rescan();
}

} // namespace FairyTapes::TheStripper::Ui
