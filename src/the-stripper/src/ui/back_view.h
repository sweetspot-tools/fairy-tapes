// The Stripper GUI: the back of the panel (spec 4, "Retro del pannello"): SETTINGS, BUTTONS (what the buttons impose) and
// CREDITS. Appearance settings are user preferences (saved in the preferences file); the behaviour settings that are not
// available yet are shown dimmed. The editor flips the panel to this view and back.
#pragma once

#include "preferences.h"
#include "ui/back_panel.h"

#include "fairy_ui/skin.h"
#include "fairy_ui/wear.h"

#include "vstgui/lib/cviewcontainer.h"

#include <functional>
#include <vector>

namespace FairyTapes::Ui {
class WearView;
}

namespace FairyTapes::TheStripper::Ui {

class SegmentRow;
class FontStepper;
class TextLink;
class PushButton;

class BackView : public VSTGUI::CViewContainer {
public:
  struct Callbacks {
    std::function<void()> back;                                                                  // "<- BACK"
    std::function<void(const std::function<void(Preferences&)>& mutate)> changePreference;       // the editor saves and applies
    std::function<void()> reroll;                                                                // RE-ROLL
    std::function<void()> manageFolders;                                                         // MODEL FOLDERS
    std::function<void(int mode)> recallModeChanged;                                             // RECALL MARKS: 0 = Auto, 1 = Manual
    std::function<void()> markNow;                                                               // MARK NOW
  };

  BackView(const VSTGUI::CRect& size, const FairyTapes::Ui::Skin* skin, const Preferences& prefs, Callbacks callbacks);

  void setPreferences(const Preferences& prefs);   // refreshes the widgets
  void setWear(std::vector<FairyTapes::Ui::WearMark> marks);
  void setRecallMode(int mode);
  void setTab(BackTab tab);
  BackTab tab() const { return tab_; }

  void drawBackgroundRect(VSTGUI::CDrawContext* context, const VSTGUI::CRect& updateRect) override;
  VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;

protected:
  ~BackView() override;

private:
  void build(const Preferences& prefs);
  void drawSettings(VSTGUI::CDrawContext* ctx);
  void drawButtons(VSTGUI::CDrawContext* ctx);
  void drawCredits(VSTGUI::CDrawContext* ctx);
  void choose(const std::function<void(Preferences&)>& mutate);

  const FairyTapes::Ui::Skin* skin_;
  Callbacks cb_;
  BackTab tab_;
  FairyTapes::Ui::WearView* wear_ = nullptr;
  TextLink* tabLinks_[3] = {};
  std::vector<VSTGUI::CView*> settingsViews_;
  SegmentRow *wearRow_ = nullptr, *notesRow_ = nullptr, *valuesRow_ = nullptr, *holdRow_ = nullptr, *releaseRow_ = nullptr;
  FontStepper* fontStepper_ = nullptr;
  SegmentRow* recallRow_ = nullptr;
};

} // namespace FairyTapes::TheStripper::Ui
