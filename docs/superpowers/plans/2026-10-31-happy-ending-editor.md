# Happy Ending Editor (GUI) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Dare a Happy Ending un editor VSTGUI uguale alla preview HTML (pannello 1280 × 890): fronte con tutti i comandi, LEVEL e LUFS vivi, schermo condiviso a cinque viste, retro con SETTINGS (SIZE, OVERSAMPLING), BUTTONS, ROUTING e CREDITS.

**Architecture:** Struttura copiata da Deep Throat dentro `src/happy-ending/src/ui/` (layout puro in `layout.h`, `editor`, viste per livelli, decor e preferenze) con i widget di `shared/ui`. Il motore pubblica altri valori di visualizzazione (livelli d'ingresso degli stadi per lo schermo); le curve EQ le calcola l'editor con copie proprie di `FineEq` e `VibeEq`. `shared/ui` riceve tre aggiunte comuni: la matematica di SIZE, un retro che prende la geometria dal plugin (ogni plugin ha la sua dimensione, anche il retro), viti per qualsiasi dimensione di pannello. Lo stato passa alla v2 con il decor.

**Tech Stack:** C++20, VST3 SDK, VSTGUI4, `shared/ui` (`fairy_ui`, `fairy_ui_widgets`), CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-10-21-happy-ending-design.md` (§3-§7 pannello e comandi, §9 stato e display, **§13 editor**: le scelte tecniche di questo piano). Riferimento visivo e fonte delle coordinate: `src/happy-ending/docs/mockups/happy_ending_rack.html`. Modelli di codice: `src/deep-throat/src/ui/*`, `src/deep-throat/src/{state.h,messages.h,controller.*}`, `src/deep-throat/tools/render_editor.cpp`, `src/deep-throat/tests/{test_ui,test_editor,test_state_module}.cpp`; per la scheda BUTTONS `src/the-stripper/src/ui/buttons_tab.*`.

## Global Constraints

- Namespace `FairyTapes::HappyEnding` (UI in `...::Ui`). Si tocca `src/happy-ending/` e, solo nel Task 1, `shared/ui/`: **le aggiunte a `shared/ui` non cambiano nulla per gli altri plugin** (vecchie firme e geometrie restano, i loro test restano verdi). Gli altri plugin non si toccano: l'adozione di SIZE da parte loro è lavoro delle loro chat.
- Pannello **1280 × 890**; titoli dei riquadri sul bordo **basso**; IN in alto a destra; regole di `docs/DESIGN_SYSTEM.md` (livelli dal fondo: pannello, bordi e titoli, serigrafia, usura, viti, controlli; attenuazione al 35 %; tre tipi di pulsante).
- **Coordinate:** la preview sposta le righe con gruppi SVG annidati (`STAGES` +202 in y; dentro, `VG2` −216 per VIBE EQ e GLUE e `SMALL` −484 per CLIP, LIMIT, FINAL, METALHEAD). Coordinata finale = valore scritto + spostamenti dei gruppi che la contengono (es. FINE EQ `box(24,100,…)` → y 302; VIBE EQ `box(24,600,…)` → y 586; CLIP `box(24,1040,…)` → y 758; la riga di servizio, y 92, non è spostata). In caso di dubbio vale la preview aperta nel browser (`getBBox` con `getCTM`). I valori dei parametri (scale, passi, default) vengono da `params.h`, **non** dalla preview (es. HI PASS ha sei posizioni, la preview cinque).
- Ogni parametro della tabella (`kNumParams`) è legato a **un solo** controllo, più LISTEN (tre pulsanti, un parametro) e SET; `kOversampling` sta nel retro (BEHAVIOUR), non sul fronte. I parametri di visualizzazione restano `kIsReadOnly | kIsHidden`, fuori da stato e automazione.
- Testo visibile **in inglese**; identificatori e commenti in inglese; documentazione in italiano. Nessun nome di fonte nella UI tranne le sigle di §7 (`MULTI-Q`, `TUBE-1A`, `4K-G`, `C-SOFTLY`, `INFL-Z` / `C-ONLY2`); nei CREDITS i nomi per esteso.
- Nessuna allocazione nel thread audio; il processor non dipende dalle viste; la UI non include nulla del processor. Le copie di `FineEq`/`VibeEq` dell'editor non girano mai sull'audio.
- `kStateVersion = 2` (v1 + `uint32` seme + 4 × `float32` angoli viti + `uint32` numero testi + per testo `uint32` lunghezza e byte; al massimo 6 testi da 14 caratteri ASCII stampabili); la v1 si carica con il decor di default; uno stato v2 corrotto nella parte decor carica i parametri e dà il decor di default (la parte parametri segue le regole già esistenti di `state.h`).
- Preferenze: `<cartella preferenze>/FairyTapes/HappyEnding.prefs` con le chiavi di Deep Throat (`wear`, `notes`, `tapeFont`, `valuesFont`) più `size=75|90|100|125` (default 100): SIZE è una preferenza **di ciascun plugin** (decisione dell'utente, 2026-10-31), non della suite.
- LISTEN e SET si spengono alla chiusura dell'editor e non entrano mai nello stato (già vero nel motore: l'editor deve solo spegnerli).
- Test: harness `RUN_TEST`/`ASSERT_TRUE` del plugin; ogni comportamento nasce con un test che prima fallisce; ogni test deve poter fallire per un'implementazione sbagliata (nel rapporto: come; mutazione dimostrata per i 2-3 più importanti).
- Build fuori dal repo, `-j2`, solo i target che servono: `cmake -S . -B /tmp/he-build -DCMAKE_BUILD_TYPE=Release -DGPU_NAM_DISABLE_CUDA=ON`, poi `cmake --build /tmp/he-build --target <target> -j2` e `ctest --test-dir /tmp/he-build -R happy_ending --output-on-failure`. Nuovi eseguibili: `happy_ending_ui_tests` (`tests/test_ui.cpp`, puro, collegato a `fairy_ui`), `happy_ending_editor_tests` (`tests/test_editor.cpp`, banco senza finestra, Task 8), `happy_ending_state_module_test` (`tests/test_state_module.cpp`, Task 3), `happy_ending_render` (`tools/render_editor.cpp`, Task 8) con il test `happy_ending_render_smoke`. I moduli che caricano il bundle hanno `add_dependencies(... happy_ending)`.
- Commit per nome (mai `git add -A`), messaggi in inglese nello stile `tipo(happy-ending): …` (`tipo(ui): …` per `shared/ui`), **senza** riga `Co-Authored-By` (regola dell'utente); niente push.

## Review Focus

- Aprire e chiudere l'editor più volte, con il timer attivo e con LISTEN o SET accesi: nessun crash, LISTEN e SET spenti dopo ogni chiusura (Task 8, 9).
- Automazione dell'host su tutti i parametri con l'editor aperto, anche il cambio di FINAL mode e di M/S su SIDE (la scala del HI PASS cambia): la UI segue e non rimanda modifiche all'host (Task 9).
- SIZE cambiato con l'editor aperto, host che rifiuta il ridimensionamento, fattore di scala dell'host diverso da 1: finestra coerente e nessun crash (Task 1, 13).
- Stato v1 di un progetto salvato prima dell'editor, stato v2 con decor corrotto o troncato: parametri caricati, decor di default, nessun crash (Task 3).
- Preferenze mancanti, corrotte o non scrivibili, grana mancante, Wear Off e Heavy: l'editor si apre con i default (Task 1, 7, 12).

---

### Task 1: `shared/ui` — matematica di SIZE, retro con la geometria del plugin, viti per ogni pannello

**Files:**
- Modify: `shared/ui/include/fairy_ui/editor_size.h`, `shared/ui/include/fairy_ui/back_panel.h`, `shared/ui/src/back_view.cpp`, `shared/ui/src/back_widgets.cpp` (dove leggono costanti orizzontali), `shared/ui/include/fairy_ui/screws.h` (solo se le posizioni sono fisse a 1000 × 628)
- Test: `shared/ui/tests/test_editor_size.cpp`, `shared/ui/tests/test_back_panel.cpp`, `shared/ui/tests/test_back_view.cpp`, `shared/ui/tests/test_screws.cpp`

**Interfaces:**
- Produces in `editor_size.h`: `enum class EditorSize { S75, S90, S100, S125 };` `constexpr double sizeFactor(EditorSize)` (0.75, 0.9, 1.0, 1.25); `PixelSize scaledSize(int w, int h, double hostFactor, EditorSize size)` (= `lround(w · sanitize(host) · size)`; la vecchia `scaledSize(w, h, factor)` resta e vale `EditorSize::S100`); `double frameZoom(double hostFactor, EditorSize)` (lo zoom da dare al `CFrame`); `const char* sizeLabel(EditorSize)` ("75 %" …) e `bool parseSizeLabel(const std::string&, EditorSize&)` ("75", "90", "100", "125": servono ai file di preferenze dei plugin, ognuno ha il suo). Nessun file di preferenze comune: SIZE si salva nel file di ciascun plugin.
- Produces in `back_panel.h`: **ogni plugin ha la sua dimensione, anche per il retro.** `struct BackLayout` guadagna `double width` e i campi orizzontali che oggi sono costanti in `Back::` (bordo destro dell'intestazione, destra dell'ultimo link delle schede, passo dei link, x e larghezza delle colonne APPEARANCE e BEHAVIOUR, larghezza dei riquadri pieni); `kTall` e `kShort` restano con width 1000 e gli stessi numeri di oggi; `BackView` e i widget del retro leggono tutta la geometria dal `BackLayout` che il plugin passa (oggi lo ricavano da `backLayoutFor(height)`, che resta per chi non passa nulla); `tabLinkBox(index, count, const BackLayout&)` e `backLinkBox(const BackLayout&)` con le vecchie firme che restano. I numeri di Happy Ending non stanno in `shared/ui`: li definisce il plugin (Task 13).
- Produces in `screws.h` (se serve): le posizioni delle quattro viti a 18 px dagli angoli per una larghezza e un'altezza qualsiasi (`screwPositions(double w, double h)`), con l'aspetto dal seme invariato.

- [x] **Step 1: Scrivere i test:** `test_size_factors` (75/90/100/125 → 0.75/0.9/1.0/1.25), `test_size_labels_roundtrip` (etichette e analisi; "33" → `false`), `test_scaled_size_with_size` (1280 × 890, host 1.5, S90 → 1728 × 1202; host NaN → come 1; la vecchia firma dà lo stesso risultato di S100), `test_back_layouts_unchanged` (`kTall` e `kShort` hanno width 1000 e i campi orizzontali uguali alle vecchie costanti), `test_back_layout_any_width` (un `BackLayout` di prova largo 1280: link delle schede e "← BACK" calcolati dai suoi campi; i riquadri dentro la larghezza), `test_back_view_uses_given_layout` (in `test_back_view.cpp`: con un layout largo 1280 il clic sul link CREDITS al suo posto nuovo cambia scheda, e al posto vecchio no), `test_screw_positions_any_panel` (1280 × 890 → (18,18), (1262,18), (18,872), (1262,872); 1000 × 628 → le posizioni di oggi).
- [x] **Step 2: Eseguire e vedere fallire.**
- [x] **Step 3: Implementare.**
- [x] **Step 4: Eseguire** `cmake --build /tmp/he-build --target fairy_ui_editor_size_tests fairy_ui_back_panel_tests fairy_ui_back_view_tests fairy_ui_screw_tests deep_throat_editor_tests pillow_talk_editor_tests -j2` (i nomi dei target degli altri plugin si verificano con `cmake --build /tmp/he-build --target help`) e `ctest --test-dir /tmp/he-build -R "fairy_ui|deep_throat_editor|pillow_talk" --output-on-failure` → tutto verde, gli altri plugin invariati.
- [x] **Step 5: Aggiornare `docs/DESIGN_SYSTEM.md`** (SIZE: preferenza di ciascun plugin, nel suo file; retro: la geometria segue la dimensione di ciascun plugin) e **commit:** `feat(ui): SIZE math, back template with the plug-in's own geometry and screws for any panel`.

---

### Task 2: Valori di visualizzazione per lo schermo

**Files:**
- Modify: `src/happy-ending/src/{params.h,engine.h,engine.cpp,display.h,processor.cpp,controller.cpp}`, `src/happy-ending/tests/{test_engine.cpp,test_display.cpp,test_factory.cpp,test_processor_levels.cpp}`

**Interfaces:**
- Produces in `params.h`, **dopo** `kResetIntegrated` (gli ID esistenti non cambiano): `kDispGlueIn, kDispGlueGr, kDispLimitIn, kDispLimitThresh, kDispClipIn, kDispFinalIn` prima di `kNumDisplayIDs`.
- Produces in `Meters` (`engine.h`): `float stageInDb[4]` (picco all'ingresso di GLUE, LIMIT, CLIP, FINAL: GLUE alla frequenza base, gli altri alla sovracampionata; −120 a stadio spento), `float glueGrDb` (riduzione del compressore prima del MIX, `Glue::grDb()`), `float limitThreshDb` (soglia effettiva del LIMIT in questo momento: THRESH più la spinta della LOUDNESS e la discesa di SET).
- Produces in `display.h`: `constexpr double kStageDisplayMinDb = -60.0, kStageDisplayMaxDb = 12.0;` `normFromStageDb`/`stageDbFromNorm`; `normFromThreshDb`/`threshDbFromNorm` su −12…0 dB; `DisplayFrame` e `MeterHold` estesi (ingressi degli stadi e GR della colla: massimo dalla scrittura precedente; soglia: ultimo valore).

- [ ] **Step 1: Scrivere i test:** in `test_engine.cpp` `test_stage_input_peaks` (seno a −6 dBFS, tutti gli stadi accesi con valori neutri: i quattro `stageInDb` ≈ −6 ± 0.5; con CLIP spento il suo ingresso è −120), `test_glue_gr_before_mix` (MIX 50 % e segnale forte: `glueGrDb > grDb[kGrGlue]`), `test_limit_thresh_follows_loudness_and_set` (THRESH −2: `limitThreshDb` = −2; con LOUDNESS 5 scende della spinta di `kLoudCurve`; con SET va verso −8 entro 150 ms). In `test_display.cpp`: conversioni avanti e indietro, estremi e NaN. In `test_factory.cpp`: i sei nuovi ID esistono, `kIsReadOnly | kIsHidden`, conteggio dei parametri automatizzabili invariato. In `test_processor_levels.cpp`: i nuovi parametri arrivano in uscita con il limitatore di 33 ms; nessuna allocazione in `process`.
- [ ] **Step 2: Eseguire e vedere fallire.**
- [ ] **Step 3: Implementare** misure nel motore (senza allocare), `MeterHold`, scrittura nel processor, registrazione nel controller.
- [ ] **Step 4: Eseguire** `ctest -R happy_ending` → tutto verde (i test esistenti invariati).
- [ ] **Step 5: Commit:** `feat(happy-ending): stage input, glue and limiter threshold display values for the screen`.

---

### Task 3: Stato v2, decor e messaggi

**Files:**
- Modify: `src/happy-ending/src/{state.h,messages.h,controller.h,controller.cpp,processor.h,processor.cpp}`, `src/happy-ending/tests/test_state.cpp`, `src/happy-ending/CMakeLists.txt`
- Create: `src/happy-ending/tests/test_state_module.cpp` (`happy_ending_state_module_test`, modello `src/deep-throat/tests/test_state_module.cpp` e il suo blocco CMake; exit 77 se il bundle manca; `add_dependencies(... happy_ending)`)

**Interfaces:**
- Produces in `state.h` (modello `src/deep-throat/src/state.h`): `constexpr std::int32_t kStateVersion = 2; constexpr int kNumScrews = 4, kMaxTapeTexts = 6, kMaxTapeChars = 14; struct Decor { std::uint32_t seed = 0; float screwAngles[4] = {}; std::vector<std::string> tapeTexts; };` `serializeState(const double*, const Decor&, std::vector<std::uint8_t>&)`, `bool deserializeState(const std::uint8_t*, std::size_t, double*, Decor&)` (v1 e v2), `serializeDecor`/`deserializeDecor`, `void ensureSeed(Decor&)`, `kMaxStateBytes` aggiornato. La regola "blob più corto = carica ciò che c'è" della v1 resta per la parte dei parametri.
- Produces in `messages.h`: `kMsgDecor = "Decor"`, `encodeDecorMessage`, `decodeDecorMessage` (accanto al messaggio Oversampling).
- Produces in `Controller`: `Decor decor() const; std::uint32_t decorRevision() const; void editDecor(Decor d); tresult PLUGIN_API notify(IMessage*) override;` (mai nel thread audio). `Processor::getState/setState` salvano e caricano il decor, lo mandano al controller dopo `setState` e alla connessione.

- [ ] **Step 1: Scrivere i test** (modello Deep Throat, Task 3): `test_state_v2_roundtrip`, `test_state_v1_loads_with_default_decor` (un blob v1 costruito a mano: tutti i parametri, `seed == 0`; `ensureSeed` dà un seme ≠ 0 e stabile), `test_state_v1_short_blob_still_loads` (il comportamento v1 di oggi resta), `test_state_v2_corrupt_decor` (decor troncato a ogni lunghezza, numero di testi enorme, lunghezza oltre il blob → parametri caricati, decor di default), `test_decor_sanitised` (angoli non finiti o fuori giro → [0, 360); testi non ASCII o lunghi → ASCII stampabile, 14 caratteri; più di 6 testi → 6), `test_decor_message_roundtrip`; nel modulo: `setState` v2 seguito da `getState` restituisce lo stesso blob; il controller riceve il decor tramite `notify`; LISTEN e SET non compaiono nel blob.
- [ ] **Step 2: Eseguire e vedere fallire.**
- [ ] **Step 3: Implementare** sul modello di Deep Throat.
- [ ] **Step 4: Eseguire** `ctest -R happy_ending` → verde.
- [ ] **Step 5: Commit:** `feat(happy-ending): state v2 with the instance decor and controller/processor messages`.

---

### Task 4: Layout puro dell'editor

**Files:**
- Create: `src/happy-ending/src/ui/{editor_size.h,layout.h}`, `src/happy-ending/tests/test_ui.cpp`
- Modify: `src/happy-ending/CMakeLists.txt` (`happy_ending_ui_tests`, `fairy_ui`, `add_test`)

**Interfaces:**
- Produces in `editor_size.h`: `constexpr int kEditorWidth = 1280, kEditorHeight = 890;` `PixelSize scaledEditorSize(double hostFactor, EditorSize size)`.
- Produces in `layout.h` (modello `src/deep-throat/src/ui/layout.h`): `struct Rect`, `enum class Box { Input, Level, Loudness, Output, FineEq, Screen, VibeEq, Glue, Clip, Limit, Final, Metal }` (lo schermo è un riquadro senza titolo), `struct BoxSpec { Box box; Rect rect; const char* title; }`, `kBoxes`; `enum class Kind { Knob, Toggle, LitChoice, Cycle, Action, HeadToggle }`, `struct ControlSpec { ParamID id; int choiceIndex; /* per LitChoice e LISTEN: quale scelta è questo pulsante, -1 altrimenti */ Kind kind; Box box; Rect rect; const char* name, *lo, *hi; float capRadius; }`, `kControls` (ogni ID da 0 a `kNumParams − 1` tranne `kOversampling`, più `kListenBand` ×3 e `kSetActive`; FINAL: EFFECT, CURVE e DRIVE ciascuno col proprio rettangolo, visibili per modo), `const ControlSpec* controlOf(ParamID, int choiceIndex = -1)`, `Rect viewRect(const ControlSpec&)`; le azioni senza parametro: `enum class Action { Match, ViewEq, ViewGlue, ViewClip, ViewLimit, ViewFinal }` con `kActions` (rettangoli); `kScreenRect`, `kMeterRows` (IN L/R, OUT L/R), `kGrRows` (G C L F M), `kClipLeds` (9, ordine di §3), `kLufsScreenRect`, `kActLeds` (LOW, AIR), `kTargetLed`; tag dei motori `const char* engineTag(Box, int finalMode)` (FINAL: `INFL-Z` / `C-ONLY2`; LIMIT, METALHEAD, INPUT, OUTPUT, LEVEL, LOUDNESS: `nullptr`); `kPanelTexts` ("SHAME ON YOU", "level matched" su due righe, ecc.); `std::vector<FairyTapes::Ui::PrintedItem> silkscreenItems()`; `kTapeSlots`, `kScrewPos[4][2]` (da `screwPositions(1280, 890)`, Task 1).
- Produces le regole di attenuazione e blocco (pure): `struct UiState { bool bypass; bool stageOn[7]; /* FINE VIBE GLUE CLIP LIMIT FINAL METAL */ bool bandOn[6]; int listen; bool set; int finalMode; }`, `UiState uiStateFrom(const double* norm, int listen, bool set)`, `bool controlDimmed(const ControlSpec&, const UiState&)`, `bool controlLocked(const ControlSpec&, const UiState&)`, `bool controlVisible(const ControlSpec&, const UiState&)`. Regole (spec §3, §5): BYPASS attenua e blocca tutto tranne sé stesso, meter, LUFS (e i LED di clip di IN e OUT); IN di stadio spento: attenua e blocca lo stadio tranne il suo IN; banda spenta: attenua e blocca la banda tranne il suo IN; LISTEN su una banda: le altre bande attenuate e bloccate (IN compresi); SET: LIMIT attenuato e bloccato tranne RELEASE e SET; METALHEAD spento: riquadro attenuato tranne l'IN; FINAL: visibili solo i comandi del modo scelto.

- [ ] **Step 1: Scrivere i test** in `tests/test_ui.cpp`: `test_layout_binds_every_param_once` (ogni ID della tabella tranne `kOversampling` in esattamente un `ControlSpec`, LISTEN in tre, SET in uno, nessun ID di visualizzazione), `test_layout_rects_inside` (riquadri dentro 1280 × 890 e senza sovrapposizioni; ogni `viewRect` dentro il suo riquadro), `test_layout_no_control_overlap` (tranne EFFECT/CURVE contro DRIVE di FINAL, che non sono mai visibili insieme), `test_in_buttons_top_right` (centro dell'IN di stadio a `x+w−29`, `y+38` ± 2 px), `test_titles_bottom_and_tags` (titoli non vuoti in maiuscolo; `engineTag` per ogni riquadro come sopra), `test_dimming_rules` (un caso per regola, compresi LISTEN che blocca l'IN delle altre bande e SET che lascia libero RELEASE), `test_final_mode_visibility`, `test_silkscreen_items_cover_controls` (un elemento per manopola e pulsante; nessuna stringa contiene "pultec", "ssl", "airwindows", "dusk" in minuscolo).
- [ ] **Step 2: Eseguire e vedere fallire.**
- [ ] **Step 3: Implementare** trascrivendo la preview con la regola delle coordinate dei vincoli globali.
- [ ] **Step 4: Eseguire** `ctest -R happy_ending_ui_tests` → PASS.
- [ ] **Step 5: Commit:** `feat(happy-ending): editor layout table and dimming rules (pure)`.

---

### Task 5: Testi dei valori e letture (puri)

**Files:**
- Create: `src/happy-ending/src/ui/value_text.h`, `src/happy-ending/src/ui/readouts.h`
- Modify: `src/happy-ending/tests/test_ui.cpp`, eventualmente `src/happy-ending/src/controller.cpp` (se i formattatori del controller diventano funzioni pure condivise)

**Interfaces:**
- Produces in `value_text.h`: `std::string formatValue(ParamID, double norm, const double* allNorms)` (il valore stampato sotto ogni comando; `allNorms` serve al HI PASS, la cui scala dipende dal suo M/S) e `bool parseValue(ParamID, const std::string&, const double* allNorms, double& norm)` (il valore digitato: si sceglie lo scatto più vicino nella scala in uso). Testi identici a `Controller::getParamStringByValue`: se il controller ha già formattatori puri, si spostano qui e il controller li chiama.
- Produces in `readouts.h`: `std::string lufsText(double lufs)` ("−14.2", "—" senza misura), `std::string toTargetText(double lufsShort, int targetIndex)` (scarto con segno e una cifra, "—" senza misura), `bool targetLedOn(double lufsShort, int targetIndex)` (verde entro ±0,5 LU), `std::string latencyText(int samples, double sampleRate)` se la preview la mostra, `std::string releaseText(int style, int releaseIndex)` (ms reali da `tuning.h`).

- [ ] **Step 1: Scrivere i test:** `test_value_text_matches_controller` (per ogni parametro e ogni scatto la stringa coincide con `getParamStringByValue`), `test_value_text_hp_side_scale` (HI PASS su SIDE: "200 Hz" all'ultimo scatto; digitando "60" su SIDE si ottiene lo scatto di 60 Hz, il caso del registro del Task 13), `test_value_text_parse_roundtrip` (`parse(format(v)) == v` per ogni scatto; testo non valido → `false`), `test_lufs_readouts` (−inf → "—"; −14.04 → "−14.0"; scarto +0,4 → LED acceso, +0,6 → spento), `test_release_text_follows_tuning` (il testo cambia se cambia `kLimitReleaseBaseMs` in una copia: funzione interna che prende le basi).
- [ ] **Step 2: Eseguire e vedere fallire.**
- [ ] **Step 3: Implementare.**
- [ ] **Step 4: Eseguire** `ctest -R "happy_ending_(ui|controller)"` → PASS.
- [ ] **Step 5: Commit:** `feat(happy-ending): value texts and LUFS readouts (pure)`.

---

### Task 6: Modello dello schermo (puro)

**Files:**
- Create: `src/happy-ending/src/ui/screen_model.h`, `src/happy-ending/src/ui/screen_model.cpp`
- Modify: `src/happy-ending/src/dsp/vibe_eq.{h,cpp}` (solo la funzione di risposta con un ritiro dato), `src/happy-ending/tests/test_ui.cpp`, `src/happy-ending/CMakeLists.txt` (il target dei test UI compila `fine_eq.cpp`, `vibe_eq.cpp` e ciò che serve)

**Interfaces:**
- Consumes: `mapParams` (`engine.h`), `FineEq::magnitudeDb`, `VibeEq`, le curve di `clip_softly.h`, `final_stage.h`, `shared/inflator`.
- Produces in `VibeEq`: `double magnitudeDb(double hz, float pullLow, float pullAir) const` (risposta della rete con il ritiro DYN dato, quello pubblicato da `kDispDynLow/Air`); la firma esistente resta.
- Produces in `screen_model.h`: `enum class ScreenView { Eq, Glue, Clip, Limit, Final };` `class EqCurves { void update(const double* norm); /* FineEq e VibeEq propri, preparati a 48 kHz, impostati da mapParams(norm, -1, false) */ bool msActive() const; double fineDb(int target, double hz) const; double vibeDb(double hz, float pullLow, float pullAir) const; double vibeStaticDb(double hz) const; };` `struct ScrollPoint { float levelDb, grDb, netGrDb, threshDb; };` `template<size_t N> class ScrollHistory` (anello, `push`, `size`, `at(0)` = il più vecchio, `clear`); `double clipTransfer(double x, const double* norm)` e `double finalTransfer(double x, const double* norm, double ceilingLin)` (le stesse funzioni del motore, con DRIVE ed EFFECT più la spinta della LOUDNESS di `kLoudCurve`); le scale degli assi come nella preview: EQ 20 Hz–20 kHz logaritmico e ±7,5 dB, GLUE livello −36…0 e GR 0…12, LIMIT livello −24…0 e GR 0…8, CLIP ingresso 0…1,05, FINAL 0…1,25 con la linea del ceiling; `std::vector<Point> curvePath(...)` per ogni vista in coordinate di `kScreenRect`.

- [ ] **Step 1: Scrivere i test:** `test_eq_curves_match_engine` (per tre impostazioni diverse le curve coincidono entro 0,01 dB con `FineEq::magnitudeDb` e `VibeEq::magnitudeDb` di un motore preparato a 48 kHz con le stesse impostazioni), `test_eq_curves_ms` (una banda su MID → `msActive()`, curve MID e SIDE diverse; tutte su ST → una curva), `test_vibe_pull_back` (ritiro 0 = statica; ritiro 1 = boost ridotto della quota di `tuning.h`), `test_listen_not_in_curves`, `test_transfer_functions_match_dsp` (`clipTransfer` e `finalTransfer` coincidono con i blocchi DSP su un campione statico, entro 1e-6, compresi CLIP/INFLATE e la spinta LOUDNESS), `test_scroll_history_ring`, `test_curve_paths_inside_screen` (ogni punto di ogni vista dentro `kScreenRect`, anche con valori estremi e NaN).
- [ ] **Step 2: Eseguire e vedere fallire.**
- [ ] **Step 3: Implementare.**
- [ ] **Step 4: Eseguire** `ctest -R "happy_ending_(ui|vibe_eq)"` → PASS.
- [ ] **Step 5: Commit:** `feat(happy-ending): screen model with EQ curves, transfer curves and scrolling history (pure)`.

---

### Task 7: Preferenze e decor visivo (puri)

**Files:**
- Create: `src/happy-ending/src/ui/{prefs.h,prefs.cpp,decor.h,decor.cpp,screw_angles.h,tapes.h}`
- Modify: `src/happy-ending/tests/test_ui.cpp`, `src/happy-ending/CMakeLists.txt`

**Interfaces:**
- Produces (modelli: file omonimi di `src/deep-throat/src/ui/`): `Prefs` (i campi di Deep Throat più `FairyTapes::Ui::EditorSize size = S100`, chiave `size`, con `sizeLabel`/`parseSizeLabel` del Task 1), `parsePrefs`, `serializePrefs`, `prefsPathFor`, `prefsPath`, `loadPrefs`, `savePrefs` (file `HappyEnding.prefs`); `WearLayout layoutFromSeed(std::uint32_t seed, WearLevel, const Rect& panel)` (nastri 0 / 1-2 / 2-3 per Off / Light / Heavy, solo negli slot di `kTapeSlots`, mai su controlli, schermo, meter, LED, LUFS); `screw_angles.h` e `tapes.h` come in Deep Throat.

- [ ] **Step 1: Scrivere i test** (modello Deep Throat, Task 6): `test_prefs_parse_defaults_and_bad_values`, `test_prefs_roundtrip` (anche `size`; `size=33` → 100), `test_prefs_file_missing_or_oversized_or_binary`, `test_decor_layout_deterministic`, `test_decor_tapes_never_cover_controls` (200 semi × tre livelli contro ogni `viewRect`, `kScreenRect`, righe dei meter, LED e schermo LUFS), `test_screw_look_matches_shared_generator`.
- [ ] **Step 2: Eseguire e vedere fallire.**
- [ ] **Step 3: Implementare** copiando e adattando i file di Deep Throat.
- [ ] **Step 4: Eseguire** `ctest -R happy_ending_ui_tests` → PASS.
- [ ] **Step 5: Commit:** `feat(happy-ending): preferences and seeded decor (pure)`.

---

### Task 8: Scheletro dell'editor, createView, render e banco di prova

**Files:**
- Create: `src/happy-ending/src/ui/{editor.h,editor.cpp,panel_view.h,panel_view.cpp}`, `src/happy-ending/tools/render_editor.cpp`, `src/happy-ending/tools/render_smoke.cmake`, `src/happy-ending/tests/test_editor.cpp`
- Modify: `src/happy-ending/src/{controller.h,controller.cpp}` (`createView`; commento d'intestazione aggiornato: l'editor esiste), `src/happy-ending/CMakeLists.txt` (sorgenti UI nel plugin, `fairy_ui fairy_ui_widgets`, grana, `happy_ending_render`, `happy_ending_editor_tests`, `happy_ending_render_smoke`), `src/happy-ending/tests/test_factory.cpp`

**Interfaces:**
- Consumes: `layout.h` (Task 4), `Decor` (Task 3), `prefs.h` (Task 7), `scaledSize` con `EditorSize` (Task 1).
- Produces: `class Editor : public Steinberg::Vst::VSTGUIEditor, public Steinberg::IPlugViewContentScaleSupport, public VSTGUI::IControlListener` come Deep Throat, con la finestra = 1280 × 890 × fattore dell'host × SIZE letto da `loadPrefs()` (file `HappyEnding.prefs`) all'apertura (`checkSizeConstraint` accetta solo quella dimensione); `PanelView` (sfondo e grana, bordi con il titolo in basso, lo schermo come riquadro senza titolo, `kPanelTexts`, sigle dei motori); `Controller::createView`; `struct EditorPreview`; banco `happy_ending_editor_tests` con `struct EditorTest` amica dell'`Editor` (viste senza finestra, tick del timer a comando, accesso al controllo di un `ParamID`, alla serigrafia e alle richieste di ridimensionamento a un `IPlugFrame` finto); il tool di render scrive `front.png` e le schede del retro in una cartella data, con valori di prova per i parametri di visualizzazione.

- [ ] **Step 1: Scrivere i test:** in `test_factory.cpp` `test_create_view_returns_editor` (per `kEditor` una vista 1280 × 890 a SIZE 100 e host 1; altro nome → `nullptr`), `test_editor_open_close_repeatedly` (50 volte, riferimenti a zero alla fine), `test_editor_size_from_prefs` (con `XDG_CONFIG_HOME` scratch e `size=75` in `HappyEnding.prefs`: 960 × 668); `happy_ending_render_smoke` (il tool scrive `front.png` 1280 × 890: intestazione PNG, larghezza e altezza).
- [ ] **Step 2: Eseguire e vedere fallire.**
- [ ] **Step 3: Implementare** banco di prova, editor (solo livello pannello), `createView`, tool di render; VSTGUI compila con `-j2`, un target alla volta.
- [ ] **Step 4: Eseguire** build di `happy_ending happy_ending_factory_test happy_ending_render happy_ending_editor_tests` e `ctest -R happy_ending` → PASS; aprire `front.png` e controllare a occhio titoli in basso, bordi, sigle e testi contro la preview (dichiararlo nel rapporto).
- [ ] **Step 5: Commit:** `feat(happy-ending): editor skeleton with the panel layer, createView and the offscreen render tool`.

---

### Task 9: Comandi legati ai parametri

**Files:**
- Modify: `src/happy-ending/src/ui/{editor.h,editor.cpp}`, `src/happy-ending/tests/test_editor.cpp`

**Interfaces:**
- Consumes: `kControls`, `kActions`, `uiStateFrom`, `controlDimmed/Locked/Visible`, `silkscreenItems` (Task 4), `formatValue`/`parseValue` (Task 5), `Controller::doMatch()`.
- Produces: una vista per ogni `ControlSpec` (`RackKnob`; `RackToggle` per IN, BYPASS, WARM, SET, LISTEN; `RackLitChoice` per gli stili del LIMIT e INFLATE/CLIP; `RackCycle` per M/S, RATIO, SC HPF, CEIL, TARGET; DYN come pulsantino d'intestazione a faccia accesa); i tre pulsanti LISTEN scrivono `kListenBand` (premere quello acceso lo spegne); MATCH chiama `Controller::doMatch()`; i pulsanti delle viste cambiano `ScreenView` (stato dell'editor, ricordato dal controller finché vive l'istanza, mai nel progetto; default EQ); `valueChanged`/`controlBeginEdit`/`controlEndEdit` → `beginEdit`/`performEdit`/`endEdit`; il timer applica i cambi dell'host (parametri dipendenti: `update` segna solo un flag) e ricalcola attenuazione, blocco, visibilità e scala del HI PASS; campo di testo sotto le manopole (invio conferma, Esc annulla, passa per `parseValue`); doppio clic = default; **alla chiusura** (`close()`), se LISTEN o SET sono accesi, l'editor li spegne con una modifica unica ciascuno.

- [ ] **Step 1: Scrivere i test:** `test_editor_controls_follow_host` (per ogni parametro: `setParamNormalized` → il controllo cambia entro un tick, senza `performEdit`), `test_user_edit_reaches_host_once`, `test_double_click_resets_default`, `test_listen_exclusive_and_toggle` (clic su MID poi su HI MID: un solo LISTEN; secondo clic su HI MID: spento), `test_close_turns_listen_and_set_off` (LISTEN su MID e SET accesi, `close()` → entrambi a 0 tramite `performEdit`; chiusi già spenti: nessuna modifica), `test_set_locks_limit_except_release`, `test_final_mode_shows_its_controls`, `test_hp_scale_follows_ms` (HI PASS su SIDE da host: valore e scala stampati cambiano), `test_match_calls_controller` (con una lettura short-term finta: LOUDNESS cambia con una modifica unica), `test_view_buttons_exclusive`.
- [ ] **Step 2: Eseguire e vedere fallire.**
- [ ] **Step 3: Implementare** (modello: `src/deep-throat/src/ui/editor.cpp`); mutazione dimostrata: se `close()` non spegne SET, `test_close_turns_listen_and_set_off` fallisce.
- [ ] **Step 4: Eseguire** build e `ctest -R happy_ending` → PASS; render e controllo a occhio di `front.png` (comandi al loro posto, valori leggibili).
- [ ] **Step 5: Commit:** `feat(happy-ending): editor controls bound to the parameters, LISTEN and SET off on close`.

---

### Task 10: LEVEL, LUFS e LED

**Files:**
- Modify: `src/happy-ending/src/ui/{editor.h,editor.cpp}`, `src/happy-ending/tests/test_editor.cpp`
- Create (se serve una vista propria): `src/happy-ending/src/ui/lufs_view.{h,cpp}`

**Interfaces:**
- Consumes: `kMeterRows`, `kGrRows`, `kClipLeds`, `kLufsScreenRect`, `kActLeds`, `kTargetLed` (Task 4), `display.h` (normalizzazioni), `readouts.h` (Task 5).
- Produces: righe `MeterBar` IN e OUT (scala e colori di The Stripper), cinque righe GR ambra 0…12 dB rosse in cima, ognuna attenuata con il suo stadio e con BYPASS; nove `ClipLed` (un clic su uno scrive `kClearClips`; quelli degli stadi attenuati con BYPASS e con il loro IN); schermo LUFS con short-term, integrata (clic → `kResetIntegrated`) e TO TARGET con il LED verde; LED ACT del DYN (LOW e AIR) accesi quanto `kDispDynLow/Air`.

- [ ] **Step 1: Scrivere i test:** `test_meters_follow_display_params` (`kDispInL` a metà → circa metà dei LED; `kDispGr2` a 0,5 → metà riga L), `test_clip_click_clears_via_trigger`, `test_lufs_click_resets_integrated`, `test_lufs_texts_and_target_led` (valori finti: testi e LED come `readouts.h`), `test_gr_rows_dim_with_stage_and_bypass`, `test_act_leds_follow_dyn`, `test_editor_timer_stops_on_close` (50 aperture e chiusure con tick in mezzo).
- [ ] **Step 2: Eseguire e vedere fallire.**
- [ ] **Step 3: Implementare.**
- [ ] **Step 4: Eseguire** build e test → PASS; render con valori di prova e controllo a occhio della riga di servizio contro la preview.
- [ ] **Step 5: Commit:** `feat(happy-ending): LEVEL meters, GR rows, clip LEDs and the LUFS readout`.

---

### Task 11: Schermo condiviso

**Files:**
- Create: `src/happy-ending/src/ui/{screen_view.h,screen_view.cpp}`
- Modify: `src/happy-ending/src/ui/{editor.h,editor.cpp}`, `src/happy-ending/tests/test_editor.cpp`

**Interfaces:**
- Consumes: `EqCurves`, `ScrollHistory`, `clipTransfer`, `finalTransfer`, `curvePath` (Task 6), i parametri di visualizzazione del Task 2.
- Produces: `class ScreenView : public VSTGUI::CView` con le cinque viste della spec §4 e della preview (`viewTick`): EQ (griglia, curva STEREO oppure MID e SIDE, curva del VIBE in ambra, tratteggiata la statica quando il DYN ritira, la zona ascoltata durante LISTEN), GLUE e LIMIT (storia che scorre di livello, GR e soglia; GLUE anche la riduzione netta; LIMIT la linea del ceiling), CLIP e FINAL (curva di trasferimento con il punto che segue `kDispClipIn` / `kDispFinalIn`; FINAL con la linea del ceiling); si attenua solo con BYPASS. Le curve EQ si ricalcolano solo quando un parametro cambia, non a ogni tick.

- [ ] **Step 1: Scrivere i test:** `test_screen_view_switch` (i pulsanti delle viste cambiano ciò che la vista disegna: contatore di disegno per vista nel banco), `test_eq_curves_recomputed_only_on_change` (100 tick senza cambi → 0 ricalcoli; un cambio di GAIN → 1), `test_scroll_history_grows` (dopo N+50 tick la storia ha N punti), `test_screen_dims_only_on_bypass`, `test_listen_zone_drawn` (con LISTEN su MID la zona è presente, senza no).
- [ ] **Step 2: Eseguire e vedere fallire.**
- [ ] **Step 3: Implementare.**
- [ ] **Step 4: Eseguire** build e test → PASS; render delle cinque viste con dati di prova e controllo a occhio contro la preview.
- [ ] **Step 5: Commit:** `feat(happy-ending): the shared screen with the EQ, GLUE, CLIP, LIMIT and FINAL views`.

---

### Task 12: Usura, nastri, viti e preferenze nell'editor

**Files:**
- Create: `src/happy-ending/src/ui/{tape_view.h,tape_view.cpp}`
- Modify: `src/happy-ending/src/ui/{editor.h,editor.cpp}`, `src/happy-ending/tests/test_editor.cpp`

**Interfaces:**
- Consumes: `Decor`, `Controller::decor/editDecor/decorRevision` (Task 3), `layoutFromSeed`, `Prefs` (Task 7), `WearView`, `ScrewView`, `TapeView` (modello Deep Throat).
- Produces (come Deep Throat, Task 10): usura tra serigrafia e viti, nastri scrivibili (14 caratteri, invio o uscita confermano, Esc annulla, il testo va a `editDecor`), viti girevoli (una `editDecor` a fine trascinamento), `setWearLevel`, `setTapeFont`, `setValuesFont`, `reroll()`; il timer ridisegna il decor solo se `decorRevision()` cambia.

- [ ] **Step 1: Scrivere i test:** `test_wear_off_has_no_marks_and_no_tapes`, `test_wear_level_change_redraws_without_rebuild`, `test_tape_text_edit_goes_to_decor`, `test_screw_drag_stores_angle_once`, `test_reroll_changes_seed_and_clears_decor`, `test_prefs_missing_file_opens_with_defaults`, `test_missing_grain_asset_draws_plain_panel`.
- [ ] **Step 2: Eseguire e vedere fallire.**
- [ ] **Step 3: Implementare** sul modello di Deep Throat.
- [ ] **Step 4: Eseguire** build e test → PASS; render con Wear Off, Light e Heavy, controllo a occhio che nastri e viti non coprano comandi, schermo e LED.
- [ ] **Step 5: Commit:** `feat(happy-ending): wear, paper tapes and screws with user preferences`.

---

### Task 13: Retro: flip, SETTINGS con SIZE e OVERSAMPLING

**Files:**
- Create: `src/happy-ending/src/ui/back_settings.h`, `src/happy-ending/src/ui/back_layout.h`
- Modify: `src/happy-ending/src/ui/{editor.h,editor.cpp}`, `src/happy-ending/tests/test_ui.cpp`, `src/happy-ending/tests/test_editor.cpp`

**Interfaces:**
- Consumes: `BackView`, `PanelFlip`, `BackLayout` con la larghezza, `tabLinkBox`/`backLinkBox` col layout (Task 1), `Prefs` con `size` (Task 7).
- Produces: `back_layout.h`: `constexpr FairyTapes::Ui::BackLayout kBackLayout` = il retro di Happy Ending, 1280 × 890, dalla preview (APPEARANCE `{24,102,602,412}`, BEHAVIOUR `{654,102,602,412}`, NOTES `{24,532,1232,120}`, titolo CREDITS `{24,102,1232,100}`, COMPONENTS `{24,220,1232,292}`, NOTICES `{24,530,1232,80}`, area della scheda propria `{24,102,1232,700}`, link SETTINGS/BUTTONS/ROUTING/CREDITS allineati a destra a x 880 / 970 / 1060 / 1150, "← BACK" e "BACK →" allineati a destra a x 1246, link a y 66-84; SIGNAL FLOW e LEGEND dalla scheda ROUTING della preview). `back_settings.h` (modello Deep Throat): APPEARANCE (Wear, Tape font, Values font, RE-ROLL, **SIZE** 75 % / 90 % / 100 % / 125 %, testo d'aiuto "editor size of this plugin" come la preview), BEHAVIOUR (**OVERSAMPLING** OFF / 2× / 4× con l'aiuto "CLIP, TUBE, FINAL, METALHEAD" e la riga sulla latenza come la preview; scrive `kOversampling` con una modifica unica, il controller fa già il resto), NOTES (le quattro frasi della preview); `settingsItems(const Prefs&, int oversamplingIndex)` e la corrispondenza voce → modifica; nell'editor: "BACK →", flip di circa 0,8 s (un campo di testo aperto si conferma prima), "← BACK", schede SETTINGS, BUTTONS, ROUTING, CREDITS (le ultime tre vuote fino al Task 14); **SIZE applicato subito:** l'editor chiede all'host `IPlugFrame::resizeView` con la nuova dimensione e aggiorna lo zoom del frame; se l'host rifiuta, la dimensione resta quella di prima e la preferenza è salvata comunque (vale alla prossima apertura).

- [ ] **Step 1: Scrivere i test:** in `test_ui.cpp` `test_back_layout_values` (i numeri di `kBackLayout` sopra; tutti i riquadri dentro 1280 × 890), `test_settings_items_reflect_prefs` (comprese SIZE e OVERSAMPLING), `test_settings_item_to_prefs`. In `test_editor.cpp`: `test_flip_roundtrip_and_text_edit_confirmed_first`, `test_settings_apply_wear_at_once`, `test_settings_failed_write_is_ignored`, `test_tab_links_order` (SETTINGS, BUTTONS, ROUTING, CREDITS), `test_size_change_resizes_window` (frame finto che accetta: `resizeView` con 1152 × 801 per 90 % a host 1; `size=90` scritto in `HappyEnding.prefs`), `test_size_change_refused_by_host` (frame finto che rifiuta: finestra invariata, nessun crash, preferenza scritta), `test_oversampling_from_back_is_one_edit`.
- [ ] **Step 2: Eseguire e vedere fallire.**
- [ ] **Step 3: Implementare.**
- [ ] **Step 4: Eseguire** build e test → PASS; render della scheda SETTINGS e controllo a occhio contro la preview.
- [ ] **Step 5: Commit:** `feat(happy-ending): back of the panel with the flip, SETTINGS, SIZE and OVERSAMPLING`.

---

### Task 14: Retro: BUTTONS, ROUTING e CREDITS

**Files:**
- Create: `src/happy-ending/src/ui/{buttons_text.h,routing_diagram.h,credits.h}`
- Modify: `src/happy-ending/src/ui/editor.cpp`, `src/happy-ending/tests/test_ui.cpp`

**Interfaces:**
- Produces: `buttons_text.h`: `struct ButtonsEntry { const char* name; std::vector<std::string> what, details; };` `const std::vector<ButtonsEntry>& buttonsEntries()` (le quattordici voci della preview, nell'ordine della preview, con i valori presi da `params.h` e `tuning.h` dove la frase li cita: rapporti, frequenze di SC HPF, target, ceiling, DRIVE massimo); `routing_diagram.h`: `FairyTapes::Ui::RoutingDiagram happyEndingRouting()` (schema fisso della spec §7 con `RoutingBuilder`, due righe, linee di controllo tratteggiate CEILING, LOUDNESS e MATCH, punti di misura, METALHEAD IN disegnato spento); `credits.h`: `std::vector<CreditEntry> credits()` (le righe della tabella di §8: Multi-Q e Multi-Comp di Dusk Audio GPL-3.0, Airwindows MIT, ZLInflator GPL-3.0, libebur128 MIT, Vicanek come metodo pubblicato, VST 3 SDK MIT, VSTGUI BSD-3-Clause, Zeyada OFL-1.1, Rock Salt e Homemade Apple Apache-2.0) e le NOTICES (marchio VST; licenza del plugin GPL-3.0); la scheda BUTTONS si disegna nell'area propria del layout largo.

- [ ] **Step 1: Scrivere i test:** `test_buttons_entries` (quattordici voci, nomi nell'ordine della preview, i numeri citati uguali a quelli di `params.h`), `test_buttons_fit_the_box` (con le misure del font del retro ogni riga sta nell'area propria e l'ultima riga sopra il bordo basso), `test_routing_blocks_in_engine_order` (INPUT, FINE EQ, VIBE EQ, GLUE sulla prima riga; CLIP, LIMIT, FINAL, METALHEAD, OUTPUT sulla seconda; un Bypass; almeno sei linee tratteggiate), `test_routing_inside_flow_box`, `test_credits_entries` (le voci sopra con autore e licenza), `test_no_brand_in_user_facing_strings` (fuori dai CREDITS e dalle sigle nessuna stringa contiene i nomi delle fonti).
- [ ] **Step 2: Eseguire e vedere fallire.**
- [ ] **Step 3: Implementare.**
- [ ] **Step 4: Eseguire** build e test → PASS; render delle tre schede e controllo a occhio contro la preview.
- [ ] **Step 5: Commit:** `feat(happy-ending): BUTTONS, ROUTING and CREDITS tabs`.

---

### Task 15: Pulizie del motore rimandate

**Files:**
- Modify: `src/happy-ending/src/dsp/fine_eq.cpp`, `src/happy-ending/src/dsp/clip_softly.h` (o dove sta `setDriveDb`), `src/happy-ending/src/engine.cpp`, `src/happy-ending/tests/{test_fine_eq.cpp,test_clip.cpp,test_engine.cpp}`

**Interfaces:**
- Produces: LISTEN che si accende e si spegne con una dissolvenza (stessa durata delle altre, 64 campioni) invece di azzerare di colpo l'altra componente M/S; cambio di via (ST/MID/SIDE) di una banda dissolto; `setBand`, `setStageOn` e `setDriveDb` che non ripartono se il valore non cambia.

- [ ] **Step 1: Scrivere i test:** `test_listen_engage_is_click_free` (seno stazionario, LISTEN acceso e spento: nessun salto tra campioni consecutivi oltre la pendenza del seno più un margine), `test_route_change_is_click_free`, `test_unchanged_settings_do_not_restart_ramps` (impostazioni identiche a ogni blocco: l'uscita è uguale a quella con una sola chiamata, bit per bit).
- [ ] **Step 2: Eseguire e vedere fallire.**
- [ ] **Step 3: Implementare.**
- [ ] **Step 4: Eseguire** `ctest -R happy_ending` → tutto verde (BYPASS esatto e latenza invariati).
- [ ] **Step 5: Commit:** `fix(happy-ending): click-free LISTEN and route changes, setters only on change`.

---

### Task 16: Documentazione e chiusura

**Files:**
- Modify: `src/happy-ending/README.md`, `docs/superpowers/specs/2026-10-21-happy-ending-design.md`, questo piano, `CLAUDE.md` (§8, riga di Happy Ending), `docs/DESIGN_SYSTEM.md` se il Task 1 o l'esecuzione hanno cambiato regole comuni

- [ ] **Step 1: README** (inglese, come l'attuale): editor, schede del retro, preferenze e dove sono salvate (SIZE compresa), tool di render, limiti noti.
- [ ] **Step 2: Allineare spec e piano:** decisioni prese durante i task (registro dell'esecuzione), spunte, §11 (editor fatto), riga "Avanzamento".
- [ ] **Step 3: Verifica finale:** build di `happy_ending happy_ending_factory_test happy_ending_ui_tests happy_ending_editor_tests happy_ending_state_module_test happy_ending_render` e dei test di `shared/ui`, `ctest -R "happy_ending|fairy_ui" --output-on-failure` (riportare l'output); render di fronte, cinque viste dello schermo e quattro schede del retro, controllo a occhio (dichiararlo come manuale); `HappyEnding.vst3` costruito e caricato da `happy_ending_factory_test`.
- [ ] **Step 4: Commit:** `docs(happy-ending): editor readme and aligned spec and plan`.

---

## Self-review

- **Copertura della spec:** §3 riga di servizio → Task 4, 9, 10; §4 schermo → Task 2, 6, 11; §5 stadi e comandi → Task 4, 9; §5.1 LISTEN e §5.5 SET (non salvati, spenti alla chiusura) → Task 9; §6 MATCH → Task 9; §7 grafica (sigle, layout, usura, viti, retro con SIZE e OVERSAMPLING, BUTTONS, ROUTING) → Task 1, 4, 8, 12-14; §8 licenze nei CREDITS → Task 14; §9 stato (LISTEN/SET fuori dallo stato, display) → Task 2, 3; §11 editor → Task 16; §13 scelte tecniche dell'editor → Task 1-3, 6, 9, 13; minori rimandati del registro (LISTEN e via dissolti, setter solo al cambio, `add_dependencies`, numero digitato su HI PASS SIDE) → Task 15, vincoli globali, Task 5.
- **Coerenza dei tipi:** `EditorSize`, `BackLayout` con la larghezza (Task 1) usati nei Task 7, 8 e 13; `Decor` (Task 3) nei Task 7 e 12; i nuovi `DisplayID` (Task 2) nei Task 6, 10, 11; `ControlSpec`, `UiState` (Task 4) nei Task 5, 9-11; `ScreenView`, `EqCurves`, `ScrollHistory` (Task 6) nei Task 9 e 11; `Prefs` (Task 7) nei Task 12 e 13.
- **Coordinate:** vengono dalla preview con la regola dei gruppi; i test del Task 4 fissano invarianti (parametri legati una volta, rettangoli dentro i riquadri, IN in alto a destra), non i pixel; il controllo dei pixel è a occhio sui render.
