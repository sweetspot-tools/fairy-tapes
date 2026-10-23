// The Stripper GUI: the model picker popup (spec 3.1, LOCAL tab) as a modal overlay inside the editor frame.
// The folder scan runs on a worker thread; the editor calls poll() from its UI timer to pick up the result.
#pragma once

#include "ui/model_source.h"

#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/icontrollistener.h"
#include "vstgui/lib/cviewcontainer.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace VSTGUI {
class CTextEdit;
}

namespace FairyTapes::TheStripper::Ui {

class PushButton;
class ListBox;
struct ScanState;

class ModelBrowser : public VSTGUI::CViewContainer, public VSTGUI::IControlListener {
public:
  static constexpr int kPopupWidth = 760;
  static constexpr int kPopupHeight = 430;

  // `frameSize`: the overlay covers it and centres the popup. `loadedPath`: path of the model now loaded (green dot).
  ModelBrowser(const VSTGUI::CRect& frameSize, std::string loadedPath);

  std::function<void(const std::string& path)> onLoad;   // LOAD (path) or NO MODEL (empty path)
  std::function<void()> onClose;                         // CANCEL, X or after a load: the editor removes the view

  void poll();   // UI timer: picks up a finished scan

  // IControlListener (search field)
  void valueChanged(VSTGUI::CControl* control) override;

  void drawBackgroundRect(VSTGUI::CDrawContext* context, const VSTGUI::CRect& updateRect) override;
  VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;

protected:
  ~ModelBrowser() override;

private:
  void build();
  void rescan();
  void applyFilter();
  void refreshFolders();
  void addFolder();
  void removeFolder();
  void selectModel(int index);
  void loadSelected();
  VSTGUI::CRect popup() const;   // popup rectangle in view coordinates

  std::string loadedPath_;
  std::vector<std::string> folders_;
  std::vector<ModelEntry> all_, shown_;
  int folderIndex_ = 0;   // 0 = all folders, i > 0 = folders_[i - 1]
  std::string selectedPath_;
  std::string status_;    // line under the model list
  bool scanning_ = false;

  ListBox* folderList_ = nullptr;
  ListBox* modelList_ = nullptr;
  VSTGUI::CTextEdit* search_ = nullptr;
  PushButton* loadButton_ = nullptr;
  PushButton* removeButton_ = nullptr;
  std::shared_ptr<ScanState> scan_;
};

} // namespace FairyTapes::TheStripper::Ui
