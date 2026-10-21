// The Stripper GUI: the TONE3000 tab of the model picker (spec 2026-10-21-tone3000-integration-design.md, section 2).
// It only draws what Tone3000Actions::snapshot() says and sends commands; the network work happens elsewhere. Signed out it
// offers SIGN IN; signed in it shows the user's lists (favourites, downloads, ...) or the tone just chosen on the website,
// the tone's models with author and licence, and DOWNLOAD & LOAD. "Powered by TONE3000" is always at the bottom.
#pragma once

#include "tone3000_actions.h"
#include "ui/tone3000_view.h"

#include "vstgui/lib/cviewcontainer.h"

#include <functional>
#include <string>
#include <vector>

namespace FairyTapes::TheStripper::Ui {

class PushButton;
class ListBox;
class TextLink;

class Tone3000Tab : public VSTGUI::CViewContainer {
public:
  // `popup`: the rectangle of the popup, in the coordinates of the browser that holds this tab (the tab covers it).
  Tone3000Tab(const VSTGUI::CRect& popup, Tone3000Actions actions);

  std::function<void(const std::string& path)> onLoad;     // a model was downloaded: load this file
  std::function<void(const std::string& url)> openUrl;     // a link was clicked (set from the actions)

  void poll();                                              // UI timer: takes a new snapshot when it changed
  void setState(const Tone3000::ClientState& state);        // also used directly by the render tool

  void drawBackgroundRect(VSTGUI::CDrawContext* context, const VSTGUI::CRect& updateRect) override;

private:
  enum class Mode { NotConfigured, SignedOut, SigningIn, SignedIn };
  Mode mode() const;
  void build();
  void apply();
  void selectTone(int index);
  void selectModel(int index);
  void downloadSelected();
  VSTGUI::CRect at(double x, double y, double w, double h) const;

  Tone3000Actions actions_;
  VSTGUI::CRect popup_;
  Tone3000::ClientState state_;
  unsigned lastVersion_ = ~0u;
  int selectedModel_ = -1;
  std::string lastLoaded_;                                  // the file already handed to onLoad (once per download)

  std::vector<PushButton*> kindButtons_;
  PushButton *signIn_ = nullptr, *signOut_ = nullptr, *cancel_ = nullptr, *choose_ = nullptr, *prev_ = nullptr, *next_ = nullptr;
  PushButton* download_ = nullptr;
  ListBox *toneList_ = nullptr, *modelList_ = nullptr;
  TextLink *powered_ = nullptr, *catalog_ = nullptr, *toneLink_ = nullptr;
};

} // namespace FairyTapes::TheStripper::Ui
