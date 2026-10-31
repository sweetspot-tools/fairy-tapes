# Deep Throat Editor (GUI) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Dare a Deep Throat un editor VSTGUI uguale alla preview HTML (pannello 1000 × 614): fronte con 25 controlli, meter e scope animati dal motore, retro con SETTINGS, MODES, ROUTING e CREDITS.

**Architecture:** Struttura copiata da Pillow Talk dentro `src/deep-throat/` (layout puro in `ui/layout.h`, `editor`, viste per livelli, decor e preferenze), con i widget di `shared/ui` (`RackKnob`, `RackToggle`, `RackCycle`, `RackLitChoice`, `MeterBar`, `ClipLed`, `SilkscreenView`, `WearView`, `ScrewView`, `BackView`, `PanelFlip`, `RoutingBuilder`). Il motore espone valori di visualizzazione tramite parametri nascosti; una tabella unica per modalità (`mode_table.h`) alimenta blocchi DSP, scritte dinamiche e scheda MODES; lo stato passa alla v2 con il decor.

**Tech Stack:** C++20, VST3 SDK, VSTGUI4, `shared/ui` (`fairy_ui`, `fairy_ui_widgets`), CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-10-24-deep-throat-gui-design.md` (spec del plugin: `docs/superpowers/specs/2026-10-10-deep-throat-design.md`). Riferimento visivo e fonte delle coordinate: `src/deep-throat/docs/mockups/deep_throat_rack.html`. Modelli di codice: `src/pillow-talk/src/ui/*`, `src/pillow-talk/src/{state.h,messages.h,controller.*,processor.cpp}`, `src/pillow-talk/tools/render_editor.cpp`.

## Global Constraints

- Namespace `FairyTapes::DeepThroat` (UI in `...::Ui`). Tutto in `src/deep-throat/` salvo `CMakeLists.txt` del plugin e, se serve, righe comuni di CMake: **non si toccano gli altri plugin né `shared/`** (regola della chat); se manca un widget in `shared/ui`, si segnala invece di modificarlo.
- Pannello **1000 × 614**; titoli dei riquadri sul bordo **basso**; IN come nella preview; regole di `docs/DESIGN_SYSTEM.md` (livelli dal fondo: pannello, bordi e titoli, serigrafia, usura, nastri, viti, controlli; attenuazione 35%).
- 25 parametri, ciascuno legato a **un solo** controllo; il Bypass sta nel riquadro OUTPUT; i parametri di visualizzazione sono `kIsReadOnly | kIsHidden`, ID da 1000, fuori da stato e automazione.
- Testo visibile **in inglese**; identificatori e commenti in inglese; documentazione in italiano. Nessun nome o marchio delle fonti d'ispirazione nella UI (il nome interno `DistressorComp` resta nel codice).
- Nessuna allocazione nel thread audio; il processor non dipende dalle viste; la UI non include nulla del processor.
- `kStateVersion = 2` (v1: int32 versione + 25 `double`; v2: v1 + `uint32` seme + 4 × `float32` angoli viti + `uint32` numero testi + per testo `uint32` lunghezza e byte); la v1 si carica con il decor di default; stato corrotto = rifiutato intero.
- Preferenze: `<cartella preferenze>/FairyTapes/DeepThroat.prefs`, chiavi `wear=Off|Light|Heavy`, `notes=Show|Hide`, `tapeFont=Zeyada|RockSalt|HomemadeApple`, `valuesFont=Tape|Standard`.
- Test: harness `RUN_TEST`/`ASSERT_TRUE` del plugin; ogni comportamento nasce con un test che prima fallisce; ogni test deve poter fallire per un'implementazione sbagliata (nel rapporto: come; mutazione dimostrata per i 2-3 più importanti).
- Build fuori dal repo, `-j2`, **solo** i target del plugin: `cmake -S . -B /tmp/dt-build -DCMAKE_BUILD_TYPE=Release -DGPU_NAM_DISABLE_CUDA=ON`, poi `cmake --build /tmp/dt-build --target deep_throat deep_throat_tests deep_throat_factory_test deep_throat_ui_tests deep_throat_state_module_test deep_throat_editor_tests deep_throat_render -j2` e `ctest --test-dir /tmp/dt-build -R deep_throat --output-on-failure`. I nuovi test puri dell'interfaccia stanno in `tests/test_ui.cpp` (eseguibile `deep_throat_ui_tests`, collegato a `fairy_ui`); i test di motore, tabella e display restano in `tests/test_deep_throat.cpp`; stato, decor e messaggi si provano tramite le interfacce VST3 in `tests/test_state_module.cpp` (`deep_throat_state_module_test`, Task 3); il comportamento dell'editor (controlli, timer, meter, nastri, viti, flip) in `tests/test_editor.cpp` (`deep_throat_editor_tests`, banco di prova creato nel Task 7); `tests/test_factory.cpp` resta per il ciclo di vita del modulo, i flag dei parametri e `createView`.
- Commit per nome (mai `git add -A`), messaggi in inglese, **senza** riga `Co-Authored-By` (regola dell'utente); niente push.

## Review Focus

- Aprire e chiudere l'editor più volte (e con il timer attivo) senza crash né fughe di riferimenti (Task 7, 9).
- Automazione dell'host su tutti e 25 i parametri con l'editor aperto: la UI segue, e una modifica dell'utente non genera ritorni all'host (Task 8).
- Stato v1 valido, stato v2 con testi e angoli corrotti, blob troncato: nessun crash, decor di default o rifiuto (Task 3).
- Cambio di modalità dalla UI e dall'automazione: scritte dinamiche, sigle e scheda MODES coerenti con la tabella (Task 5, 8, 12).
- File di preferenze o asset della grana mancanti o corrotti, Wear Off e Heavy: l'editor si apre con i default (Task 6, 7, 10).

---

### Task 1: Tabella unica per modalità

**Files:**
- Create: `src/deep-throat/src/mode_table.h`
- Modify: `src/deep-throat/src/mode_presets.h` (rimosso), `src/deep-throat/src/engine.cpp`, `src/deep-throat/src/dsp/{corrective_eq,deesser,distressor_comp,tone_eq}.{h,cpp}` (leggono dalla tabella), `src/deep-throat/tests/test_deep_throat.cpp`

**Interfaces:**
- Produces: `struct ModeRow { const char* name; const char* description; bool levelDetector; float riderMaxRangeDb, riderSlewDbPerSec; double hpfHz, mudCutDb, deEssScale, compRatio; int compDist; /* 1 OFF, 2 Dist 2, 3 Dist 3 */ float toneMudDb, tonePresenceDb, toneAirDb; };` `inline constexpr ModeRow kModeTable[3];` `constexpr const ModeRow& modeRow(Mode m)` (indice limitato a 0..2).
- Valori (spec del plugin §3-5): Speak = {"SPEAK", "Spoken word: podcast, voice-over", spectral, 6, 8, 80, -2, 1, 2, OFF, {-2, +3, +2}}; Sing = {"SING", "Sung voice", spectral, 4, 4, 70, -3, 1, 3, Dist 2, {-1.5, +2, +4}}; Puke = {"PUKE", "Extreme voices: scream, growl", level, 3, 2, 60, -4, 0.15, 8, Dist 3, {-3, +1, 0}}.

- [ ] **Step 1: Scrivere i test** in `test_deep_throat.cpp`: `test_mode_table_values` (ogni campo delle tre righe uguale ai numeri sopra, con `modeRow(Mode(7))` limitato a Puke e `modeRow(Mode(-1))` a Speak), `test_mode_table_drives_engine` (con Rider Range = 1: `mapParams` dà `riderRangeDb` 6, 4, 3 nelle tre modalità; `Engine::usingLevelDetector()` è vero solo in Puke), e la sostituzione di `test_presets_values` con un test sui valori di `modeRow` (l'asserzione di identità col vecchio `kModePresets` sparisce con il file).
- [ ] **Step 2: Eseguire e vedere fallire** (il header non esiste): `cmake --build /tmp/dt-build --target deep_throat_tests -j2` → errore di include.
- [ ] **Step 3: Implementare** `mode_table.h` e farlo leggere a engine (`riderRangeDb`, slew, detector), `CorrectiveEq` (HPF, fango), `DeEsser` (scala Puke, oggi `kPukeAmountScale`), `DistressorComp` (`modeRatio`, `modeDist`), `ToneEq` (guadagni a +100%). I numeri non cambiano.
- [ ] **Step 4: Eseguire tutti i test del plugin** → PASS identico alla baseline (118+ test, factory test) più i due nuovi; nessun test dei blocchi modificato.
- [ ] **Step 5: Commit:** `refactor(deep-throat): one mode table read by the DSP blocks and the engine`.

---

### Task 2: Valori di visualizzazione (motore, processor, controller)

**Files:**
- Create: `src/deep-throat/src/display.h`
- Modify: `src/deep-throat/src/{params.h,engine.h,engine.cpp,processor.h,processor.cpp,controller.cpp}`, `src/deep-throat/tests/test_deep_throat.cpp`, `src/deep-throat/tests/test_factory.cpp`

**Interfaces:**
- Produces in `params.h`: `constexpr ParamID kMeterBase = 1000; enum DisplayID : ParamID { kDispInL = kMeterBase, kDispInR, kDispOutL, kDispOutR, kDispGr, kDispVoice, kDispRider, kDispBreath, kDispClipIn, kDispClipOut, kNumDisplayIDs }; constexpr ParamID kClearClips = kMeterBase + 100;`
- Produces in `display.h` (modello: `Display*`/`Level*` di `src/pillow-talk/src/engine.h`): `struct LevelValues { float inL, inR, outL, outR /*peak dB, floor -120*/; bool clipIn, clipOut; }`, `struct DisplayValues { float voiceDb /*max, Rider meter level*/, riderGainDb /*min*/, breathProb /*max, raw detector output before the look-ahead max*/, grDb /*max*/; }`, `LevelAccumulator`, `DisplayAccumulator`, `DisplayRateLimiter` (16 ms), `constexpr double kClipHoldSeconds = 2.0`, e `std::array<double, 10> displayNorms(const DisplayValues&, const LevelValues&)` nell'ordine dei `DisplayID` (picchi e voce: `Ui::meterNormFromDb(db, -60, 6)`, GR: `grDb / 21` limitato a 0..1, Rider: `0.5 + riderGainDb / 12` limitato a 0..1, respiro: probabilità, clip: 0/1).
- Produces in `Engine`: `LevelValues takeLevels(); DisplayValues takeDisplay(); void clearClips();` (accumulano dentro `process`, senza allocare; i picchi in ingresso sono presi prima del guadagno d'ingresso, quelli in uscita dopo l'Output e il Bypass; clip = qualsiasi `|x| >= 1.0`, tenuta 2 s o fino a `clearClips()`).

- [ ] **Step 1: Scrivere i test** in `test_deep_throat.cpp`: `test_display_peaks_and_clip` (seno a -6 dBFS in ingresso: `inL`/`inR` ≈ -6 dB ±0.5 e `outL` coerente; un campione 1.0 accende `clipIn` e lo tiene 2 s di audio, `clearClips()` lo spegne), `test_display_gr_and_rider` (segnale forte con Comp Amount 1: `grDb > 3`; Rider con livello basso: `riderGainDb > 0`), `test_display_breath_prob_raw` (frase, respiro, frase in Speak: `breathProb` massimo del periodo > 0.6 durante il respiro), `test_display_norms_mapping` (`displayNorms` con picco 0 dB → 60/66 ±1e-6, -60 dB → 0, GR 21 dB → 1, Rider ±6 dB → 1 e 0, valori non finiti → 0), `test_display_rate_limiter_16ms` (prima chiamata scrive, poi una ogni 16 ms di audio a 44.1/48/96 kHz), `test_display_no_alloc` (contatore di `operator new`: 0 allocazioni in `process`, `takeLevels`, `takeDisplay`, `displayNorms`). In `test_factory.cpp`: i parametri nascosti esistono con ID `kDisp*` e `kClearClips` e hanno i flag `kIsReadOnly | kIsHidden`; il conteggio dei parametri automatizzabili resta 25.
- [ ] **Step 2: Eseguire e vedere fallire** (simboli non definiti).
- [ ] **Step 3: Implementare** `display.h`, gli accumulatori nell'`Engine`, la scrittura in `Processor::process` (un punto di parametro in uscita per ID a ogni scadenza del limitatore, `kClearClips` riconosciuto e azzerato come in Pillow Talk), la registrazione dei parametri nel `Controller::initialize`.
- [ ] **Step 4: Eseguire i test** → PASS (anche `deep_throat_factory_test`).
- [ ] **Step 5: Commit:** `feat(deep-throat): display values from the engine through hidden parameters`.

---

### Task 3: Stato v2, decor e messaggi controller/processor

**Files:**
- Create: `src/deep-throat/src/messages.h`
- Modify: `src/deep-throat/src/{state.h,controller.h,controller.cpp,processor.h,processor.cpp}`, `src/deep-throat/tests/test_deep_throat.cpp`, `src/deep-throat/tests/test_state_module.cpp` (nuovo: `deep_throat_state_module_test`, modello `src/pillow-talk/tests/test_state_module.cpp` e il suo blocco in `src/pillow-talk/CMakeLists.txt`, con le classi host dell'SDK; si salta con exit 77 se il bundle manca)

**Interfaces:**
- Produces in `state.h` (modello: `src/pillow-talk/src/state.h`, senza le impostazioni di comportamento): `constexpr int32_t kStateVersion = 2; constexpr int kNumScrews = 4, kMaxTapeTexts = 6, kMaxTapeChars = 14; struct Decor { uint32_t seed = 0; float screwAngles[4] = {}; std::vector<std::string> tapeTexts; };` `void serializeDecor(const Decor&, std::vector<uint8_t>&)`, `bool deserializeDecor(const uint8_t*, size_t, Decor&)`, `void serializeState(const double* values, const Decor&, std::vector<uint8_t>&)`, `bool deserializeState(const uint8_t*, size_t, double* values, Decor&)` (v1 e v2), `void ensureSeed(Decor&)` (seme non nullo, mai nel thread audio), `constexpr size_t kMaxStateBytes`. Sanificazione come Pillow Talk: parametri clampati a [0,1] (un valore non finito rifiuta tutto), angoli non finiti → 0 e riportati a [0,360), testi ASCII stampabili, tagliati, al massimo 14 caratteri e 6 testi.
- Produces in `messages.h` (modello `src/pillow-talk/src/messages.h`): `constexpr const char* kMsgDecor = "Decor"; template <class Msg> void encodeDecorMessage(Msg*, const Decor&); template <class Msg> bool decodeDecorMessage(Msg*, Decor&);`
- Produces in `Controller`: `Decor decor(); uint32_t decorRevision() const; void editDecor(Decor d); tresult notify(IMessage*) override;` (copia per l'editor, scambio con il processor tramite messaggi `Decor`, mai nel thread audio). `Processor::getState/setState` salvano e caricano il decor e lo inviano al controller dopo `setState` e alla connessione.

- [ ] **Step 1: Scrivere i test:** in `test_deep_throat.cpp`: `test_state_v2_roundtrip` (valori e decor con seme, 4 angoli e due testi tornano identici), `test_state_v1_loads_with_default_decor` (un blob v1 costruito a mano carica i 25 valori e dà `seed == 0`; `ensureSeed` poi dà un seme ≠ 0 e un secondo `ensureSeed` non lo cambia), `test_state_rejects_corrupt` (blob troncato a ogni lunghezza da 0 a `size-1`, versione 3, valore non finito, numero di testi enorme, lunghezza di testo oltre il blob → `false`, `values` intatti), `test_decor_sanitised` (angoli NaN/inf/±1000 → [0,360); testo con caratteri non stampabili e 40 caratteri → ASCII stampabile e 14 caratteri; più di 6 testi → 6), `test_decor_message_roundtrip` (con un messaggio finto: encode/decode identici, messaggio di altro tipo o blob corrotto → `false`). In `test_state_module.cpp` (tramite le interfacce VST3: `IComponent::setState/getState`, `IConnectionPoint`, `IMessage`): `setState` con un blob v2 seguito da `getState` restituisce lo stesso blob; il controller riceve il decor tramite `notify`.
- [ ] **Step 2: Eseguire e vedere fallire.**
- [ ] **Step 3: Implementare** stato, messaggi e il passaggio nel controller e nel processor sul modello di Pillow Talk.
- [ ] **Step 4: Eseguire i test del plugin** → PASS; i test di stato v1 esistenti restano verdi (la v1 si carica ancora).
- [ ] **Step 5: Commit:** `feat(deep-throat): state v2 with the instance decor and controller/processor messages`.

---

### Task 4: Layout puro dell'editor

**Files:**
- Create: `src/deep-throat/src/ui/editor_size.h`, `src/deep-throat/src/ui/layout.h`, `src/deep-throat/tests/test_ui.cpp`
- Modify: `src/deep-throat/CMakeLists.txt` (eseguibile `deep_throat_ui_tests`, `fairy_ui`, `add_test`)

**Interfaces:**
- Produces: in `editor_size.h`: `constexpr int kEditorWidth = 1000, kEditorHeight = 614;`. In `layout.h` (modello `src/pillow-talk/src/ui/layout.h`): `struct Rect {float x,y,w,h;}`, `enum class Box { Input, Scope, Level, Output, Mode, Rider, Breath, Eq, DeEss, Comp, Tone }`, `struct BoxSpec { Box box; Rect rect; const char* title; }`, `constexpr BoxSpec kBoxes[11]` con i rettangoli della spec del GUI §4; `enum class Kind { Knob, Toggle, Choice, Cycle }`, `enum class Led { None, Red, Yellow }`, `struct ControlSpec { ParamID id; Kind kind; Box box; Rect rect; const char* name, *lo, *hi; float capRadius; Led led; int choiceCount; const char* choices[8]; }`, `constexpr ControlSpec kControls[25]`, `const ControlSpec* controlOf(ParamID)`, `Rect viewRect(const ControlSpec&)`; i riquadri dei moduli e la regola di appartenenza `enum class Module { None, Rider, Breath, Eq, DeEss, Comp, Tone }` con `Module moduleOf(ParamID)` (Rider: Target, Range; ecc.; l'IN di un modulo e il suo Listen non appartengono al proprio modulo per l'attenuazione ma sì al Bypass); `struct DimState { bool bypass; bool in[6]; }`, `bool controlDimmed(ParamID, const DimState&)`; `constexpr Rect kScopeRect`, `kMeterRows` (ingresso L/R, uscita L/R, GR: 15 LED, `kDispInL..kDispGr`), `kClips` (IN, OUT), `kPanelTexts`, `std::vector<FairyTapes::Ui::PrintedItem> silkscreenItems()`, `kTapeSlots[6]`, `kScrewPos[4][2]`.
- Regola di trascrizione dalla preview: i contenuti dei moduli (`modBox`) hanno l'origine spostata di −12 px in y; IN in `(x + w − inset, y + 38)` con l'inserzione che la preview usa in `modBox` (BREATH e DE-ESS ne hanno una maggiore per IN e Listen); gli elementi fuori da `modBox` (INPUT, OUTPUT, MODE, SCOPE, LEVEL) hanno le coordinate finali. Il Bypass è nel riquadro OUTPUT. Le coordinate esatte si leggono da `src/deep-throat/docs/mockups/deep_throat_rack.html`, che è la fonte.

- [ ] **Step 1: Scrivere i test** in `tests/test_ui.cpp` (harness copiato dal plugin): `test_layout_binds_every_param_once` (ogni ID da 0 a `kNumParams - 1` compare in esattamente un `ControlSpec`; nessun ID di visualizzazione), `test_layout_rects_inside` (ogni `viewRect` dentro il pannello e dentro il proprio riquadro; ogni riquadro dentro il pannello e senza sovrapposizioni tra riquadri), `test_layout_no_control_overlap` (nessuna coppia di `viewRect` si interseca), `test_layout_in_buttons_top_right` (ogni IN ha il centro a 38 px sotto il bordo alto del suo riquadro e a destra della metà), `test_layout_titles_bottom` (ogni `BoxSpec` ha un titolo non vuoto e in maiuscolo), `test_dimming_rules` (bypass attenua tutto tranne `kBypass` e gli IN; IN spento attenua i controlli del modulo ma non il proprio IN né gli altri moduli; SCOPE e LEVEL non sono controlli), `test_silkscreen_items_cover_controls` (un elemento per ogni manopola, pulsante e ciclo; i nomi in inglese e senza marchi: nessuna delle stringhe contiene "distressor" in minuscolo).
- [ ] **Step 2: Eseguire e vedere fallire** (header assente).
- [ ] **Step 3: Implementare** `editor_size.h` e `layout.h` trascrivendo la preview; aggiungere l'eseguibile al CMake.
- [ ] **Step 4: Eseguire** `cmake --build /tmp/dt-build --target deep_throat_ui_tests -j2` e `ctest -R deep_throat_ui_tests` → PASS.
- [ ] **Step 5: Commit:** `feat(deep-throat): editor layout table (pure)`.

---

### Task 5: Scritte dinamiche (pure)

**Files:**
- Create: `src/deep-throat/src/ui/dynamic_text.h`
- Modify: `src/deep-throat/tests/test_ui.cpp`

**Interfaces:**
- Consumes: `modeRow` (Task 1), `compRatioFromChoice`, `kCompRatioCount` e simili da `params.h`.
- Produces (tutte pure, in `FairyTapes::DeepThroat::Ui`): `std::string eqHpfText(Mode, double eqAmountNorm)` (`round(hpf · (0.5 + a)) + " Hz"`), `std::string eqMudText(Mode, double eqAmountNorm)` (`mud · 2a` a una cifra, "+" per i positivi, " dB"), `std::string toneGainText(Mode, int band /*0 mud, 1 presence, 2 air*/, double toneNorm)` (`gain · (tone − 0.5) · 2`; sotto 0.05 in valore assoluto "0.0 dB", altrimenti segno e una cifra), `std::string ratioText(int choice, Mode)` ("AUTO 2:1" con scelta 0, altrimenti "2:1", …, "20:1"), `std::string distText(int choice, Mode)` ("AUTO OFF", "AUTO DIST 2", "AUTO DIST 3", "OFF", "DIST 2", "DIST 3"), `std::string hpfChoiceText(int)` ("OFF", "80 Hz", "150 Hz"), `std::string modeTagLine1(Mode)` e `modeTagLine2(Mode)` (la descrizione di `ModeRow` divisa ai due punti, in maiuscolo; "SUNG VOICE" e riga vuota per Sing), `const char* detectorTag(Mode)` ("SPECTRAL" / "LEVEL"), `std::string riderMaxText(Mode)` ("MAX ±6 dB · 8 dB/s"), `std::string deEssNote(Mode)` ("MOSTLY OFF IN PUKE (X0.15)" solo in Puke, altrimenti vuota).

- [ ] **Step 1: Scrivere i test** in `test_ui.cpp` con le stringhe esatte: `test_eq_texts` (Speak Amount 0.5: "80 Hz" e "-2 dB"; Speak Amount 1: "120 Hz" e "-4 dB"; Puke Amount 0: "30 Hz" e "0 dB"; Sing Amount 0.5: "70 Hz" e "-3 dB"), `test_tone_gain_texts` (Speak tone 1: mud "-2.0 dB", presence "+3.0 dB", air "+2.0 dB"; tone 0.5: "0.0 dB"; tone 0 in Sing: mud "+1.5 dB", presence "-2.0 dB", air "-4.0 dB"; Puke air a ogni tone "0.0 dB"), `test_ratio_dist_texts` (`ratioText(0, Sing)` = "AUTO 3:1", `ratioText(7, Speak)` = "20:1", `distText(0, Puke)` = "AUTO DIST 3", `distText(1, Sing)` = "OFF", `hpfChoiceText(1)` = "80 Hz"), `test_mode_tags` (tre modalità: righe e `detectorTag`), `test_rider_max_text` ("MAX ±6 dB · 8 dB/s", "MAX ±4 dB · 4 dB/s", "MAX ±3 dB · 2 dB/s"), `test_de_ess_note_only_in_puke`, `test_texts_follow_the_table` (per ogni modalità i numeri nelle stringhe derivano da `modeRow`: cambiando temporaneamente un valore della tabella in una copia locale il testo cambia: usare le funzioni sulle `ModeRow` passate come parametro nelle versioni interne e verificare l'uguaglianza con la versione pubblica).
- [ ] **Step 2: Eseguire e vedere fallire.**
- [ ] **Step 3: Implementare** `dynamic_text.h`; le funzioni pubbliche chiamano versioni interne che prendono una `const ModeRow&`.
- [ ] **Step 4: Eseguire** `ctest -R deep_throat_ui_tests` → PASS.
- [ ] **Step 5: Commit:** `feat(deep-throat): dynamic panel texts from the mode table (pure)`.

---

### Task 6: Preferenze, decor visivo e storia dello scope (pure)

**Files:**
- Create: `src/deep-throat/src/ui/{prefs.h,prefs.cpp,decor.h,decor.cpp,screw_angles.h,tapes.h,history.h}`
- Modify: `src/deep-throat/tests/test_ui.cpp`, `src/deep-throat/CMakeLists.txt` (sorgenti nel target dei test UI)

**Interfaces:**
- Produces (modelli: file omonimi di `src/pillow-talk/src/ui/`): `Prefs { WearLevel wear = Light; bool notes = true; TapeFont tapeFont = Zeyada; bool handwrittenValues = true; }`, `Prefs parsePrefs(const std::string&)`, `std::string serializePrefs(const Prefs&)`, `std::filesystem::path prefsPath()`, `Prefs loadPrefs()`, `bool savePrefs(const Prefs&)` (file `DeepThroat.prefs`, scrittura atomica); `WearLayout layoutFromSeed(uint32_t seed, WearLevel, const Rect& panel)` con `TapePlacement`/`ScrewPlacement` (nastri: 0 per Off, 1-2 per Light, 2-3 per Heavy, sempre dentro il pannello, mai su controlli, scope, righe LEVEL e LED di clip: gli slot di `kTapeSlots` del Task 4); `screw_angles.h` e `tapes.h` come in Pillow Talk (angoli iniziali delle viti dal seme, testi iniziali dei nastri); `history.h`: `template<size_t N> class History` buffer circolare di `struct ScopePoint { float voice, rider, breath; }` con `push`, `size`, `at(i)` (0 = il più vecchio) e `void clear()`, più `std::vector<ScopeColumn>` per la conversione in colonne di pixel: `ScopeColumn { float voiceTop; float riderY; bool breath; }` da `ScopePoint` e `Rect` (inviluppo della voce in alto a partire da -60..0 dB, guadagno del Rider centrato con ±6 dB, banda del respiro dove la probabilità > 0.5).

- [ ] **Step 1: Scrivere i test** in `test_ui.cpp`: `test_prefs_parse_defaults_and_bad_values` (testo vuoto, riga senza `=`, chiave sconosciuta, valore non valido: ogni valore errato cade sul proprio default), `test_prefs_roundtrip` (serializza e rileggi tutte le combinazioni dei quattro campi), `test_prefs_file_missing_or_oversized_or_binary` (con `XDG_CONFIG_HOME` impostato a una cartella scratch: assente → default; oltre 64 KB → default; binario → default; scrittura riuscita rilegge uguale e non lascia file temporanei), `test_decor_layout_deterministic` (stesso seme → stesso risultato; Off → 0 nastri; Heavy → 2-3; sempre 4 viti, al massimo una mancante), `test_decor_tapes_never_cover_controls` (per 200 semi e tre livelli nessun nastro interseca un `viewRect` di controllo, `kScopeRect`, le righe dei meter o i clip), `test_screw_look_matches_shared_generator` (le viti coincidono con `FairyTapes::Ui::generateScrews` per lo stesso seme), `test_history_ring_and_columns` (riempi oltre la capacità: `at(0)` è il più vecchio, `size == N`; colonne: voce a -60 dB in basso e a 0 dB in cima, Rider +6 dB in alto e -6 dB in basso, respiro > 0.5 acceso, valori NaN/negativi limitati).
- [ ] **Step 2: Eseguire e vedere fallire.**
- [ ] **Step 3: Implementare** copiando e adattando i file di Pillow Talk (nome di file e chiavi come sopra, dimensioni e slot del Task 4); collegare i sorgenti al target dei test UI.
- [ ] **Step 4: Eseguire** `ctest -R deep_throat_ui_tests` → PASS.
- [ ] **Step 5: Commit:** `feat(deep-throat): preferences, seeded decor and scope history (pure)`.

---

### Task 7: Scheletro dell'editor, pannello, createView e render fuori schermo

**Files:**
- Create: `src/deep-throat/src/ui/{editor.h,editor.cpp,panel_view.h,panel_view.cpp}`, `src/deep-throat/tools/render_editor.cpp`, `src/deep-throat/tests/test_editor.cpp`
- Modify: `src/deep-throat/src/{controller.h,controller.cpp}` (`createView`), `src/deep-throat/CMakeLists.txt` (sorgenti UI nel plugin, `sdk fairy_ui fairy_ui_widgets`, asset della grana, target `deep_throat_render`), `src/deep-throat/tests/test_factory.cpp`

**Interfaces:**
- Consumes: `layout.h` (Task 4), `Decor`/`Controller::decor()` (Task 3), `prefs.h` (Task 6).
- Produces: `class Editor : public Steinberg::Vst::VSTGUIEditor, public Steinberg::IPlugViewContentScaleSupport, public VSTGUI::IControlListener` come `src/pillow-talk/src/ui/editor.h` (finestra fissa 1000 × 614 per il fattore di scala, `open`, `close`, `checkSizeConstraint`, `setContentScaleFactor`); `class PanelView : public VSTGUI::CView` (sfondo `#161617` con la grana di `shared/ui/assets/panel_grain.png` se presente, altrimenti piatto; bordi dei riquadri con interruzione per il titolo sul bordo basso; testi di `kPanelTexts`); `Controller::createView(FIDString)` restituisce l'editor per `ViewType::kEditor`, altrimenti `nullptr`; `struct EditorPreview` e il target `deep_throat_render` (modello `tools/render_editor.cpp` di Pillow Talk: disegna fronte e schede del retro in PNG in una cartella passata da riga di comando).

- [ ] **Step 1: Scrivere i test** in `test_factory.cpp`: `test_create_view_returns_editor` (per `kEditor` una vista con dimensione 1000 × 614 e conteggio dei riferimenti corretto; per un nome diverso `nullptr`), `test_editor_open_close_repeatedly` (creare, rilasciare, ricreare 50 volte senza finestra: nessun crash, conteggio dei riferimenti a zero alla fine). Per il render: il target si costruisce e, lanciato con una cartella scratch, scrive `front.png` con dimensioni 1000 × 614 (controllo del file: esiste, intestazione PNG, larghezza e altezza) — in `test_ui.cpp` non (richiede VSTGUI): in `test_factory.cpp` o in un test CTest dedicato `deep_throat_render_smoke` che esegue il tool.
- [ ] **Step 2: Eseguire e vedere fallire.**
- [ ] **Step 3: Creare il banco di prova dell'editor** `deep_throat_editor_tests` (`tests/test_editor.cpp`, harness `RUN_TEST`): si costruisce come `pillow_talk_render` (compila `controller.cpp`, i sorgenti del motore e dell'editor, `sdk`, `pluginterfaces`, `fairy_ui`, `fairy_ui_widgets`; nessuna finestra, `moduleinit.cpp` e `pluginfactory.cpp` dell'SDK) con una struct amica `EditorTest` dell'`Editor` (come `EditorPreview`) che espone: costruzione delle viste senza finestra, un tick del timer UI a comando, accesso al controllo di un `ParamID` e alle stringhe della serigrafia. Un `Controller` reale pilotato direttamente fa da host (`setParamNormalized`, i listener dei parametri). Lo usano i Task 8-11. Poi implementare editor (solo livello pannello: nessun controllo ancora), `createView` e il tool di render; VSTGUI compila con `-j2`, un target alla volta.
- [ ] **Step 4: Eseguire** build di `deep_throat deep_throat_factory_test deep_throat_render` e i test → PASS; aprire `front.png` e controllare a occhio titoli in basso, bordi e testi (dichiararlo nel rapporto).
- [ ] **Step 5: Commit:** `feat(deep-throat): editor skeleton with the panel layer, createView and the offscreen render tool`.

---

### Task 8: Controlli legati ai parametri

**Files:**
- Modify: `src/deep-throat/src/ui/{editor.h,editor.cpp}`, `src/deep-throat/tests/test_editor.cpp`
- Create: `src/deep-throat/src/ui/value_text.h` (formattazione dei valori stampati sotto le manopole e analisi dei valori digitati: modello `src/the-stripper/src/ui/value_text.h`, adattato; test in `test_ui.cpp`)

**Interfaces:**
- Consumes: `kControls` e `silkscreenItems()` (Task 4), `dynamic_text.h` (Task 5).
- Produces: nell'editor, una vista per ogni `ControlSpec` (`RackKnob`, `RackToggle`, `RackLitChoice` per Mode con le tre facce impilate, `RackCycle` per Ratio, Dist e SC HPF), la `SilkscreenView` con i nomi, le scale e i valori correnti (anche le scritte dinamiche: sigla del rilevatore, descrizione della modalità, `MAX ±`, HPF e MUD CUT, i tre dB del Tone, nota del de-esser), i valori delle manopole con il font dei valori delle preferenze, `valueChanged` / `controlBeginEdit` / `controlEndEdit` → `beginEdit` / `performEdit` / `endEdit`, il timer UI che applica i cambi dell'host (i `Parameter` sono dipendenti: `update` imposta solo un flag), l'attenuazione (IN, Bypass) e la regola della manopola con campo di testo (clic sul valore → campo modificabile; invio conferma, Esc annulla; il testo digitato passa per `Controller::getParamValueByString`); `std::string formatKnobValue(ParamID id, double norm)` e `bool parseKnobValue(ParamID id, const std::string&, double& norm)` in `value_text.h` (db, percentuale, kHz con una cifra, dBFS, "+100 %").

- [ ] **Step 1: Scrivere i test:** in `test_ui.cpp`: `test_value_text_formats` (Input 0.5 → "0.0 dB", 1 → "+12.0 dB"; Rider Target 0.5 → "-20.0 dBFS"; Range 0.5 → "50 %"; De-ess Freq 0.53 → "6.5 kHz"; Comp Amount 0.5 → "-18.0 dB"; Tone 1 → "+100 %"), `test_value_text_parse_roundtrip` (per ogni manopola: `parse(format(v))` entro la risoluzione della cifra stampata; testo non valido → `false`), `test_value_text_matches_controller` (le stringhe coincidono con `Controller::getParamStringByValue` per tutte le manopole a 0, 0.25, 0.5, 1: confronto di tipi tramite funzione pura condivisa o stesso formattatore: se il controller ha già un formattatore puro, usarlo). In `test_editor.cpp`: `test_editor_controls_follow_host` (con il fake host: `setParamNormalized` per ognuno dei 25 parametri → il valore del controllo corrispondente cambia entro un tick del timer, senza richiamare `performEdit`), `test_user_edit_reaches_host_once` (un `valueChanged` genera esattamente un `beginEdit/performEdit/endEdit` e nessun ritorno), `test_double_click_resets_default`, `test_mode_change_updates_dynamic_texts` (cambiando Mode da host, la serigrafia dinamica mostra le stringhe di `dynamic_text.h` per la nuova modalità), `test_in_off_dims_module_controls`.
- [ ] **Step 2: Eseguire e vedere fallire.**
- [ ] **Step 3: Implementare** modello `src/pillow-talk/src/ui/editor.cpp` (costruzione per livelli, `bindControl`, timer, dipendenti); mutazione dimostrata: se `update` smette di segnare il parametro, `test_editor_controls_follow_host` fallisce.
- [ ] **Step 4: Eseguire** build e test → PASS; `deep_throat_render` e controllo a occhio di `front.png` (controlli al loro posto, valori leggibili).
- [ ] **Step 5: Commit:** `feat(deep-throat): editor controls bound to the 25 parameters`.

---

### Task 9: Meter, clip e scope

**Files:**
- Create: `src/deep-throat/src/ui/{scope_view.h,scope_view.cpp}`
- Modify: `src/deep-throat/src/ui/{editor.h,editor.cpp}`, `src/deep-throat/tests/test_editor.cpp`

**Interfaces:**
- Consumes: `kMeterRows`, `kClips`, `kScopeRect` (Task 4), `History`/`ScopeColumn` (Task 6), `displayNorms` (Task 2).
- Produces: `class ScopeView : public VSTGUI::CView` (rettangolo nero con bordo, linea tratteggiata a metà, inviluppo bianco della voce, linea ambra del guadagno del Rider, banda rossa semitrasparente del respiro, legenda VOICE / RIDER GAIN / BREATH sotto); in `Editor`: righe di `MeterBar` (15 LED, -60..+6 dB; la riga GR 0..21 dB, ambra con gli ultimi quattro rossi), `ClipLed` IN e OUT (un clic li azzera: scrive `kClearClips`), `PeakHold` come negli altri plugin; il timer legge i parametri `kDisp*`, alimenta la `History` (292 punti) e aggiorna le viste; SCOPE e LEVEL si attenuano solo col Bypass (le righe IN/OUT dei meter restano accese).

- [x] **Step 1: Scrivere i test:** `test_meters_follow_display_params` (con il fake host: portare `kDispInL` a 0.5 accende ~metà dei 15 LED e `kDispClipIn` a 1 accende il clip), `test_clip_click_clears_via_trigger` (un clic su un clip scrive `kClearClips` = 1 e il processor di test lo azzera), `test_scope_history_grows_to_292` (dopo 400 tick la storia ha 292 punti e il più vecchio è il tick 108), `test_scope_dimming_only_on_bypass` (IN spenti non attenuano scope e meter; Bypass sì, tranne le righe IN/OUT e i clip), `test_editor_timer_stops_on_close` (dopo `close()` il timer non gira e nessuna vista viene toccata: ripetere open/close 50 volte con tick in mezzo).
- [x] **Step 2: Eseguire e vedere fallire.**
- [x] **Step 3: Implementare** `ScopeView`, i meter e la lettura dei parametri nel timer (modello: `duck_scope.cpp` e la parte dei meter di `editor.cpp` di Pillow Talk).
- [x] **Step 4: Eseguire** build e test → PASS; render e controllo a occhio dello scope con dati di prova (il tool di render accetta valori di prova per i parametri di visualizzazione).
- [x] **Step 5: Commit:** `feat(deep-throat): LEVEL meters, clip LEDs and the SCOPE view`.

---

### Task 10: Usura, nastri, viti e preferenze nell'editor

**Files:**
- Create: `src/deep-throat/src/ui/{tape_view.h,tape_view.cpp}`
- Modify: `src/deep-throat/src/ui/{editor.h,editor.cpp}`, `src/deep-throat/tests/test_editor.cpp`

**Interfaces:**
- Consumes: `Decor` e `Controller::decor/editDecor/decorRevision` (Task 3), `layoutFromSeed` e `Prefs` (Task 6), `WearView`, `ScrewView`, `TapeView` (Pillow Talk: `tape_view`).
- Produces: nell'editor, strati usura (`WearView`) tra serigrafia e controlli, nastri (`TapeView`, testo modificabile al clic: al massimo 14 caratteri, invio/uscita conferma, Esc annulla; il testo va a `editDecor`), viti (`ScrewView`: l'angolo si ruota trascinando e a fine trascinamento va a `editDecor`), livello di usura e font dei nastri dalle preferenze lette all'apertura; `setWearLevel(WearLevel)`, `setTapeFont(TapeFont)`, `setValuesFont(bool handwritten)`, `reroll()` (nuovo seme, viti e nastri azzeriti); il timer controlla `decorRevision()` e ridisegna solo se cambia.

- [ ] **Step 1: Scrivere i test:** `test_wear_off_has_no_marks_and_no_tapes`, `test_wear_level_change_redraws_without_rebuild`, `test_tape_text_edit_goes_to_decor` (testo di 40 caratteri → 14, ASCII, e il controller riceve una `editDecor`), `test_screw_drag_stores_angle_once` (un trascinamento = una sola `editDecor` a fine gesto), `test_reroll_changes_seed_and_clears_decor`, `test_prefs_missing_file_opens_with_defaults` (cartella di preferenze inesistente o file corrotto: l'editor si apre con Light/Zeyada/Tape), `test_missing_grain_asset_draws_plain_panel` (asset assente: nessun crash, sfondo piatto).
- [ ] **Step 2: Eseguire e vedere fallire.**
- [ ] **Step 3: Implementare** sul modello di `src/pillow-talk/src/ui/editor.cpp` (wear, tapes, screws) e `tape_view.*`.
- [ ] **Step 4: Eseguire** build e test → PASS; render con Wear Off, Light e Heavy e controllo a occhio che nastri e viti non coprano controlli.
- [ ] **Step 5: Commit:** `feat(deep-throat): wear, paper tapes and screws with user preferences`.

---

### Task 11: Retro: flip e scheda SETTINGS

**Files:**
- Create: `src/deep-throat/src/ui/back_settings.h`
- Modify: `src/deep-throat/src/ui/{editor.h,editor.cpp}`, `src/deep-throat/tests/test_ui.cpp`, `src/deep-throat/tests/test_editor.cpp`

**Interfaces:**
- Consumes: `FairyTapes::Ui::BackView`, `PanelFlip` (`fairy_ui/back_view.h`, `flip_view.h`, `back_panel.h`), `Prefs` (Task 6).
- Produces: `back_settings.h` (modello `src/pillow-talk/src/ui/back_settings.h`): le voci di APPEARANCE (Wear: Off/Light/Heavy; Tape font: Zeyada/Rock Salt/Homemade Apple; Values font: Tape font/Standard; RE-ROLL) e di NOTES come nella preview, con le funzioni pure `std::vector<...> settingsItems(const Prefs&)` e la corrispondenza voce → modifica delle preferenze; nell'editor: il link "BACK →" in alto a destra dell'intestazione, il flip (circa 0,8 s, un campo di testo aperto su un nastro si conferma prima), "← BACK" per tornare, scheda SETTINGS con il template fisso del retro (titolo corsivo 38 px = scheda corrente, "FAIRY TAPES", "DEEP THROAT · VOCAL CHANNEL STRIP VST3", link SETTINGS, MODES, ROUTING, CREDITS); le modifiche alle preferenze sono applicate subito e salvate (una scrittura fallita è ignorata).

- [ ] **Step 1: Scrivere i test:** in `test_ui.cpp`: `test_settings_items_reflect_prefs` (per ogni combinazione dei campi, la voce attiva è quella giusta), `test_settings_item_to_prefs` (ogni clic produce la `Prefs` attesa e non tocca gli altri campi). In `test_editor.cpp`: `test_flip_roundtrip_and_text_edit_confirmed_first`, `test_settings_apply_wear_at_once`, `test_settings_failed_write_is_ignored` (cartella delle preferenze non scrivibile: l'editor continua e il valore resta applicato per la sessione), `test_tab_links_order` (SETTINGS, MODES, ROUTING, CREDITS).
- [ ] **Step 2: Eseguire e vedere fallire.**
- [ ] **Step 3: Implementare** flip e SETTINGS; le altre tre schede restano per ora vuote ma selezionabili.
- [ ] **Step 4: Eseguire** build e test → PASS; render della scheda SETTINGS e controllo a occhio contro la preview.
- [ ] **Step 5: Commit:** `feat(deep-throat): back of the panel with the flip and the SETTINGS tab`.

---

### Task 12: Retro: schede MODES, ROUTING e CREDITS

**Files:**
- Create: `src/deep-throat/src/ui/{back_modes.h,routing_diagram.h,credits.h}`
- Modify: `src/deep-throat/src/ui/editor.cpp`, `src/deep-throat/tests/test_ui.cpp`

**Interfaces:**
- Consumes: `kModeTable` (Task 1), `FairyTapes::Ui::RoutingBuilder` (`fairy_ui/routing.h`), il template del retro (Task 11).
- Produces: `back_modes.h`: `struct ModesRow { std::string label; std::array<std::string, 3> cells; };` `std::vector<ModesRow> modesRows()` (le nove righe della preview: Breath detector, Rider max range, Rider speed, HPF, Mud cut, De-ess, Comp ratio, Dist, Tone curve, tutte generate da `modeRow`), `std::vector<std::string> modesNotes()` (le note sotto la tabella), `int activeModeColumn(Mode)`; `routing_diagram.h`: `FairyTapes::Ui::RoutingDiagram deepThroatRouting()` (schema fisso trascritto dalla preview con `RoutingBuilder`: riquadro SIGNAL FLOW con ingresso → Rider → Breath → EQ → De-ess → Comp → Tone → uscita, rilevatori e meter tratteggiati, deviatori IN, Bypass che collega ingresso e uscita; riquadro LEGEND); `credits.h`: `std::vector<CreditEntry> credits()` (VST 3 SDK, Steinberg, MIT; VSTGUI, Steinberg, BSD-3-Clause; Zeyada, OFL-1.1; Rock Salt e Homemade Apple, Apache-2.0) e il testo di NOTICES (marchio VST); nell'editor le tre schede sono costruite con `BackView`, MODES con la colonna attiva in giallo.

- [ ] **Step 1: Scrivere i test** in `test_ui.cpp`: `test_modes_rows_from_table` (le righe e le celle coincidono, riga per riga e modalità per modalità, con i valori di `modeRow` e con le stringhe di `dynamic_text.h`: "SPECTRAL"/"LEVEL", "±6 dB", "8 dB/s", "80 Hz", "-2 dB", "normal"/"x0.15", "2:1", "OFF"/"DIST 2"/"DIST 3", curve del Tone), `test_modes_follow_table_edit` (con una copia locale della tabella modificata la riga cambia), `test_active_mode_column`, `test_routing_diagram_blocks_in_engine_order` (i riquadri compaiono da sinistra a destra nell'ordine Rider, Breath, EQ, De-ess, Comp, Tone; ci sono 6 deviatori IN, un Bypass, almeno tre linee tratteggiate), `test_routing_diagram_inside_panel`, `test_credits_entries` (esattamente le cinque voci con autore e licenza; nessun altro componente di terzi), `test_no_brand_in_user_facing_strings` (nessuna stringa delle schede o del layout contiene "distressor" o "hardware" di marchi).
- [ ] **Step 2: Eseguire e vedere fallire.**
- [ ] **Step 3: Implementare** i tre header e le schede.
- [ ] **Step 4: Eseguire** build e test → PASS; render delle tre schede e controllo a occhio contro la preview.
- [ ] **Step 5: Commit:** `feat(deep-throat): MODES, ROUTING and CREDITS tabs generated from the mode table`.

---

### Task 13: Documentazione e chiusura

**Files:**
- Modify: `src/deep-throat/README.md`, `docs/superpowers/specs/2026-10-24-deep-throat-gui-design.md`, questo piano (spuntare i task), `docs/superpowers/specs/2026-10-10-deep-throat-design.md` (§10: l'editor esiste), `CLAUDE.md` (§8: puntatori alla spec e al piano del GUI)

- [ ] **Step 1: Aggiornare il README** (italiano): editor (come si apre, le schede del retro, preferenze e dove sono salvate, tool di render e come usarlo), limiti noti (nessun confronto automatico con la preview, colori e flip non provati senza un host, scope sulla linea temporale dell'ingresso con il guadagno del Rider sfalsato di D).
- [ ] **Step 2: Allineare le spec e il piano:** decisioni prese durante i task (in particolare quelle del registro dell'esecuzione), spunte, "Decisioni aperte" della spec del GUI, stato della spec ("bozza" → "implementata").
- [ ] **Step 3: Verifica finale:** costruire `deep_throat deep_throat_tests deep_throat_factory_test deep_throat_ui_tests deep_throat_render -j2`, `ctest -R deep_throat --output-on-failure`, riportare l'output; eseguire il render di tutte le schede e controllare a occhio che nulla sia sovrapposto o fuori dal pannello (dichiararlo come controllo manuale); verificare che il bundle `DeepThroat.vst3` si costruisca e che `deep_throat_factory_test` lo carichi.
- [ ] **Step 4: Commit:** `docs(deep-throat): editor readme and aligned spec and plan`.

---

## Self-review

- **Copertura della spec del GUI:** §2-3 struttura e decisioni → Task 1-12; §4 pannello e controlli → Task 4, 5, 8; §5 dati di visualizzazione → Task 2, 9; §6 stato v2 e preferenze → Task 3, 6, 10, 11; §7 tabella unica → Task 1, 5, 12; §8 retro → Task 11, 12; §9 test e render → in ogni task e Task 7, 13; §10 rischi (VSTGUI pesante, parametri nascosti) → vincoli globali e Task 2, 7; §11 decisioni aperte (finestra scope fissa, confronto manuale) → Task 9 e 13.
- **Coerenza dei tipi:** `Decor` (Task 3) usato da Task 6 (`layoutFromSeed`), 10 e 11; `ModeRow`/`modeRow` (Task 1) usati da Task 5 e 12; `DisplayID`, `displayNorms` (Task 2) usati da Task 9; `kControls`/`ControlSpec` (Task 4) usati da Task 5, 8, 9; `Prefs` (Task 6) usato da Task 10 e 11.
- **Soglie e coordinate:** le coordinate dei controlli vengono dalla preview; i test del Task 4 fissano invarianti (parametri legati una volta, rettangoli dentro i riquadri, nessuna sovrapposizione), non i pixel.
