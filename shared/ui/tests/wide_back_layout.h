// A back layout 1280 px wide for the tests of the template: every horizontal number differs from kTall's, so a piece of
// code that still reads the 1000 px constants shows up. (Happy Ending's real back layout lives in the plug-in.)
#pragma once

#include "fairy_ui/back_panel.h"

inline FairyTapes::Ui::BackLayout wideBackLayout() {
  using namespace FairyTapes::Ui;
  BackLayout L = Back::kTall;
  L.width = 1280;
  L.headerRight = 1246;
  L.lastTabRight = 1160;
  L.tabSpacing = 100;
  L.appearance = {24, 102, 602, 364};
  L.behaviour = {654, 102, 602, 364};
  L.notes = {24, 484, 1232, 120};
  L.creditsTitle = {24, 102, 1232, 118};
  L.components = {24, 246, 1232, 252};
  L.notices = {24, 522, 1232, 88};
  L.custom = {24, 102, 1232, 508};
  L.flow = {24, 102, 1232, 388};
  L.legend = {24, 508, 1232, 102};
  L.creditNameX = 50;
  L.creditAuthorX = 450;
  L.creditLicenseRight = 1230;
  return L;
}
