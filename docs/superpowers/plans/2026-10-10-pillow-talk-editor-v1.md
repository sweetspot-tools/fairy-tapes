# Pillow Talk — Editor v1 (grafica comune) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Sostituire l'editor verticale provvisorio con l'editor rack 1000 × 628 della preview, con il sistema di texture comune della suite, e chiudere una prima versione di Pillow Talk.

**Architecture:** Sfondi PNG pre-renderizzati dalla preview (strati statici) più un manifest; il plugin sceglie la variante dal seme salvato nello stato. Controlli, LED, display, meter, valori, scritte sul nastro e viti sono disegnati dal codice con `shared/ui`. Il processor pubblica in più i picchi di ingresso e uscita e i clip.

**Tech Stack:** C++20, VST3 SDK + VSTGUI4, `shared/ui`; Python 3 (`Pillow`, `numpy`, `fontTools`) e Google Chrome headless per gli strumenti di generazione; CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-10-09-pillow-talk-gui-design.md` (§12 e §10-11). Preview di riferimento: `src/pillow-talk/docs/mockups/pillow_rack.html`. Linguaggio grafico: `docs/DESIGN_SYSTEM.md`.

## Global Constraints

- Lavoro nel worktree `/srv/http/fairy-tapes-wt-pillow`, branch `feat/pillow-talk`; non toccare il checkout principale (lo usa la chat dello Stripper). Push solo quando l'utente lo chiede. Stage dei file per nome.
- Build in cartella scratch fuori dal repo, `-DCMAKE_BUILD_TYPE=Release -DGPU_NAM_DISABLE_CUDA=ON`, `-j2`, solo i target necessari; i test restano verdi prima e dopo; il nuovo codice nasce con un test che prima fallisce.
- Tutto il testo visibile nell'editor è in inglese; documentazione e spec in italiano; identificatori, commenti e commit in inglese.
- Materiali e texture come The Stripper (alluminio tornito nero anodizzato, sfondo `#161617` a grana fine); nessun nome o marchio delle fonti d'ispirazione nella UI.
- Riusare `shared/ui` (controlli, `MeterBar`, `ClipLed`, `meter_math.h`, `Skin`); ciò che serve a entrambi i plugin si aggiunge lì. Non modificare file di The Stripper.
- Dimensione dell'editor 1000 × 628, host-scalata; il processor non alloca e non attende; parametri di display in sola lettura da `kMeterBase`, fuori da `kNumParams`.
- I valori che arrivano dall'host o da file (stato, preferenze) sono sempre sanificati; un file mancante o corrotto non blocca mai l'editor.
- Licenze: i tre font del nastro (Zeyada, Rock Salt, Homemade Apple) e VSTGUI/VST3 SDK nei Credits.

## Review Focus

- Stato v1 (senza seme, viti, nastri) si carica con valori di default; stato con seme assente o a 0 genera un seme nuovo e lo salva.
- PNG o manifest mancanti o malformati: l'editor si apre con sfondo a tinta unita e senza usura, senza crash né log in eccesso.
- File delle preferenze mancante, vuoto o corrotto: valori di default; scrittura non riuscita: ignorata.
- Testo del nastro molto lungo o con caratteri non previsti: troncato e filtrato, mai fuori dal nastro.
- Cambio di Wear con l'editor aperto: lo sfondo cambia senza ricostruire la finestra; il flip con un campo di testo aperto lo chiude confermando.
- Apertura e chiusura dell'editor 50 volte: nessuna perdita, nessun listener rimasto; HiDPI con fattore non esatto.

---

### Task 1: Dati del blocco LEVEL dal processor

**Files:**
- Modify: `src/pillow-talk/src/params.h`, `src/pillow-talk/src/engine.{h,cpp}`, `src/pillow-talk/src/processor.{h,cpp}`, `src/pillow-talk/src/controller.{h,cpp}`, `src/pillow-talk/tests/test_pillow_talk.cpp`

**Interfaces:**
- Produces: in `params.h` nuovi `DisplayID` dopo `kDispEcho`: `kDispInL, kDispInR, kDispOutL, kDispOutR, kDispClipIn, kDispClipOut, kClearClips` (tutti in sola lettura e nascosti, tranne `kClearClips` scrivibile e nascosto); picchi normalizzati con `meterNormFromDb(db, -60, 0)`, clip 0/1. In `Engine`: `struct LevelValues { float inL, inR, outL, outR; bool clipIn, clipOut; }; LevelValues lastLevels() const; void clearClips();` (ingresso misurato prima di tutto, uscita dopo Mix e Bypass, clip se |x| ≥ 1.0 con hold di 2 s di tempo audio).
- Riusa `DisplayAccumulator` e `DisplayRateLimiter` (massimo tra due scritture da 16 ms).

- [ ] **Step 1: Test:** `test_levels_silence_is_floor`; `test_levels_in_out_peaks_follow_signal` (sinusoide -12 dBFS entro ±1.5 dB, canali diversi danno valori diversi); `test_levels_out_after_mix_and_bypass` (Mix 0 e Bypass portano l'uscita a seguire l'ingresso; l'uscita non include l'eco con Mix 0); `test_clip_at_exactly_one_and_hold_2s`; `test_clip_clear`; `test_level_ids_do_not_collide_and_not_in_state`; `test_levels_accumulator_holds_peaks`.
- [ ] **Step 2: Eseguire, verificare il fallimento.**
- [ ] **Step 3: Implementare** in `Engine`/`Processor`/`Controller` (registrazione read-only/hidden; `kClearClips` scrivibile e azzerato dal processor), senza allocazioni.
- [ ] **Step 4: Eseguire i test del plugin.** Expected: PASS.
- [ ] **Step 5: Commit** `feat(pillow-talk): publish input/output peaks and clip flags`.

---

### Task 2: Generatore degli sfondi PNG

**Files:**
- Modify: `src/pillow-talk/docs/mockups/pillow_rack.html` (modalità `?bg=1`: solo strati statici, senza controlli, valori, display, LED, scritte sul nastro e viti)
- Create: `src/pillow-talk/tools/render_backgrounds.py`, `src/pillow-talk/tools/test_render_backgrounds.py`, `src/pillow-talk/assets/` (PNG e `manifest.json` generati e committati)

**Interfaces:**
- Produces: `render_backgrounds.py [--out DIR] [--chrome PATH]` che, per ogni variante `(wear, seed)` di una lista fissa (Off ×1, Light ×2, Heavy ×2), apre la preview con `?bg=1&wear=<w>&seed=<n>&scale=<1|2>` in Chrome headless e salva `panel_<wear>_<n>.png` (1000 × 628) e `panel_<wear>_<n>@2x.png` (2000 × 1256); scrive `manifest.json` con, per variante, i nastri (`x, y, w, h, rot`) e le posizioni delle viti. Il PNG ha sfondo opaco e angoli arrotondati trasparenti.

- [ ] **Step 1: Test** (`pytest`, senza Chrome dove possibile): il manifest ha le varianti attese; ogni nastro sta dentro il pannello; stesso seme → stesso manifest; il PNG ha le dimensioni attese e gli angoli trasparenti (se i PNG esistono); un controllo che nessun PNG superi un limite di peso concordato (da misurare in questo task e riportare nella spec).
- [ ] **Step 2: Eseguire, verificare il fallimento.**
- [ ] **Step 3: Implementare** la modalità `?bg=1` nella preview (parametri dall'URL; strati dei controlli nascosti; nastri senza testo e senza gestore di clic; viti non disegnate) e `render_backgrounds.py` (Chrome headless `--screenshot`, ritaglio e trasparenza con Pillow).
- [ ] **Step 4: Generare gli asset** e controllarli a occhio (un PNG per livello di usura); riportare numero e peso totale nella spec §12 e fermarsi a segnalare se il totale supera 15 MB.
- [ ] **Step 5: Commit** `feat(pillow-talk): background renderer and generated panel variants`.

---

### Task 3: Controlli in alluminio in `shared/ui`

**Files:**
- Modify (se serve): `shared/ui/include/fairy_ui/controls.h`, `shared/ui/src/controls.cpp`, `shared/ui/tests/`

**Interfaces:**
- Produces: manopole e pulsanti con l'aspetto in alluminio nero della preview (corpo `#232427`, anello, indice crema `#d4d1c5`, pulsanti grigi `#9a9ca1`), usabili dai due plugin; usura dei controlli seguita dal livello Wear.

- [ ] **Step 1: Verificare cosa c'è già** in `origin/pre-release` (`git log --oneline -- shared/ui`, e leggere `controls.cpp`): se la chat dello Stripper ha già pubblicato l'aspetto in alluminio, riusarlo e chiudere il task annotandolo nel registro; altrimenti continuare.
- [ ] **Step 2: Test** dei soli calcoli puri (angoli, raggi, colori dall'usura) in `shared/ui/tests`.
- [ ] **Step 3: Eseguire, verificare il fallimento; implementare** il disegno in alluminio in `AnalogKnob`/`ToggleButton` mantenendo l'API (nessun cambio per The Stripper).
- [ ] **Step 4: Eseguire i test di `shared/ui` e `pillow_talk_tests`.** Expected: PASS. Controllo visivo con un render offscreen non committato.
- [ ] **Step 5: Commit** `feat(ui): aluminium look for shared knobs and buttons`.

---

### Task 4: Editor orizzontale con sfondo PNG

**Files:**
- Modify: `src/pillow-talk/src/ui/editor.{h,cpp}`, `src/pillow-talk/src/ui/panel_view.{h,cpp}`, `src/pillow-talk/CMakeLists.txt` (copia degli asset nel bundle)
- Create: `src/pillow-talk/src/ui/layout.h`, `src/pillow-talk/src/ui/background.{h,cpp}`, `src/pillow-talk/tests/test_layout.cpp`

**Interfaces:**
- Produces: `layout.h` con la tabella dati dei controlli alle posizioni della preview (riquadri DUCKING 24,92 / LEVEL / OUTPUT / DELAY / AUTO DUCK, manopole e Sync/note) con test su: ogni parametro persistente ha esattamente un controllo, nessun rettangolo esce dal pannello 1000 × 628 né si sovrappone ad un altro; `background.h`: `struct Variant { std::string png; std::vector<Tape> tapes; std::vector<Screw> screws; }; std::optional<Manifest> loadManifest(const std::filesystem::path&)` e `size_t pickVariant(uint32_t seed, Wear wear, const Manifest&)` (deterministica).
- L'editor passa a 1000 × 628; `DuckScope` si ridimensiona al riquadro (376 × 130); LEVEL usa `MeterBar`/`ClipLed`; Sync/note con dimming come oggi; Bypass in alto a destra del riquadro OUTPUT.

- [ ] **Step 1: Test** per `layout.h` e `pickVariant`/`loadManifest` (manifest valido, malformato, mancante; seme 0; livello senza varianti).
- [ ] **Step 2: Eseguire, verificare il fallimento.**
- [ ] **Step 3: Implementare** layout, caricamento dello sfondo (tinta unita `#161617` se manca) e la nuova `Editor`; aggiornare `checkSizeConstraint`/HiDPI alla nuova dimensione.
- [ ] **Step 4: Build e test.**
  Run: `cmake --build <scratch> --target pillow_talk pillow_talk_tests pillow_talk_factory_test -j2 && ctest --test-dir <scratch> -R pillow_talk --output-on-failure`
  Expected: PASS, 0 simboli `Steinberg::*` indefiniti.
- [ ] **Step 5: Commit** `feat(pillow-talk): landscape rack editor with PNG background`.

---

### Task 5: Stato v2, seme e preferenze

**Files:**
- Modify: `src/pillow-talk/src/state.h`, `src/pillow-talk/src/processor.{h,cpp}`, `src/pillow-talk/src/controller.{h,cpp}`, `src/pillow-talk/tests/test_pillow_talk.cpp`
- Create: `src/pillow-talk/src/ui/prefs.{h,cpp}`, `src/pillow-talk/tests/test_prefs.cpp`

**Interfaces:**
- Produces: `PluginState { double params[kNumParams]; uint32_t seed; float screwAngles[4]; std::vector<std::string> tapeTexts; }` (`kStateVersion = 2`; lettura v1 con default; testi filtrati e al massimo 14 caratteri); `struct Prefs { Wear wear; bool notes; TapeFont tapeFont; bool handwrittenValues; }` con `Prefs loadPrefs(const std::filesystem::path&)`, `bool savePrefs(const std::filesystem::path&, const Prefs&)` (file JSON nella cartella delle preferenze dell'utente; difetti sanificati) e `std::filesystem::path prefsPath()`.

- [ ] **Step 1: Test:** round-trip dello stato v2; stato v1 caricato; seme 0 → seme nuovo salvato; valori non finiti e testi invalidi sanificati; `loadPrefs` con file assente, vuoto, JSON non valido e campi mancanti; `savePrefs` su percorso non scrivibile ritorna `false`.
- [ ] **Step 2: Eseguire, verificare il fallimento.**
- [ ] **Step 3: Implementare** stato e preferenze; il controller espone seme, angoli e testi all'editor e ne riceve le modifiche (messaggi `IMessage`, come per il modello nello Stripper) senza toccare il thread audio.
- [ ] **Step 4: Eseguire i test.** Expected: PASS.
- [ ] **Step 5: Commit** `feat(pillow-talk): state v2 with seed, screw angles and tape texts; user preferences`.

---

### Task 6: Nastro con scritte a mano

**Files:**
- Create: `src/pillow-talk/tools/gen_tape_fonts.py`, `src/pillow-talk/tools/test_gen_tape_fonts.py`, `src/pillow-talk/src/ui/tape_fonts.h` (generato e committato), `src/pillow-talk/src/ui/tape_view.{h,cpp}`, `src/pillow-talk/tests/test_tape_text.cpp`
- Modify: `src/pillow-talk/src/ui/editor.cpp`

**Interfaces:**
- Produces: `tape_fonts.h` con i contorni vettoriali dei glifi ASCII stampabili dei tre font (curve quadratiche e cubiche, `advance`, `unitsPerEm`); `tape_view.h`: `class TapeView : public VSTGUI::CView` (un nastro del manifest: disegna il testo con il font scelto e piccola variazione per lettera dal seme; un clic apre un campo di testo da 14 caratteri al massimo, Invio conferma, Esc annulla) e funzioni pure `std::string sanitizeTapeText(std::string)` e `float fitScale(float textWidth, float tapeWidth)`.

- [ ] **Step 1: Test:** `gen_tape_fonts` produce l'header deterministicamente e ogni glifo ASCII stampabile esiste; `sanitizeTapeText` (lunghezza 14, caratteri non stampabili rimossi, spazi ai bordi tolti); `fitScale` non supera mai il nastro.
- [ ] **Step 2: Eseguire, verificare il fallimento.**
- [ ] **Step 3: Implementare** lo script (sorgenti dei font da percorso indicato, licenze riportate in testa all'header) e `TapeView`; i testi vanno nello stato (Task 5).
- [ ] **Step 4: Eseguire i test e la build.** Expected: PASS.
- [ ] **Step 5: Commit** `feat(pillow-talk): tape handwriting with vector fonts`.

---

### Task 7: Viti ruotabili

**Files:**
- Create: `src/pillow-talk/src/ui/screw_view.{h,cpp}`, `src/pillow-talk/tests/test_screw_math.cpp`
- Modify: `src/pillow-talk/src/ui/editor.cpp`

**Interfaces:**
- Produces: `class ScrewView : public VSTGUI::CView` (taglio o croce, angolo iniziale dal seme, una può mancare con bassa probabilità, trascinamento che ruota; l'angolo si salva nello stato) e funzioni pure `float angleFromDrag(...)`, `ScrewKind kindFromSeed(uint32_t seed, int index)`, `bool missingFromSeed(uint32_t seed, int index)` (al più una vite mancante).

- [ ] **Step 1: Test** delle funzioni pure (deterministiche, una sola vite mancante al massimo, angolo normalizzato).
- [ ] **Step 2: Eseguire, verificare il fallimento; implementare** `ScrewView` e collegarla alle posizioni del manifest.
- [ ] **Step 3: Eseguire i test e la build.** Expected: PASS.
- [ ] **Step 4: Commit** `feat(pillow-talk): rotatable screws`.

---

### Task 8: Retro con Settings e Credits

**Files:**
- Create: `src/pillow-talk/src/ui/back_view.{h,cpp}`, `src/pillow-talk/src/ui/credits.h`, `src/pillow-talk/tests/test_credits.cpp`
- Modify: `src/pillow-talk/src/ui/editor.{h,cpp}`

**Interfaces:**
- Produces: la voce "BACK →" nell'intestazione; `BackView` con le schede SETTINGS (Appearance: Wear, Notes, Tape font, Values font, Look/Re-roll; Behaviour: Display window, Display peaks, Tempo fallback, come nella preview) e CREDITS (versione, GPL-3.0, componenti e licenze: VST 3 SDK, VSTGUI, i tre font, avvisi di legge); animazione di rotazione del pannello di circa 0,8 s (restringimento orizzontale con prospettiva, non un vero 3D) e ritorno con "← BACK"; `credits.h` con l'elenco dati (nome, autore, licenza). Le preferenze di aspetto vanno nel file del Task 5, quelle di comportamento nello stato.

- [ ] **Step 1: Test:** ogni voce di `credits.h` ha nome, autore e licenza non vuoti; i tre font e VSTGUI/VST3 SDK sono presenti.
- [ ] **Step 2: Eseguire, verificare il fallimento; implementare** `BackView`, collegare le preferenze (Wear cambia lo sfondo con la variante corrispondente allo stesso seme) e il comando Re-roll (nuovo seme, angoli delle viti e testi dei nastri azzerati).
- [ ] **Step 3: Eseguire i test e la build.** Expected: PASS.
- [ ] **Step 4: Commit** `feat(pillow-talk): back panel with settings and credits`.

---

### Task 9: Documentazione e verifica

**Files:**
- Modify: `src/pillow-talk/README.md`, `src/pillow-talk/docs/CONTROLS.md`, `src/pillow-talk/docs/UI_ASSETS.md`, `README.md` radice, spec §10-12 (stato dei compiti e decisioni), questo piano (compiti spuntati)

- [ ] **Step 1: Aggiornare i docs** al codice reale: layout, meter e LED di clip, Wear, nastro e viti, retro, preferenze (dove vivono), asset e come rigenerarli (`render_backgrounds.py`, `gen_tape_fonts.py`), dipendenze di sistema per Linux.
- [ ] **Step 2: Verifica finale**
  Run: `cmake --build <scratch> --target pillow_talk pillow_talk_tests pillow_talk_factory_test fairy_ui_tests -j2 && ctest --test-dir <scratch> -R "pillow_talk|fairy_ui" --output-on-failure`
  Expected: PASS.
- [ ] **Step 3: Verifica manuale in Reaper e Cubase** (a cura dell'utente): aspetto confrontato con la preview, apertura e chiusura 50 volte, automazione e undo, Sync, meter e clip con segnale vero, Wear Off/Light/Heavy, scrivere sul nastro, ruotare le viti, flip del retro, preferenze che persistono, HiDPI.
- [ ] **Step 4: Commit** `docs(pillow-talk): editor v1 documentation`.
