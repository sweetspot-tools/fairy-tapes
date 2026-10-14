# The Stripper → kit comune in `shared/ui` (nota per la chat dello Stripper)

Scritta il 2026-10-14 dalla chat di Pillow Talk (branch `feat/pillow-talk`). Pillow Talk aveva bisogno di usura, serigrafia e helper di disegno dello Stripper: ne esiste ora una **copia comune** in `shared/ui`, namespace `FairyTapes::Ui`. I file dello Stripper **non sono stati toccati**: la migrazione spetta a voi, quando volete. Finché non migrate, il codice è duplicato.

## File dello Stripper duplicati e loro sostituti

| Stripper (`src/the-stripper/src/ui/`) | Comune (`shared/ui/`) | Libreria |
|---|---|---|
| `wear.h` (`TheStripper::Ui::generateWear`, `WearRng`, `WearMark`, `kWearWidth/Height`, `kDefaultWearSeed`) | `include/fairy_ui/wear.h` (`FairyTapes::Ui::…`, stessi nomi) | `fairy_ui` (header-only) |
| `wear_view.{h,cpp}` (`WearView`) | `include/fairy_ui/wear_view.h`, `src/wear_view.cpp` | `fairy_ui_widgets` |
| `draw_util.{h,cpp}` (`kWhite`, `kDimWhite`, `kAmber`, `kPanelBlack`, `drawTitledBox`) | `include/fairy_ui/draw_util.h`, `src/draw_util.cpp` | `fairy_ui_widgets` |
| `silkscreen_view.{h,cpp}` (`SilkscreenView`, `tagPosition`) | `include/fairy_ui/silkscreen.h` (puro) + `include/fairy_ui/silkscreen_view.h`, `src/silkscreen_view.cpp` | `fairy_ui` / `fairy_ui_widgets` |
| `tests/test_wear.cpp` | `shared/ui/tests/test_wear.cpp` (stessi test più valori fissati per il seme `0x51`) | test `fairy_ui_wear_tests` |

Test comuni nuovi: `fairy_ui_wear_tests`, `fairy_ui_silkscreen_tests`, `fairy_ui_layer_view_tests`.

## Cosa sostituire

- **Include:** `"ui/wear.h"` → `"fairy_ui/wear.h"`; `"ui/wear_view.h"` → `"fairy_ui/wear_view.h"`; `"ui/draw_util.h"` → `"fairy_ui/draw_util.h"`; `"ui/silkscreen_view.h"` → `"fairy_ui/silkscreen_view.h"`. `the_stripper` e `the_stripper_render` linkano già `fairy_ui_widgets`; togliere da `CMakeLists.txt` (plugin e `the_stripper_render`) `src/ui/draw_util.cpp`, `src/ui/silkscreen_view.cpp`, `src/ui/wear_view.cpp`, e il target `the_stripper_wear_tests` (coperto da `fairy_ui_wear_tests`).
- **Namespace:** i simboli passano da `FairyTapes::TheStripper::Ui` a `FairyTapes::Ui` (`using FairyTapes::Ui::WearView;` ecc., oppure qualificarli). `draw_util` usa gli stessi nomi di colori: attenzione alle ambiguità se un file fa `using namespace` su entrambi.
- **`WearLevel`:** quello comune è `FairyTapes::Ui::WearLevel { Off, Light, Heavy }`, quello dello Stripper è `FairyTapes::TheStripper::WearLevel` in `preferences.h` (con `name()` e il parsing). Due strade: (a) in `preferences.h` scrivere `using WearLevel = FairyTapes::Ui::WearLevel;` e togliere l'enum (stessi valori e nomi, il file delle preferenze non cambia); (b) tenere l'enum e convertire con un `static_cast<FairyTapes::Ui::WearLevel>(…)` nell'editor. Consigliata la (a).
- **`generateWear`:** firma `generateWear(seed, level, width = 1000, height = 628)`. Per il pannello di riferimento 1000 × 628 i segni sono **identici bit per bit** a quelli dello Stripper (stessa sequenza di chiamate al generatore, stesse costanti; verificato su 6000 combinazioni seme/livello, 1,69 milioni di segni, 0 differenze). Un'altra dimensione scala solo le posizioni.
- **`WearView`:** stessa API (`WearView(rect, marks)`), in più `setMarks()` e `marks()`. Il calcolo del riquadro di un segno è ora `wearMarkBounds()` in `wear.h` (testato).
- **`SilkscreenView`:** non dipende più da `layout.h`. Il costruttore riceve l'elenco degli elementi stampati e le posizioni delle sigle dei blocchi; i setter prendono un `int` (id dell'elemento o chiave del blocco). Adattatore suggerito (in `editor.cpp` o in un nuovo `ui/silkscreen_items.h` dello Stripper, testabile senza finestra):

```cpp
#include "fairy_ui/silkscreen_view.h"
namespace Rack = FairyTapes::Ui::Rack;
using FairyTapes::Ui::PrintedItem; using FairyTapes::Ui::PrintKind; using FairyTapes::Ui::BlockTag;

std::vector<PrintedItem> silkscreenItems() {
  std::vector<PrintedItem> out;
  for (const ControlSpec& c : kControls) {
    if (c.sharesView) continue;   // the old view skipped them when drawing
    PrintedItem it;
    it.id = static_cast<int>(c.id);
    it.cx = c.rect.x + c.rect.w / 2;
    if (c.kind == Kind::Knob) {
      const KnobLook* look = knobLookOf(c.id);
      it.kind = PrintKind::Knob;
      it.cy = c.rect.y + 30;   // cap centre (AnalogKnob::kCenterOffsetY)
      it.capRadius = look ? look->radius : Rack::kDefaultCapRadius;
      if (look) { it.lo = look->lo; it.hi = look->hi; }
      if (c.label) it.name = c.label;
    } else if (c.kind == Kind::Toggle) {
      it.kind = PrintKind::Button;
      it.cy = c.rect.y + Rack::kFaceHeight / 2;
      if (c.label && !isBellButton(c.id)) it.name = c.label;
    } else {
      it.kind = PrintKind::Choice;
      it.cy = c.rect.y + Rack::kFaceHeight / 2;
      it.width = c.rect.w;
      for (int k = 0; k < 3 && c.choices[k]; ++k) it.labels.push_back(c.choices[k]);
    }
    out.push_back(std::move(it));
  }
  return out;
}

std::vector<BlockTag> silkscreenTags() {
  std::vector<BlockTag> out;
  for (Module m : {Module::Filters, Module::Eq, Module::Comp, Module::Drive, Module::Limiter, Module::Gate}) {
    const VSTGUI::CPoint p = tagPosition(m);   // move tagPosition next to the adapter
    out.push_back({static_cast<int>(m), p.x, p.y});
  }
  return out;
}
// editor: silk_ = new FairyTapes::Ui::SilkscreenView(CRect(0, 0, kWidth, kHeight), silkscreenItems(), silkscreenTags());
//         silk_->setTag(static_cast<int>(m), …); silk_->setTagDimmed(static_cast<int>(m), …);
```

  Differenza di comportamento da tenere presente: i setter comuni agiscono su **tutti** gli elementi con quell'id (quelli dello Stripper sul primo); scartando le voci `sharesView` gli id restano unici, quindi il risultato è lo stesso. Il valore giallo si stampa solo se `showValue` (predefinito `true`) e il testo non è vuoto, come prima.

## Come verificare

1. Prima della migrazione: build dei target dello Stripper e dei test, annotare i conteggi (`the_stripper_tests`: 49 test il 2026-10-14) e salvare un render dell'editor con `the_stripper_render` (Wear Light e Heavy, seme predefinito).
2. Dopo: stessi test verdi (tranne `the_stripper_wear_tests`, sostituito da `fairy_ui_wear_tests`), più `ctest -R fairy_ui`.
3. Render dopo la migrazione **identico** a quello di prima (confronto pixel per pixel, ad es. `cmp` dei PNG o differenza con uno script): usura e serigrafia devono coincidere.
4. `nm -D --undefined-only` sul modulo: nessun simbolo `Steinberg::*` indefinito.
5. Aggiornare spec e piano dello Stripper (Task 11) con la migrazione.
