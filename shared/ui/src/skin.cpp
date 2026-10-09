// Fairy Tapes shared UI: optional bitmap skin.
#include "fairy_ui/skin.h"

#include "vstgui/lib/cresourcedescription.h"

namespace FairyTapes::Ui {

VSTGUI::CBitmap* Skin::bitmap(const char* name) const {
  if (name == nullptr || *name == '\0') return nullptr;
  auto [it, inserted] = cache_.try_emplace(name);
  if (inserted) {
    // CResourceDescription keeps the raw pointer: use the map key, which outlives the bitmap.
    auto bmp = VSTGUI::makeOwned<VSTGUI::CBitmap>(VSTGUI::CResourceDescription(it->first.c_str()));
    if (bmp->isLoaded()) it->second = bmp;
  }
  return it->second.get();
}

} // namespace FairyTapes::Ui
