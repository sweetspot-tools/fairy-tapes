// Fairy Tapes shared UI: the back of the panel (docs/DESIGN_SYSTEM.md, "Retro del pannello"), the same template for
// every plug-in; the contents come from the plug-in (fairy_ui/back_panel.h: BackContent). SETTINGS shows the items as
// rows of widgets (fairy_ui/back_widgets.h) and calls their callbacks; CREDITS draws the credits; a custom tab (e.g.
// BUTTONS) is drawn by a function of the plug-in. The wear of the back comes from the plug-in (setWear); clicks never go
// through to the front. The editor shows it with a turn of the panel (fairy_ui/flip_view.h: PanelFlip).
// Generalised from The Stripper's BackView (src/the-stripper/src/ui/back_view.{h,cpp}).
#pragma once

#include "fairy_ui/back_panel.h"
#include "fairy_ui/wear.h"

#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cviewcontainer.h"

#include <functional>
#include <map>
#include <vector>

namespace FairyTapes::Ui {

class Skin;
class WearView;
class TextLink;

class BackView : public VSTGUI::CViewContainer {
public:
  struct Callbacks {
    std::function<void()> back;               // "<- BACK"
    std::function<void(int tab)> tabChanged;  // a tab link was clicked (the plug-in may remember it)
  };
  // Draws a custom tab inside `area` (the custom box of the layout, editor px): the context is already in anti-aliased mode.
  using CustomDrawer = std::function<void(VSTGUI::CDrawContext* context, const VSTGUI::CRect& area)>;

  // The layout is picked from the height of the view (tall 628 or short 470, 1000 px wide).
  BackView(const VSTGUI::CRect& size, const Skin* skin, BackContent content, Callbacks callbacks, int tab = 0);
  // A plug-in with its own panel size passes its layout: header links, columns, boxes and credit columns come from it
  // (the view keeps a copy).
  BackView(const VSTGUI::CRect& size, const Skin* skin, BackContent content, Callbacks callbacks, int tab,
           const BackLayout& layout);

  // Refreshes an item from outside (the value changed elsewhere); no callback is called. Unknown keys are ignored.
  void setSelected(int key, int index);
  int selected(int key) const;
  void setItemEnabled(int key, bool enabled);
  void setTab(int index);
  int tab() const { return tab_; }
  void setWear(std::vector<WearMark> marks);
  void setCustomDrawer(int tab, CustomDrawer drawer);
  const BackContent& content() const { return content_; }
  const BackLayout& layout() const { return layout_; }   // the one given, or tall / short from the height of the view

  void drawBackgroundRect(VSTGUI::CDrawContext* context, const VSTGUI::CRect& updateRect) override;
  VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;

protected:
  ~BackView() override;

private:
  void build();
  void drawSettings(VSTGUI::CDrawContext* ctx, const VSTGUI::CPoint& o);
  void drawCredits(VSTGUI::CDrawContext* ctx, const VSTGUI::CPoint& o);
  void drawRouting(VSTGUI::CDrawContext* ctx, const VSTGUI::CPoint& o);
  void drawRoutingPrims(VSTGUI::CDrawContext* ctx, const VSTGUI::CPoint& o, const RoutingDiagram& prims);
  void text(VSTGUI::CDrawContext* ctx, const std::string& s, double x, double baseline, VSTGUI::CFontDesc* font,
            const VSTGUI::CColor& colour, int align = 0);   // align: 0 left, 1 centre, 2 right
  BackTabKind tabKind() const;

  const Skin* skin_;
  BackLayout layout_;
  BackContent content_;
  Callbacks cb_;
  int tab_ = 0;
  WearView* wear_ = nullptr;
  std::vector<TextLink*> tabLinks_;
  std::vector<VSTGUI::CView*> itemViews_;   // one per content_.items entry, owned by this container
  std::map<int, CustomDrawer> customDrawers_;
  VSTGUI::SharedPointer<VSTGUI::CFontDesc> titleFont_, f10_, f11_, f12_, f13_, f14_;   // all bold, as in the previews
  std::map<int, VSTGUI::SharedPointer<VSTGUI::CFontDesc>> routingFonts_;               // by size, for the diagram
};

} // namespace FairyTapes::Ui
