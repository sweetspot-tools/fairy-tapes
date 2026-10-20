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
| `tools/gen_tape_fonts.py`, `tools/test_gen_tape_fonts.py` | `tools/gen_tape_fonts.py`, `tools/test_gen_tape_fonts.py` (stesso script, namespace `FairyTapes::Ui::tape_glyphs`, un test in più) | script (Python, `fontTools`) |
| `src/ui/tape_glyphs.h` (generato) | `include/fairy_ui/tape_glyphs.h` (rigenerato il 2026-10-21 dagli stessi tre TTF, SHA-256 uguali: **dati dei glifi identici**, cambiano solo namespace e commento in testa) | `fairy_ui` (header-only) |
| `assets/fonts/` (README con fonti e SHA-256, tre licenze) | `assets/fonts/` (stesse licenze, README aggiornato ai percorsi comuni) | — |
| `src/ui/tape_font.h` (`TapeFontId`, `tapeFontData`, `findGlyph`, `decodeUtf8`, `normalizedFontSize`, `textWidth`, `layoutText`, `fitTapeText`, `tapeFontIdOf`) | `include/fairy_ui/tape_font.h` (stessi nomi e stessa matematica, in più `truncateToWidth` — prima in `model_source.h` — e `fitTapeLine`; **senza** `tapeFontIdOf`, che dipende da `preferences.h`) | `fairy_ui` (header-only) |
| `src/ui/tape_text.{h,cpp}` (`drawTapeText`, `TapeAlign`) | `include/fairy_ui/tape_draw.h`, `src/tape_draw.cpp` (`drawTapeText`, `TapeAlign`, in più `drawTapePaper` e i colori `kTapePaper`, `kTapeShadow`, `kTapeFibre`, `kTapeInk`) | `fairy_ui_widgets` |
| carta di `generateTapePieces` (`outline`, `fibres` in `tape_pieces.h`) e disegno del corpo in `tape_piece_view.cpp` | `include/fairy_ui/tape_paper.h` (`TapePaper`, `makeTapePaper(w, h, WearRng&)` che consuma il generatore **nello stesso ordine**, `tapeBounds`) + `drawTapePaper` | `fairy_ui` / `fairy_ui_widgets` |
| `clampTapeText` (`tape_pieces.h`) | `include/fairy_ui/tape_label.h`: `sanitizeTapeText` (solo ASCII stampabile, spazi ai bordi tolti, al più 14 caratteri) e `fitScale` (mai oltre il nastro). **Regola diversa:** `clampTapeText` tiene l'UTF-8 e cambia i caratteri di controllo in spazi | `fairy_ui` (header-only) |
| `tests/test_tape_font.cpp` | `shared/ui/tests/test_tape_font.cpp` (stessi test tranne la mappatura delle preferenze, più `truncateToWidth` e `fitTapeLine`) e `tests/test_tape.cpp` (testo, scala, carta identica a quella dello Stripper) | test `fairy_ui_tape_font_tests`, `fairy_ui_tape_tests` |
| `src/ui/screws.h` (`TheStripper::Ui::generateScrews(seed)`, `ScrewSpec`, `normalizeScrewAngle`, `screwAngleAfterDrag`, `screwHit`, `kScrewCount`, `kScrewHitRadius`, `kScrewDegreesPerPixel`) | `include/fairy_ui/screws.h` (`FairyTapes::Ui::…`, stessi nomi; `generateScrews(seed, positions = kRackScrewPositions)`, in più `ScrewPoint`, `ScrewPositions`, `kRackScrewPositions` = gli stessi quattro angoli, `kScrewMissingChance`). **Viti identiche** per lo stesso seme: stesso generatore, stesso ordine di estrazioni | `fairy_ui` (header-only) |
| `src/ui/screw_view.{h,cpp}` (`ScrewView`) | `include/fairy_ui/screw_view.h`, `src/screw_view.cpp` (stesso disegno; differenze sotto) | `fairy_ui_widgets` |
| `tests/test_screws.cpp` | `shared/ui/tests/test_screws.cpp` (stessi test più valori fissati presi dallo Stripper) e `tests/test_screw_view.cpp` | test `fairy_ui_screw_tests`, `fairy_ui_screw_view_tests` |
| `src/ui/back_panel.h` (crediti, `wrapText`, `decodeValid`, indici delle impostazioni, `tabLabel`) | `include/fairy_ui/back_panel.h` (puro: geometria fissa del template, `BackContent` con schede, voci, note e crediti forniti dal plugin, `BackItem` scelta/stepper/pulsante con callback, `backContentProblems`, hit test, `wrapText` e `decodeValid` **identici**). Restano dello Stripper: `kCredits`, `helpEntries()`, la mappatura sulle sue `Preferences` | `fairy_ui` (header-only) |
| `src/ui/back_view.{h,cpp}` (`BackView` con le voci scritte a mano) | `include/fairy_ui/back_view.h`, `src/back_view.cpp` (stesso template, contenuti dai dati; scheda personalizzata con `setCustomDrawer`) | `fairy_ui_widgets` |
| `src/ui/back_widgets.{h,cpp}` (`SegmentRow`, `FontStepper`, `TextLink`) e `PushButton` di `browser_widgets.h` | `include/fairy_ui/back_widgets.h`, `src/back_widgets.cpp` (`SegmentRow`, `OptionStepper` = `FontStepper` per qualsiasi elenco, `TextLink`, `PushButton`) | `fairy_ui_widgets` |
| `src/ui/flip_pose.h` | `include/fairy_ui/flip_pose.h` (stessi numeri, in più `flipProgress`, `flipDone`) | `fairy_ui` (header-only) |
| `src/ui/flip_view.{h,cpp}` e `Editor::startFlip`/`finishFlip` | `include/fairy_ui/flip_view.h`, `src/flip_view.cpp` (`FlipView` identica; `PanelFlip` fa istantanee, nasconde i lati, avanza col timer, `reset()` alla chiusura) | `fairy_ui_widgets` |
| `drawPanelBase` (in `src/ui/panel_view.cpp`) | `drawPanelBackground(ctx, rect, skin)` e `kPanelGrainTile` in `include/fairy_ui/draw_util.h` (stesso disegno) | `fairy_ui_widgets` |
| `tests/test_flip.cpp`, parte di `tests/test_back_panel.cpp` (a capo, stepper) | `shared/ui/tests/test_flip.cpp`, `tests/test_back_panel.cpp`, `tests/test_back_view.cpp` | test `fairy_ui_flip_tests`, `fairy_ui_back_panel_tests`, `fairy_ui_back_view_tests` |
| `assets/panel_grain.png` (tessera di grana 256 × 256) | `shared/ui/assets/panel_grain.png` (copia identica byte per byte, aggiunta il 2026-10-15 per Pillow Talk) | copiata nel bundle dal CMake del plugin |

**Grana del pannello:** Pillow Talk copia `shared/ui/assets/panel_grain.png` in `Contents/Resources` del suo bundle (comando `POST_BUILD` in `src/pillow-talk/CMakeLists.txt`) e la carica con lo stesso nome (`panel_grain.png`) tramite `Skin`. Lo Stripper può passare alla copia comune cambiando solo la sorgente del suo `POST_BUILD` (`${CMAKE_CURRENT_SOURCE_DIR}/../../shared/ui/assets/panel_grain.png`) e togliendo la sua `assets/panel_grain.png`; lo script `tools/gen_panel_grain.py` resta lo strumento che la rigenera (scriverla poi in `shared/ui/assets/`).

Test comuni nuovi: `fairy_ui_wear_tests`, `fairy_ui_silkscreen_tests`, `fairy_ui_layer_view_tests`, `fairy_ui_tape_font_tests`, `fairy_ui_tape_tests`, `fairy_ui_screw_tests`, `fairy_ui_screw_view_tests`, `fairy_ui_back_panel_tests`, `fairy_ui_flip_tests`, `fairy_ui_back_view_tests` (più `python3 -m pytest shared/ui/tools/test_gen_tape_fonts.py`).

**Nastro (aggiunto il 2026-10-21, Task 6 di Pillow Talk).** Restano dello Stripper, perché dipendono da `state.h`, dalle preferenze o dal suo layout: `TapePiece` e `generateTapePieces` (posizioni, testi di default, `textSize`), `piecesShown`, `displayedTapeText`, `TapePieceView`, `value_text.h` (valori digitati, nessun duplicato comune). Per passare al comune:
- **Include e namespace:** `"ui/tape_font.h"` → `"fairy_ui/tape_font.h"`, `"ui/tape_text.h"` → `"fairy_ui/tape_draw.h"`, `"ui/tape_glyphs.h"` → `"fairy_ui/tape_glyphs.h"`; i simboli passano a `FairyTapes::Ui` (`TapeFontId`, `drawTapeText`, `TapeAlign`, `normalizedFontSize`, `fitTapeText`, …). `truncateToWidth` esiste ora in entrambi: tenere quella di `model_source.h` o usare la comune, non tutte e due nello stesso `using namespace`.
- **`tapeFontIdOf(TapeFont)`** resta nello Stripper (accanto a `preferences.h` o nell'editor): una riga, come `src/pillow-talk/src/ui/tapes.h`.
- **Carta:** in `generateTapePieces` sostituire i due cicli di `outline` e quello delle `fibres` con `const TapePaper paper = makeTapePaper(p.w, p.h, R);` (stesso `R`, chiamato nello stesso punto) e tenere `paper` nel `TapePiece`; i numeri restano identici (verificato da `test_paper_is_seeded_and_matches_the_stripper`). In `TapePieceView::draw` il blocco di ombra, carta e fibre diventa `drawTapePaper(ctx, piece_.paper, tapeRect(), piece_.rotationDeg);`; il testo resta com'è (o usa `fitTapeLine`, che fa la stessa riduzione al 70 % e lo stesso taglio).
- **CMake:** togliere `src/ui/tape_text.cpp` dalle sorgenti di `the_stripper` e `the_stripper_render`, il target `the_stripper_tape_font_tests` (coperto da `fairy_ui_tape_font_tests`) se si toglie anche `ui/tape_font.h`; poi `src/ui/tape_glyphs.h`, `tools/gen_tape_fonts.py`, il suo test e `assets/fonts/` (le licenze sono in `shared/ui/assets/fonts/`, da citare nei Credits).
- **Verifica:** render prima e dopo con dei testi sui nastri: devono coincidere pixel per pixel.

**Viti (aggiunte il 2026-10-21, Task 7 di Pillow Talk).** Per passare al comune:
- **Include e namespace:** `"ui/screws.h"` → `"fairy_ui/screws.h"`, `"ui/screw_view.h"` → `"fairy_ui/screw_view.h"`; i simboli passano a `FairyTapes::Ui`. `generateScrews(seed)` senza posizioni dà gli stessi angoli del pannello 1000 × 628.
- **`ScrewView`, differenze di comportamento:** (1) `onTurned` scatta **una volta sola, alla fine** di un trascinamento che ha cambiato l'angolo (lo Stripper la chiama a ogni movimento e alla fine); per seguire il movimento c'è `onTurning`. Nell'editor dello Stripper basta collegare `screwsTurned()` a `onTurned`: `setScrewAngles` sarà chiamata una volta per gesto invece che a ogni movimento (meno messaggi al processor; verificare che il progetto risulti comunque modificato). (2) `onMouseCancel` rimette la vite all'angolo di partenza. (3) `hitTest` è vero solo entro `kScrewHitRadius` dal centro e mai per la vite mancante (prima il riquadro 26 × 26 intero); `takesClickAt()` è pubblico. (4) In più `setAngle()` (ignorato durante il trascinamento) e `dragging()`.
- **Angoli salvati:** lo Stripper salva l'angolo assoluto (`hasScrewAngles`), Pillow Talk la rotazione dell'utente sommata a quella iniziale; il kit comune non salva niente, la scelta resta al plugin.
- **CMake:** togliere `src/ui/screw_view.cpp` dalle sorgenti di `the_stripper` e `the_stripper_render` e il target dei test delle viti (coperto da `fairy_ui_screw_tests`); poi `src/ui/screws.h`, `src/ui/screw_view.h`, `tests/test_screws.cpp`.
- **Verifica:** render prima e dopo con alcuni semi (uno con vite mancante, ad es. 3 o 7): devono coincidere pixel per pixel.

**Retro e flip (aggiunti il 2026-10-21, Task 8 di Pillow Talk).** Il retro comune è guidato dai dati: il plugin compone un `BackContent` (riga in alto a destra, schede, voci di SETTINGS con etichette, valori correnti e callback, note, crediti) e `BackView` disegna il template e gestisce i clic. Per passare al comune:
- **Contenuti:** costruire il `BackContent` dello Stripper in una funzione pura (es. `ui/back_content.h`, testabile): schede `{"SETTINGS", Settings}`, `{"BUTTONS", Custom}`, `{"CREDITS", Credits}`; APPEARANCE con `choiceItem` (WEAR, NOTES LAYER, VALUES FONT, METER HOLD, METER RELEASE), `stepperItem(..., tapeFontOptions = true)` per TAPE FONT e `buttonItem` per LOOK; BEHAVIOUR con NAM INFERENCE (`enabled = false`), RECALL MARKS, MARK NOW, MODEL FOLDERS, TONE3000 (`enabled = false`, nota "coming later"); `notes` = le quattro righe di NOTES; `credits` da `kCredits`, `kLicenseLine*`, `kNotices`. Le callback chiamano quello che oggi chiamano `choose(...)`, `cb_.reroll`, ecc.; `setPreferences` diventa una serie di `BackView::setSelected(key, indice)`, `setRecallMode` idem.
- **Scheda BUTTONS:** `back->setCustomDrawer(indiceScheda, [](CDrawContext* ctx, const CRect& area){ ... })` con il corpo di `drawButtons` (riquadro "WHAT THE BUTTONS DO" in `area` = `Back::kCustomBox`); `wrapText` è ora quello comune.
- **Differenze di geometria:** i link delle schede seguono una regola unica (ultima scheda che finisce a 880, passo 90; "← BACK" fino a 966, come `pillow_rack.html`): con tre schede SETTINGS finisce a 700 invece di 690 e CREDITS a 880 invece di 870. Il riquadro CREDITS segue `pillow_rack.html` (titolo 102-220, componenti 246-498, avvisi 522-610); con gli 11 componenti dello Stripper il passo delle righe scende a circa 20 px e il testo a 13/12 px (lo Stripper ora usa 22 px e 13/12). Se serve l'aspetto attuale al pixel, va proposto un parametro nel template comune, non una copia.
- **Flip:** al posto di `flip_`, `flipFrom_`, `flipTo_`, `flipStart_`, `startFlip`, `finishFlip`: un membro `FairyTapes::Ui::PanelFlip flip_;` creato in `open()` con `PanelFlip(frame, front_, backView_, scala)`, `flip_.start(true/false)` (prima chiudere i campi di testo), `flip_.tick()` nel timer, `flip_.reset()` in `close()` prima di `removeAll`. La vista del flip è identica.
- **Pannello:** `drawPanelBase` dello Stripper → `FairyTapes::Ui::drawPanelBase` (stesso disegno; nome della tessera `kPanelGrainTile`).
- **CMake:** togliere `src/ui/back_view.cpp`, `src/ui/back_widgets.cpp`, `src/ui/flip_view.cpp` dalle sorgenti di `the_stripper` e `the_stripper_render` e il target dei test del flip (coperto da `fairy_ui_flip_tests`); `PushButton` resta in `browser_widgets` finché il popup dei modelli lo usa, oppure passa al comune.
- **Verifica:** render del retro (SETTINGS, BUTTONS, CREDITS) prima e dopo; a parte le differenze di geometria dette sopra, devono coincidere.

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
