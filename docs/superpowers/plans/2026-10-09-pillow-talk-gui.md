# Pillow Talk — Editor grafico Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Montare in Pillow Talk un editor VSTGUI4 con manopole, pulsanti e un display del ducking in tempo reale, disegnati dal codice e pronti a ricevere skin.

**Architecture:** Libreria condivisa `shared/ui` (matematica dei controlli, controlli disegnati dal codice, caricamento opzionale di immagini). Il processor pubblica tre valori di display come parametri in sola lettura; l'editor li accumula in un buffer circolare e li disegna.

**Tech Stack:** C++20, VST3 SDK + VSTGUI4 (`vstgui_support`), CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-10-09-pillow-talk-gui-design.md`

## Global Constraints

- Branch `feat/pillow-talk-gui`, impilato su `feat/pillow-talk` (PR #1).
- Namespace `FairyTapes::Ui` (codice condiviso) e `FairyTapes::PillowTalk::Ui` (plugin). Nessun include dallo Stripper.
- Editor 480 × 560 fisso; view costruite in codice, senza `.uidesc`.
- Il thread audio non alloca e non attende; i parametri di display sono in sola lettura (`kIsReadOnly | kIsHidden`), da `kMeterBase = 1000`, scritti al massimo ogni 16 ms, fuori da `kNumParams` e dallo stato.
- Ogni controllo legato a un `ParamID` con `beginEdit`/`performEdit`/`endEdit`.
- Le immagini della skin sono opzionali: se mancano, disegno in codice.
- Build pesanti: sempre `-j2`, mai tutto insieme; `-DCMAKE_BUILD_TYPE=Release -DGPU_NAM_DISABLE_CUDA=ON`.
- Licenza GPL-3.0.

## Review Focus

- Aprire e chiudere l'editor 50 volte di fila: nessun crash, nessun listener rimasto agganciato.
- Il display con host che non inoltra i parametri di sola lettura: resta fermo senza errori.
- Sync acceso/spento mentre si automatizza Time o Hold: nessun valore fuori range, nessun controllo bloccato.
- Chiusura dell'editor mentre il timer è attivo: nessun accesso a view distrutte.
- Cambio di scala/DPI della finestra dell'host: i controlli restano allineati.

---

### Task 1: Matematica dei controlli in `shared/ui`

**Files:**
- Create: `shared/ui/CMakeLists.txt`, `shared/ui/include/fairy_ui/knob_math.h`, `shared/ui/tests/test_knob_math.cpp`
- Modify: `CMakeLists.txt` (aggiungere `add_subdirectory(shared/ui)`)

**Interfaces:**
- Produces, in `knob_math.h` (namespace `FairyTapes::Ui`, senza dipendenze da VSTGUI): `double knobAngleFromNorm(double n)` (da 225° a -45°, rotazione oraria, clamp), `double normFromDrag(double startNorm, double dyPixels, bool fine)` (200 px = pieno range, 1/10 in modalità fine, clamp 0..1), `int choiceIndexFromNorm(double n, int count)`, `double normFromChoiceIndex(int i, int count)`, `double meterNormFromDb(double db, double minDb, double maxDb)` (lineare in dB, clamp). Libreria `fairy_ui` di tipo `INTERFACE` con `include/` come include dir.

- [ ] **Step 1: Scrivere i test** (macro come `src/pillow-talk/tests/test_pillow_talk.cpp`): `test_knobAngle_endpoints` (0 → 225, 1 → -45, 0.5 → 90, valori fuori range clampati); `test_normFromDrag_full_range_and_fine` (200 px su da 0 → 1; fine: 200 px → 0.1); `test_normFromDrag_clamps`; `test_choice_index_roundtrip` (count 2, 3, 4: `choiceIndexFromNorm(normFromChoiceIndex(i, c), c) == i`); `test_meterNormFromDb` (-60 → 0, +6 → 1, -27 → 0.5 entro 1e-9, fuori range clampato).
- [ ] **Step 2: Creare il CMake** (libreria `INTERFACE` + eseguibile `fairy_ui_tests` + `add_test`) e aggiungerlo alla radice.
- [ ] **Step 3: Eseguire, verificare il fallimento.**
  Run: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DGPU_NAM_DISABLE_CUDA=ON && cmake --build build --target fairy_ui_tests -j2`
  Expected: errore di compilazione (header mancante).
- [ ] **Step 4: Implementare `knob_math.h`.**
- [ ] **Step 5: Eseguire i test.**
  Run: `cmake --build build --target fairy_ui_tests -j2 && ctest --test-dir build -R fairy_ui --output-on-failure`
  Expected: PASS.
- [ ] **Step 6: Commit**

```bash
git add shared/ui CMakeLists.txt
git commit -m "feat(ui): add shared knob math library"
```

---

### Task 2: Dati di display dal processor

**Files:**
- Modify: `src/pillow-talk/src/params.h` (intervallo `kMeterBase`), `src/pillow-talk/src/engine.{h,cpp}`, `src/pillow-talk/src/processor.{h,cpp}`, `src/pillow-talk/src/controller.{h,cpp}`, `src/pillow-talk/tests/test_pillow_talk.cpp`

**Interfaces:**
- Produces:
  - In `params.h`: `constexpr Steinberg::Vst::ParamID kMeterBase = 1000;` e `enum DisplayID : Steinberg::Vst::ParamID { kDispVoice = kMeterBase, kDispSend, kDispEcho, kNumDisplayIDs };` (valori normalizzati 0..1: voce e picco dell'eco in dB da -60 a 0 tramite `meterNormFromDb`, guadagno di mandata lineare 0..1)
  - In `Engine`: `struct DisplayValues { float voiceDb; float sendGain; float echoDb; }; DisplayValues lastDisplay() const;` aggiornato a ogni blocco senza allocazioni (voce = picco del segnale di rilevamento, guadagno = ultimo valore del Ducker, eco = picco del segnale wet dopo il ducking)

- [ ] **Step 1: Scrivere i test:** `test_display_silence_is_floor_and_unity_send` (silenzio → voiceDb ≤ -60, sendGain 1, echoDb ≤ -60); `test_display_voice_follows_signal` (sinusoide a -12 dBFS → `voiceDb` entro ±1.5 dB); `test_display_send_drops_above_threshold` (burst forte → `sendGain` < 0.1 con Depth di default); `test_display_echo_appears_after_phrase` (dopo burst e hold+release, `echoDb` > -50); `test_display_ids_do_not_collide` (`kMeterBase >= kNumParams`); `test_display_not_in_state` (la dimensione dello stato serializzato non cambia); `test_display_write_rate_limit` (con blocchi da 64 campioni a 48 kHz, al massimo una scrittura ogni 16 ms).
- [ ] **Step 2: Eseguire, verificare il fallimento.**
- [ ] **Step 3: Implementare** `lastDisplay()` in `Engine`; nel `Processor` scrivere i tre valori in `outputParameterChanges` al massimo ogni 16 ms; nel `Controller` registrarli con `kIsReadOnly | kIsHidden`.
- [ ] **Step 4: Eseguire i test.**
  Run: `cmake --build build --target pillow_talk_tests -j2 && ctest --test-dir build -R pillow_talk --output-on-failure`
  Expected: PASS.
- [ ] **Step 5: Commit**

```bash
git add src/pillow-talk
git commit -m "feat(pillow-talk): publish display values as read-only parameters"
```

---

### Task 3: Integrazione VSTGUI4 e controlli disegnati dal codice

**Files:**
- Create: `shared/ui/include/fairy_ui/controls.h`, `shared/ui/include/fairy_ui/skin.h`, `shared/ui/src/controls.cpp`, `shared/ui/src/skin.cpp`
- Modify: `CMakeLists.txt` (attivare `SMTG_ENABLE_VSTGUI_SUPPORT`), `shared/ui/CMakeLists.txt` (libreria `fairy_ui_widgets` che collega `vstgui_support`), `.github/workflows/build.yml` (pacchetti `apt` per VSTGUI nel job Linux)

**Interfaces:**
- Consumes: `knob_math.h` (Task 1).
- Produces (namespace `FairyTapes::Ui`):
  - `class AnalogKnob : public VSTGUI::CControl` — disegna con `CDrawContext` arco del valore ambra, corpo scuro, indicatore chiaro; trascinamento verticale con `normFromDrag`, doppio clic al valore di default, Maiuscolo per il fine; `void setDimmed(bool)` per attenuarla; testo di valore e etichetta.
  - `class ToggleButton : public VSTGUI::CControl` — piatto, ambra se attivo.
  - `class ChoiceButtons : public VSTGUI::CControl` — N pulsanti a selezione esclusiva (`int count`, etichette), valore normalizzato con `normFromChoiceIndex`.
  - `class Skin` — `VSTGUI::CBitmap* bitmap(const char* name) const` (nullptr se il file non esiste nella cartella `Resources` del bundle); i controlli lo usano al posto del disegno se presente.
  - Tutti emettono `beginEdit`/`valueChanged`/`endEdit` tramite `IControlListener`.

- [ ] **Step 1: Verificare la build di VSTGUI** (prima di scrivere codice): attivare l'opzione nella radice, configurare e compilare solo `vstgui_support` e `fairy_ui_widgets` con un file vuoto.
  Run: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DGPU_NAM_DISABLE_CUDA=ON && cmake --build build --target vstgui_support -j2`
  Expected: build senza errori (librerie di sistema già installate su Linux).
- [ ] **Step 2: Implementare i tre controlli e `Skin`** con disegno vettoriale (colori e proporzioni del mockup `src/the-stripper/docs/mockups` per riferimento di stile: corpo `#171614`, arco `#d9a441`, indicatore `#e8e2d4`, testo secondario `#9a9384`).
- [ ] **Step 3: Aggiungere i pacchetti `apt`** al workflow (libcairo2-dev, libfreetype-dev, libfontconfig1-dev, libpango1.0-dev, libxcb1-dev, libxcb-util-dev, libxcb-cursor-dev, libxcb-keysyms1-dev, libxcb-xkb-dev, libxkbcommon-dev, libxkbcommon-x11-dev).
- [ ] **Step 4: Verificare che la libreria compili e che i test esistenti restino verdi.**
  Run: `cmake --build build --target fairy_ui_widgets fairy_ui_tests pillow_talk_tests -j2 && ctest --test-dir build -R "fairy_ui|pillow_talk_tests" --output-on-failure`
  Expected: build senza errori, test PASS.
- [ ] **Step 5: Commit**

```bash
git add shared/ui CMakeLists.txt .github/workflows/build.yml
git commit -m "feat(ui): add VSTGUI4 integration and code-drawn controls"
```

---

### Task 4: Display del ducking

**Files:**
- Create: `src/pillow-talk/src/ui/history.h`, `src/pillow-talk/src/ui/duck_scope.{h,cpp}`
- Modify: `src/pillow-talk/CMakeLists.txt`, `src/pillow-talk/tests/test_pillow_talk.cpp`

**Interfaces:**
- Consumes: `kDisp*` (Task 2), `AnalogKnob` non necessario.
- Produces:
  - `history.h` (senza VSTGUI): `class History { explicit History(int capacity); void push(float voice, float send, float echo); int size() const; float voice(int i) const; float send(int i) const; float echo(int i) const; /* i = 0 è il campione più vecchio */ };` buffer circolare con capacità fissa, nessuna allocazione dopo la costruzione.
  - `class DuckScope : public VSTGUI::CView` (432 × 130): disegna le tre tracce (voce grigia, eco ambra, mandata verde), la soglia tratteggiata e una legenda; `void setThreshold(float norm)`, `void push(float voice, float send, float echo)`.

- [ ] **Step 1: Scrivere i test per `History`:** `test_history_order_before_full`, `test_history_wraps_oldest_first` (dopo `capacity + k` push, `voice(0)` è l'elemento `k`), `test_history_zero_capacity_is_safe`, `test_history_no_allocation_after_construction` (contatore globale di `operator new`).
- [ ] **Step 2: Eseguire, verificare il fallimento.**
- [ ] **Step 3: Implementare `History` e `DuckScope`** (finestra di circa 6 s a 30 ms per campione = 200 punti).
- [ ] **Step 4: Eseguire i test.**
  Run: `cmake --build build --target pillow_talk_tests -j2 && ctest --test-dir build -R pillow_talk_tests --output-on-failure`
  Expected: PASS.
- [ ] **Step 5: Commit**

```bash
git add src/pillow-talk
git commit -m "feat(pillow-talk): add ducking scope view"
```

---

### Task 5: Editor, layout e collegamento ai parametri

**Files:**
- Create: `src/pillow-talk/src/ui/editor.{h,cpp}`
- Modify: `src/pillow-talk/src/controller.{h,cpp}` (`createView`), `src/pillow-talk/CMakeLists.txt` (collegare `vstgui_support`, `fairy_ui_widgets`, i sorgenti dell'editor)

**Interfaces:**
- Consumes: `AnalogKnob`, `ToggleButton`, `ChoiceButtons`, `Skin` (Task 3); `DuckScope`, `History` (Task 4); `kDisp*` (Task 2); `ParamID` e mappature di `params.h`.
- Produces: `class Editor : public Steinberg::Vst::VSTGUIEditor, public VSTGUI::IControlListener`, 480 × 560, con il layout dello spec §5: intestazione (Sidechain, Bypass), display, sezione Delay (6 manopole + Sync + note per Time) e sezione Auto Duck (4 manopole + Sync + note per Hold). `Controller::createView("editor")` ritorna un `Editor`.

- [ ] **Step 1: Implementare `Editor::open`/`close`:** costruire le view in codice con una tabella `struct ControlSpec { ParamID id; Kind kind; CRect rect; const char* label; }` (nessuna coordinata sparsa nel codice); registrare il listener dei controlli e un timer da 30 ms (`CVSTGUITimer`) che legge `kDisp*` dal controller e alimenta `History`/`DuckScope`; `close` ferma il timer, rimuove i listener e rilascia le view.
- [ ] **Step 2: Sync.** Time Sync e Hold Sync attenuano la manopola in millisecondi (`setDimmed`) e attivano le note, e viceversa, a ogni cambio del parametro (anche da automazione).
- [ ] **Step 3: Soglia sul display:** `setThreshold` aggiornato quando cambia `kThreshold`.
- [ ] **Step 4: Build e controllo dei simboli.**
  Run: `cmake --build build --target pillow_talk pillow_talk_tests pillow_talk_factory_test -j2 && ctest --test-dir build -R pillow_talk --output-on-failure && nm -D --undefined-only "dist/Pillow Talk.vst3/Contents/x86_64-linux/Pillow Talk.so" | c++filt | grep -ci steinberg`
  Expected: build senza errori, test PASS, 0 simboli `Steinberg::*` indefiniti.
- [ ] **Step 5: Verifica manuale in Reaper e Cubase** (a cura dell'utente): aprire e chiudere l'editor 50 volte; muovere ogni controllo e controllare l'automazione e l'annullamento; Sync acceso e spento; display con voce reale e con sidechain; finestra su schermo ad alta densità.
- [ ] **Step 6: Commit**

```bash
git add src/pillow-talk
git commit -m "feat(pillow-talk): add VSTGUI editor with ducking display"
```

---

### Task 6: Documentazione della skin e dell'editor

**Files:**
- Create: `src/pillow-talk/docs/UI_ASSETS.md`
- Modify: `src/pillow-talk/README.md`, `src/pillow-talk/docs/CONTROLS.md`, `README.md` radice, `docs/superpowers/plans/2026-10-09-the-stripper-completion.md` (Task 7: usare `shared/ui` al posto di `src/the-stripper/src/ui/`)

- [ ] **Step 1: Scrivere `UI_ASSETS.md`:** cartella `Resources` del bundle, nomi dei file riconosciuti da `Skin` (sfondo del pannello 480 × 560, filmstrip della manopola, immagine on/off dei pulsanti) e come si sostituiscono gli elementi uno alla volta; dichiarare che il formato definitivo viene fissato con le grafiche.
- [ ] **Step 2: Aggiornare README e CONTROLS** (l'editor c'è; il display e come legge i parametri di sola lettura) e il piano dello Stripper (riuso di `shared/ui`).
- [ ] **Step 3: Verifica finale**
  Run: `cmake --build build --target pillow_talk pillow_talk_tests fairy_ui_tests -j2 && ctest --test-dir build -R "pillow_talk|fairy_ui" --output-on-failure`
  Expected: PASS.
- [ ] **Step 4: Commit**

```bash
git add src/pillow-talk README.md docs
git commit -m "docs(pillow-talk): document the editor and skin assets"
```
