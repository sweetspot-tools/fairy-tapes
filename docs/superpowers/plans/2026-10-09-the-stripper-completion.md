# The Stripper — Completamento: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Aggiungere a The Stripper la scelta del modello NAM, un editor VSTGUI disegnato dal codice, i parametri di limiter e gate, la conformità SSL 360 Link e i docs allineati.

**Architecture:** `NamDrive` si estrae in `shared/nam_drive` e riceve un caricamento asincrono (thread di lavoro + scambio atomico). Il controller invia al processor il percorso del `.nam` con un `IMessage`. L'editor VSTGUI è costruito in codice, con controlli disegnati da `CDrawContext` e una tessera di grana PNG prodotta da uno script.

**Tech Stack:** C++20, VST3 SDK + VSTGUI4 (`vendors/vst3sdk/vstgui4`), NeuralAmpModelerCore, CUDA opzionale (`shared/gpu_nam`), Python 3 (script della tessera di grana), CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-10-09-the-stripper-completion-design.md`

## Stato dei compiti (2026-10-10)

- [x] Task 1 — `NamDrive` in `shared/nam_drive` (commit `5805520`, adattato a NAM v0.6.0 in `10cb131`; 49 test su 49; CUDA non verificato)
- [x] Task 2 — caricamento asincrono (`requestLoad`, `LoadState`; 5 test, 25 ripetizioni senza errori). Trovato e corretto un difetto del Task 1: i registratori delle architetture NAM sono inizializzatori statici e il linker li scartava dalla libreria statica; i consumatori usano ora `nam_drive_link(<target>)` (collegamento whole-archive)
- [x] Task 3 — `kLimiterPound` e `kGateThreshold` (ora usati dal processor), stato v8 (parametri + percorso del modello; `state.h` header-only, v1-v8 leggibili), tabella dei parametri bloccata (`tests/param_table_golden.h`: ID verificati a compilazione, default a runtime; mutazioni di prova rilevate). **Resta per il Task 14/§6:** seme, viti, scritte sul nastro, segni di richiamo e impostazioni di comportamento nello stato; nomi e numero di passi non sono nella tabella bloccata (solo ID e default)
- [x] Task 4 — meter (picchi in/out, 6 LED di clip per stadio, gain reduction compressore e limiter; parametri di sola lettura da `kMeterBase = 1000`, scritti al massimo ogni 16 ms; `kClearClips` scrivibile). Il guadagno del compressore è letto dai motori (`currentGainReductionDb()`: il rapporto dei picchi non funziona con FET e Bus, che hanno guadagno in ingresso), quello del limiter dal rapporto dei picchi (BlockParty non lo espone). Test: unit (`test_meter.cpp`) e test nel modulo vero con code di parametri simulate (`test_processor_meters.cpp`)
- [x] Task 5 — messaggi del modello (`messages.h`: `LoadModel` controller → processor, `ModelStatus` processor → controller; stringhe in UTF-8 binario; il processor ripristina il modello dallo stato; `Controller::requestModelLoad/modelName/modelError/modelState`; callback di fine caricamento di `NamDrive`; `requestLoad` con percorso vuoto = nessun modello). Test: messaggi con oggetti finti, `nam_drive` (vuoto e callback) e il modulo vero con host, peer e stream finti. **Da verificare in Reaper/Cubase:** il messaggio di stato parte dal thread di caricamento (non dal thread audio)
- [x] Task 6 — tessera di grana (`tools/gen_panel_grain.py` con NumPy/Pillow, 5 test pytest: dimensioni, determinismo per seme, quasi invisibile (alfa max 0,07, media ≤ 0,04), senza giunture; asset `assets/panel_grain.png` seme 4 copiato in `Contents/Resources` del bundle dal CMake; l'editor la usa già se presente). Comune a tutti i plugin dal 2026-10-10
- [x] Task 7 — VSTGUI e controlli: già fatti in `shared/ui` dalla chat di Pillow Talk (VSTGUI nella build e nella CI, `knob_math`, `AnalogKnob`/`ToggleButton`/`ChoiceButtons`, `Skin`); aggiunti `meter_math.h` (LED accesi, colori, peak hold), `MeterBar` e `ClipLed` (`shared/ui`), collegata la libreria a The Stripper. **Resta da fare (Task 8 e 11):** l'aspetto dei controlli condivisi è ancora quello vecchio (arco ambra, disco piatto); va portato al design approvato (scala di puntini, valori di fondo scala, alluminio tornito, usura)
- [x] Task 8 — editor (completato 2026-10-21, tranne la verifica manuale in un host, Step 6, a cura dell'utente, e la scheda TONE3000, spec a parte `2026-10-21-tone3000-integration-design.md`) — (suddiviso in 8a layout/binding, 8b fascia meter, 8c striscia del modello e popup locale). **8a fatto (2026-10-10):** `ui/layout.h` (tabella di layout, polarità IN, regole di attenuazione, test `the_stripper_layout_tests`), `ui/editor.{h,cpp}`, `ui/panel_view.{h,cpp}` (tinta, gradiente, riquadri con interruzione per il titolo, intestazione, testate delle bande EQ), `shared/ui/editor_size.h` (+ test). Il controller ha `beginEdit/performEdit/endEdit`; `setParamNormalized` è solo host → controller (prima rimandava le modifiche all'host: un anello che poteva sovrascrivere l'automazione registrata). L'editor legge i valori dal controller con un timer da 30 ms (nessun listener sui parametri). Default di `kEqHPFOn`/`kEqLPFOn` = 1 (tabella bloccata aggiornata). Il plugin ora è un `MODULE` e usa `InitModule`/`DeinitModule` dell'SDK (servono a `VSTGUI::init`). **Modifica del 2026-10-14:** prima riga di manopole 8 px più in basso, CHAN/BUS 14 px più in basso; icona GPU dentro il pulsante NAM (solo con GPU attiva, Task 15); preview aggiornata; un solo pulsante IN per i due filtri (`kBypassLPHP`, `Module::Filters`), `kEqHPFOn`/`kEqLPFOn` senza controllo. **8b fatto:** fascia dei meter nel riquadro LEVEL (6 `MeterBar`, 6 `ClipLed`, clic su un LED → `kClearClips`, attenuazione col Bypass globale tranne IN/OUT e i meter di livello; geometria in `ui/layout.h`, test inclusi). La scala dB stampata ha 8 etichette (−60 … 0, una per LED al massimo) invece di 12: quelle vicine a 0 si sovrapponevano; aggiornata anche la preview. **8c fatto (parte locale):** `ui/model_source.h` (scansione ricorsiva delle cartelle, filtro, nome senza `.nam`, troncamento per larghezza misurata; test `the_stripper_model_source_tests`), `ui/model_strip` (striscia di nastro cliccabile, "No model"/"Loading..."/errore in rosso; Drive attenuato senza modello), `ui/model_browser` + `ui/browser_widgets` (popup modale: cartelle con ADD via selettore di cartella nativo/REMOVE/rescan, ricerca con `CTextEdit`, elenco, LOAD, NO MODEL, CANCEL; scansione su thread). **Fatti dopo:** font del nastro (Task 12), suggerimento con il nome completo del modello sulla striscia (2026-10-21: `stripTooltip` in `model_source.h` con test; con NAM spento dice che lavora il 4K). **Resta:** scheda TONE3000 (spec a parte). Strumento di sviluppo `the_stripper_render` (`tools/render_editor.cpp`): disegna l'editor fuori schermo in un PNG, senza host
- [x] Task 9 — ricerca SSL 360 Link e docs (2026-10-10). Trovato (documentazione SSL): nessun SDK né certificazione; solo VST3, stereo; i meter di gain reduction passano da parametri VST3 in sola lettura con codifica 0 = nessuna riduzione, dB = 20·log10(1 − valore). Di conseguenza `kMeterGrComp`/`kMeterGrLimiter` usano ora quella codifica (`grValueFromDb`, test unit e nel modulo) e sono visibili (non nascosti); l'editor li riconverte (`grBarNorm`). Docs allineati (FEATURES §4, CONTROLS, PARAMS_STATUS, DRIVE_STAGE_PARAMS, BUILD_AND_USE, UI_ASSETS_SPEC, README). **Da verificare con SSL 360 Link vero:** che i due parametri compaiano tra i dati di meter assegnabili; lane del gate non fatta (Denoise non espone la riduzione)
- [x] Task 10 — preferenze dell'utente (2026-10-14): `src/preferences.h` header-only (file `FairyTapes/TheStripper.prefs` nella cartella delle preferenze: `~/.config` o `$XDG_CONFIG_HOME` su Linux, `Library/Application Support` su macOS, `%APPDATA%` su Windows), formato `chiave=valore` in UTF-8, scrittura atomica (file temporaneo + rename), file mancante/corrotto/troppo grande → default, valori non validi → default per chiave, numeri limitati agli intervalli, elenco cartelle con tetto e voci ostili scartate. 12 test (`the_stripper_preferences_tests`, verificati con una mutazione). Il popup dei modelli legge e salva qui le cartelle. Le chiavi per usura, note, meter e font esistono già ma saranno usate dalle Settings del retro (Task 13) e dagli strati dell'editor (Task 11-12)
- [x] Task 11 — livelli e usura (2026-10-14; migrato al kit comune in `shared/ui` il 2026-10-14 secondo la nota della chat di Pillow Talk: `fairy_ui/wear.h`, `wear_view`, `draw_util`, `silkscreen` + `silkscreen_view`; lo Stripper mantiene l'adattatore `ui/silkscreen_items.h`, i valori scritti a mano passano da `SilkscreenView::setValueDrawer`; render dell'editor identici byte per byte prima e dopo, 4 casi): fatti il restyle dei controlli (`shared/ui`: `RackKnob`, `RackToggle`, `RackChoice`, `rack_geometry.h` con test; tappo in alluminio tornito, facce grigie con LED giallo per i selettori e rosso per gli IN, icona campana), lo strato di serigrafia (`ui/silkscreen_view`: scale a puntini e valori di fondo scala, nomi, valori gialli vivi, etichette dei pulsanti, sigle dei blocchi, tutto attenuato col modulo) e l'usura del pannello (`ui/wear.h` generatore deterministico con seme, 6 test inclusi i valori di riferimento di mulberry32; `ui/wear_view`: macchie, residui a pennello asciutto, graffi, puntini, sopra la serigrafia e sotto i controlli; livello Off/Light/Heavy dalle preferenze). **Usura dei controlli fatta** (`shared/ui/control_wear.h`, generatore deterministico con test, usato da `RackKnob`/`RackToggle`/`RackChoice`: tappi con sporco alla base, bordo scheggiato sul lato del dito, ammaccature, residui e puntini, intarsio del puntatore consumato; pulsanti con sporco nello spazio attorno, angoli e bordo inferiore consumati, graffi, puntini; tutto al 25% di opacità, quantità secondo il livello). **Restano (fuori dal Task 11):** seme nello stato del plugin (Task 14: oggi ogni istanza ha lo stesso seme `kDefaultWearSeed`), eventuale cache bitmap dell'usura, Bell attenuato e scritta BUS in modalità Bus (proposte da confermare), pezzi di nastro (Task 12/14). Il generatore dell'usura condivide `fairy_ui/wear_rng.h` (mulberry32) con quello del pannello
- [x] Task 12 — nastro, scritte e font a tracciati (2026-10-14): `tools/gen_tape_fonts.py` (fontTools; test pytest) converte Zeyada, Rock Salt e Homemade Apple (ASCII stampabile + puntini di sospensione) in `src/ui/tape_glyphs.h` (generato, circa 840 KB), licenze e provenienza in `assets/fonts/` (OFL e Apache 2.0, hash dei sorgenti; i `.ttf` non sono nel repo); `ui/tape_font.h` (misura, normalizzazione delle cifre ×1,3, layout con variazione per lettera dal seme, troncamento per larghezza misurata; 8 test) e `ui/tape_text` (disegno con percorsi); il font del nastro e il font dei valori vengono dalle preferenze (default Zeyada, valori a mano); nome del modello e valori sotto le manopole scritti col font del nastro; **pezzi di nastro scrivibili** (`ui/tape_pieces.h`, `ui/tape_piece_view`: posizioni dal seme, 14 caratteri, testo predefinito o vuoto, clic → campo di testo `CTextEdit`, salvato nello stato). **Stato v10**: `wearSeed` (creato dal controller alla creazione dell'istanza) e i testi dei pezzi di nastro, nello stato del **controller** (lo stato del processor non li porta e `setComponentState` non li tocca); i progetti v9 si caricano. **Non verificato:** la scrittura sul nastro in un host vero (focus e tastiera di `CTextEdit` su X11/Windows/macOS) e il testo con caratteri non ASCII (non sono nei glifi: restano vuoti)
- [x] Task 13 — retro del pannello (2026-10-15): `ui/back_view` (schede SETTINGS, BUTTONS, CREDITS; si apre sull'ultima usata, salvata nelle preferenze con la chiave `backTab`), `ui/back_widgets` (selettori a segmenti, selettore del font del nastro scritto nel suo font, link di testo), `ui/back_panel.h` (dati puri, 9 test: crediti, aiuto dei pulsanti, a capo, opzioni delle impostazioni), `ui/flip_view` + `flip_pose.h` (il giro del pannello, 0,8 s, a due immagini che si stringono e si allargano: simulazione piatta del flip 3D; la posa ha un test). **Aiuto dei pulsanti generato dalla tabella di layout**: i titoli sono le etichette dei controlli e un pulsante della tabella senza spiegazione fa fallire il test (mutazione verificata). **Crediti**: licenze rilette sui file `LICENSE` dei sottomoduli il 2026-10-15 (Dusk Audio GPL-3.0, Airwindows MIT, NAM Core MIT, AudioDSPTools MIT, Eigen MPL-2.0, nlohmann/json MIT, SDK VST 3 MIT, VSTGUI BSD-3-Clause) più i tre font (OFL-1.1, Apache-2.0 ×2). **Impostazioni dell'aspetto** (usura, note, font del nastro, font dei valori, mantenimento e rilascio dei meter, RE-ROLL) applicate **dal vivo** e salvate nelle preferenze; il mantenimento e il rilascio comandano il marcatore di picco dei meter (`PeakHold::configure` in `shared/ui`, con test); RE-ROLL = nuovo seme (nuovi graffi, sporco, nastro) e testi del nastro azzerati. **Comportamento**: MODEL FOLDERS apre il popup dei modelli; NAM INFERENCE (serve la build GPU), RECALL MARKS e MARK NOW (Task 14) e TONE3000 sono mostrati attenuati finché non esistono. **Non verificato:** il giro del pannello e i clic sul retro in un host vero (visti solo i render delle tre schede e la posa del giro); le impostazioni del comportamento nello stato del progetto arrivano con il Task 14
- [x] Task 14 — viti, valori digitabili, richiamo (2026-10-15). **Stato v11** (nello stato del controller): angoli delle 4 viti, segni di richiamo (valori + validità) e modalità del richiamo (Auto/Manual); i progetti v10 si caricano; Auto = i segni sono i valori appena caricati, Manual = restano quelli salvati; test nello stato e nel modulo vero. **Viti** (`ui/screws.h` puro con 5 test, `ui/screw_view`): quattro viti negli angoli, a taglio o a croce, angolo e (6% dei pannelli) una mancante dal seme; si ruotano trascinando (destra/su in senso orario, 1,6°/px, come nella preview) e gli angoli si salvano; RE-ROLL le rigenera. **Valori digitabili** (`ui/value_text.h`, 4 test; `RackKnob::onValueClick`, `ValueEdit`): un clic sul valore giallo apre il campo; accetta "3", "+3.5 dB", "1.2k", "45 %" (virgola decimale, k = ×1000); il numero torna nell'intervallo invertendo il testo che il controller già mostra (bisezione, nessuna seconda copia delle mappe: preciso quanto il testo, per esempio 0,1 kHz); Invio o uscita dal campo conferma, Esc annulla; passa da begin/perform/end come una rotazione. **Segni di richiamo** (`ui/recall_marks.h` puro con test, `ui/recall_marks_view`): un tratto di penna blu sulla scala nella posizione del valore richiamato, solo mentre la manopola si è mossa di almeno il 2%; fa parte dello strato note (Hide li nasconde); nel retro RECALL MARKS (Auto/Manual) e MARK NOW sono attivi. **Scelte confermate il 2026-10-15 (spec §11):** soglia del 2%, aspetto del tratto, niente scotch sui valori; **Alt/Ctrl + doppio clic riporta al valore di richiamo (fatto 2026-10-15, solo se il segno è visibile)**. **Non fatto:** NAM INFERENCE (serve la build GPU: resta attenuato e non è nello stato). **Non verificato in un host:** trascinare le viti, il campo dei valori (focus e tastiera) e i clic
- [x] Task 15 — Drive NAM / 4K (2026-10-14, icona GPU nel pulsante NAM il 2026-10-21; dettagli nella sezione del Task 15).
- [x] Task 16 — meter del gate (dettagli nella sezione del Task 16).
- [x] Task 17 — adozione del kit comune di Pillow Talk (2026-10-21): cherry-pick dal suo branch `feat/pillow-talk` delle sole parti `shared/ui` (tessera di grana, font e generatore dei glifi, regole del testo, carta e disegno del nastro, viti). Lo Stripper ora usa `fairy_ui/screws.h` + `ScrewView` (`onTurned` a fine trascinamento), `tape_font.h` (con `truncateToWidth`), `tape_label.h` (`sanitizeTapeText`), `tape_paper.h` (`makeTapePaper`, stessa sequenza di numeri casuali), `tape_draw.h` (`drawTapePaper`, `drawTapeText`) e `shared/ui/assets/panel_grain.png`; restano suoi `TapePiece`/`generateTapePieces`, `TapePieceView`, `tapeFontIdOf` (`ui/tape_font_id.h`, `ui/tape_kit.h`) e `value_text.h`. Tolti `ui/screws.h`, `screw_view.*`, `tape_font.h`, `tape_glyphs.h`, `tape_text.*`, `tools/gen_tape_fonts.py` e il suo test, `assets/fonts/`, `assets/panel_grain.png`; i test dei font e delle viti vivono in `shared/ui`. **Verifica: 19 render prima e dopo (16 semi, retro Settings e Credits, segni di richiamo) identici byte per byte; 32 test verdi.** **Cambia una regola:** il testo sul nastro è ora quello comune (solo ASCII stampabile, al più 14 caratteri, spazi ai bordi tolti) invece di `clampTapeText` (che teneva l'UTF-8); i font disegnano comunque solo ASCII. I commit di Pillow Talk restano nel suo branch finché non lo mergia: al merge git riconosce le patch identiche.
- [x] Task 18 — scheda TONE3000 del popup dei modelli (2026-10-21), sul client comune `shared/tone3000` (spec `2026-10-21-tone3000-integration-design.md`, piano di massima §11): `ui/tone3000_tab` + `ui/tone3000_view.h` (testi puri, test `the_stripper_tone3000_view_tests`), schede LOCAL/TONE3000 in `ModelBrowser`, `Controller::tone3000Actions()` + `Tone3000Service` (client creato alla prima apertura, `FAIRY_HAS_TONE3000` solo con OpenSSL), `tone3000_config.h` con `kTone3000ClientId` **vuoto**: finché l'app non è registrata la scheda dice che TONE3000 non è configurato e non parte nessuna richiesta. CREDITS con libcurl e OpenSSL, avviso "Powered by TONE3000", pulsante TONE3000 → OPEN... nel retro, anteprima aggiornata, `the_stripper_render ... tone3000=<stato>` per vedere gli stati con dati inventati. **Non verificato:** tutto il flusso vero (serve l'app registrata e un account), l'apertura del browser su Windows e macOS, il ritorno su loopback, lo scarico di un modello vero.

## Global Constraints

- Fuori scopo: gain staging (i suoi due parametri restano inerti), integrazione Tone3000 (spec a parte, spec §10).
- Tutto il testo visibile nell'editor è in inglese.
- `NamDrive` in `shared/nam_drive`, namespace `FairyTapes::Shared::DSP`; il comportamento audio non cambia.
- Collegare `nam_drive` con `nam_drive_link(<target>)`, non con `target_link_libraries`: serve il collegamento whole-archive (vedi Task 2).
- Il thread audio non alloca e non attende mai; il modello sostituito si rilascia fuori dal thread audio.
- I nuovi `ParamID` (`kLimiterPound`, `kGateThreshold`) si aggiungono **in coda**, prima di `kNumParams`: gli ID esistenti non cambiano.
- `kStateVersion` passa da 7 a 8 (aggiunge il percorso del modello); lo stato v7 resta caricabile.
- GUI: 1000 × 628 (layout e stile come `src/the-stripper/docs/mockups/stripper_rack.html`), vettoriale, nessun asset 2x; ogni controllo ha un `ParamID`; gli asset PNG manuali sono opzionali. I pulsanti IN dei moduli invertono solo nell'interfaccia i parametri `kBypass*` (ID e polarità invariati); default di `kEqHPFOn`/`kEqLPFOn` = 1.
- Texture: una sola tessera di grana finissima (spec §4, Sfondo), da `src/the-stripper/tools/gen_panel_grain.py` in `src/the-stripper/assets/panel_grain.png`; nessuna variante nello stato.
- Licenza GPL-3.0; nessun nome o branding delle fonti nella UI, salvo le sigle serigrafate dei blocchi (spec §11, 2026-10-14: `4K-G`/`4K-E`, `OPTO-2A`, `FET-76`, `VCA-160`, `NAM`, `B-PARTY`, `D-NOISE`).

## Review Focus

- Aprire e chiudere l'editor 50 volte di fila (anche con la finestra ridimensionata dall'host): nessun crash, nessun listener sui parametri rimasto agganciato.
- `process()` in esecuzione mentre `requestLoad` completa: nessuna allocazione, nessun glitch udibile (uscita senza NaN e continua).
- File `.nam` inesistente, vuoto o di formato errato: Drive in passthrough, messaggio di errore, nessun crash.
- Stato v7 (senza percorso): si carica con percorso vuoto, parametri invariati.
- Cambio di Strip Mode con l'editor aperto: i controlli Voice/Drums/Classic compaiono e spariscono senza ricostruire la finestra.

---

### Task 1: Estrarre NamDrive in `shared/nam_drive`

**Files:**
- Create: `shared/nam_drive/CMakeLists.txt`
- Move: `src/the-stripper/src/dsp/nam_drive.{h,cpp}` → `shared/nam_drive/include/nam_drive.h`, `shared/nam_drive/src/nam_drive.cpp`
- Modify: `src/the-stripper/CMakeLists.txt`, `src/the-stripper/src/processor.{h,cpp}`, `src/the-stripper/tests/test_params.cpp` (include e namespace)

**Interfaces:**
- Produces: libreria CMake `nam_drive` (compila le `NAM_SOURCES`, dipende da `gpu_nam`; espone `include/` e le include dir di NAM come `PUBLIC`); classe `FairyTapes::Shared::DSP::NamDrive` con la stessa API di oggi.

- [x] **Step 1: Verificare lo stato di partenza**
  Run: `cmake -S . -B build && cmake --build build --target the_stripper_tests && ctest --test-dir build -R the_stripper --output-on-failure`
  Expected: PASS (baseline).
- [x] **Step 2: Spostare i file con `git mv`**, cambiare il namespace in `FairyTapes::Shared::DSP` e creare la libreria `nam_drive` (modellata su `shared/gpu_nam/CMakeLists.txt`). Spostare `NAM_SOURCES` e `NAM_INCLUDE_DIRS` dal CMake dello Stripper a quello della libreria.
- [x] **Step 3: Aggiornare lo Stripper** perché colleghi `nam_drive` (plugin e test) e usi il nuovo namespace in `processor.{h,cpp}` e nei test.
- [x] **Step 4: Ricompilare e rieseguire i test** (comando dello Step 1). Expected: PASS identico alla baseline.
- [x] **Step 5: Commit** (fatto: `5805520` e seguenti)

```bash
git add shared/nam_drive src/the-stripper
git commit -m "refactor: extract NamDrive to shared/nam_drive"
```

---

### Task 2: Caricamento asincrono del modello

**Files:**
- Modify: `shared/nam_drive/include/nam_drive.h`, `shared/nam_drive/src/nam_drive.cpp`
- Create: `shared/nam_drive/tests/test_nam_drive_async.cpp`, `shared/nam_drive/tests/fixtures/linear_unity.nam`
- Modify: `shared/nam_drive/CMakeLists.txt` (target `nam_drive_tests` + `add_test`)

**Interfaces:**
- Produces, in `NamDrive`:
  - `void requestLoad(const std::filesystem::path& path)` — dal thread dei messaggi; non blocca; una richiesta nuova rende obsoleta la precedente (vince l'ultima)
  - `enum class LoadState { Idle, Loading, Loaded, Failed }; LoadState loadState() const`
  - `std::string loadedModelName() const` — nome del file senza cartella, vuoto se nessun modello
  - `std::string lastError() const` — vuoto se `loadState() != Failed`
  - `process()` adotta il modello pronto all'inizio del blocco, senza allocare né attendere.

- [x] **Step 1: Creare la fixture** `linear_unity.nam`: modello NAM di architettura `Linear` a un coefficiente 1.0, sample rate 48000 (formato dal loader `Linear` in `vendors/NeuralAmpModelerCore/NAM/dsp.cpp`; verificare che `nam::get_dsp` lo accetti).
- [x] **Step 2: Scrivere i test** (eseguibile con le macro di `test_params.cpp`): `test_requestLoad_reaches_Loaded` (attesa max 5 s, `loadedModelName() == "linear_unity.nam"`); `test_requestLoad_missing_file_fails_and_passes_through` (stato `Failed`, `lastError()` non vuoto, `process` lascia il segnale invariato); `test_requestLoad_garbage_file_fails` (file di testo); `test_last_request_wins` (due richieste ravvicinate, la seconda su un file inesistente → `Failed`, non `Loaded`); `test_process_during_load_is_finite` (un thread chiama `process` in loop su rumore mentre arriva `requestLoad`; nessun NaN, nessun valore oltre ±10).
- [x] **Step 3: Eseguire, verificare il fallimento** (API mancante).
  Run: `cmake --build build --target nam_drive_tests`
  Expected: errore di compilazione.
- [x] **Step 4: Implementare l'API.** Thread di lavoro per richiesta (o un thread persistente con coda a un elemento), costruzione del modello fuori dal lock audio, scambio con `std::atomic<Model*>`; il thread audio fa `exchange` all'inizio di `process` e consegna il modello vecchio a una coda di rilascio svuotata dal thread di lavoro. `loadModel`/`unloadModel` sincroni restano per compatibilità.
- [x] **Step 5: Eseguire i test.**
  Run: `cmake --build build --target nam_drive_tests && ctest --test-dir build -R nam_drive --output-on-failure`
  Expected: PASS.
- [x] **Step 6: Commit**

```bash
git add shared/nam_drive
git commit -m "feat(nam-drive): add asynchronous model loading"
```

---

### Task 3: Nuovi parametri, stato v8 e blocco della tabella dei parametri

**Files:**
- Modify: `src/the-stripper/src/params.h`, `src/the-stripper/src/processor.{h,cpp}`, `src/the-stripper/src/controller.{h,cpp}`, `src/the-stripper/tests/test_params.cpp`
- Create: `src/the-stripper/tests/param_table_golden.h`

**Interfaces:**
- Produces:
  - In `params.h`: `kLimiterPound`, `kGateThreshold` appesi in coda a `ParamID` (default 0.5 e 0.0)
  - In `processor.h`: `struct PluginState { double params[kNumParams]; std::string modelPath; };` con `bool readState(Steinberg::IBStream*, PluginState&)` e `bool writeState(Steinberg::IBStream*, const PluginState&)` (funzioni libere testabili con un `IBStream` in memoria).
  - Il processor applica `kLimiterPound` a `BlockParty::setParams(pound, 1.0)` e `kGateThreshold` a `DeNoise::setParams(threshold, speed_attuale, 1.0)`.

- [x] **Step 1: Scrivere i test:** `test_new_params_appended_at_end` (`kLimiterPound == kNumParams - 2`, `kGateThreshold == kNumParams - 1`, e nessun ID precedente cambiato); `test_param_table_matches_golden` (tabella ID → nome → passi → default letta da `param_table_golden.h`, generata dallo stato attuale del codice **prima** dell'aggiunta e poi estesa con i due nuovi); `test_state_v8_roundtrip` (parametri, percorso con caratteri non ASCII); `test_state_v7_loads_with_defaults` (fixture v7 scritta con la vecchia struttura: percorso vuoto); `test_state_unknown_version_rejected`.
- [x] **Step 2: Generare `param_table_golden.h` dal codice attuale** (che include già `kDefaultEqHPFFreq = 0.0`, vedi spec §4, Default dei filtri) con un piccolo programma temporaneo, estenderlo a mano con i due nuovi parametri e verificare che il test sul nome/ordine fallisca se si scambiano due ID.
- [x] **Step 3: Eseguire, verificare il fallimento.**
- [x] **Step 4: Implementare** parametri, `readState`/`writeState` (versione 8; lettura v7 mantenuta), uso dei due parametri nel processor, titoli e unità nel controller.
- [x] **Step 5: Eseguire i test.**
  Run: `cmake --build build --target the_stripper_tests && ctest --test-dir build -R the_stripper --output-on-failure`
  Expected: PASS.
- [x] **Step 6: Commit**

```bash
git add src/the-stripper
git commit -m "feat(the-stripper): add limiter/gate params, state v8 and frozen param table"
```

---

### Task 4: Metering (picchi, clip per stadio, gain reduction)

**Files:**
- Create: `src/the-stripper/src/dsp/meter.h`
- Modify: `src/the-stripper/src/params.h` (intervallo `kMeterBase`), `src/the-stripper/src/processor.{h,cpp}`, `src/the-stripper/src/controller.{h,cpp}`, `src/the-stripper/src/dsp/multicomp_engines.{h,cpp}`, `src/the-stripper/src/dsp/blockparty.{h,cpp}`, `src/the-stripper/tests/test_params.cpp`

**Interfaces:**
- Produces:
  - In `params.h`: `constexpr Steinberg::Vst::ParamID kMeterBase = 1000;` e `enum MeterID : Steinberg::Vst::ParamID { kMeterInL = kMeterBase, kMeterInR, kMeterOutL, kMeterOutR, kMeterGrComp, kMeterGrLimiter, kClipInput, kClipEq, kClipComp, kClipDrive, kClipLimiter, kClipOutput, kClearClips, kNumMeterIDs };` (valori normalizzati 0..1; tutti in sola lettura tranne `kClearClips`, scrivibile e nascosto: un clic su un LED lo imposta a 1 e il processor azzera i `ClipDetector` e lo riporta a 0; picchi: -60..+6 dBFS, GR comp: 0..-20 dB, GR limiter: 0..-12 dB, clip: 0/1)
  - In `dsp/meter.h`: `class PeakMeter { void prepare(double sampleRate); float process(const float* x, int numSamples); /* picco del blocco in lineare */ float levelDb() const; /* con rilascio 20 dB/s */ float holdDb() const; /* picco mantenuto 1.5 s */ void reset(); };` e `class ClipDetector { void prepare(double sampleRate); void process(const float* x, int numSamples); bool lit() const; /* acceso fino a 2 s dopo |x| >= 1.0 */ void clear(); };`
  - `float currentGainReductionDb() const` (>= 0) su `MultiCompOpto/FET/VCA/Bus` e `CompressionStub` (dall'inviluppo del motore); per `BlockParty` (non lo espone) dal rapporto tra picchi di uscita e ingresso del blocco (`GainReductionMeter::update`).

- [x] **Step 1: Scrivere i test:** `test_peakmeter_instant_attack_and_release` (impulso a 0.5 → `levelDb ≈ -6` subito; dopo 1 s di silenzio sceso di 20 ± 1 dB); `test_peakmeter_hold_1_5s` (`holdDb` resta al picco per 1.4 s e cade dopo 1.6 s); `test_clip_detects_at_exactly_one` (0.9999 → spento; 1.0 → acceso); `test_clip_hold_2s_and_clear` (acceso a 1.9 s, spento a 2.1 s; `clear()` lo spegne subito); `test_gr_zero_when_not_compressing` e `test_gr_positive_when_compressing` per ciascun motore (rumore a -30 dBFS e a -6 dBFS con Amount 0.8); `test_meter_ids_do_not_collide_with_params` (`kMeterBase >= kNumParams`, intervalli disgiunti); `test_meters_not_in_state` (`writeState` non cambia di dimensione aggiungendo i meter); `test_meter_update_rate_limit` (con blocchi da 64 campioni a 48 kHz, al massimo una scrittura ogni 16 ms).
- [x] **Step 2: Eseguire, verificare il fallimento.**
- [x] **Step 3: Implementare `PeakMeter`, `ClipDetector` e `currentGainReductionDb()`** sui motori.
- [x] **Step 4: Integrare nel `Processor`:** un `ClipDetector` per stadio (misurato all'uscita di Input gain, EQ, Compression, Drive, Limiter e Output gain) e due `PeakMeter` stereo (ingresso prima di tutto, uscita dopo l'Output gain); a fine blocco, se sono passati almeno 16 ms, scrivere i valori normalizzati in `outputParameterChanges` come parametri di sola lettura. Gli stadi in bypass non accendono il loro LED. Il `Controller` registra i parametri con `kIsReadOnly | kIsHidden`.
- [x] **Step 5: Eseguire i test.**
  Run: `cmake --build build --target the_stripper_tests && ctest --test-dir build -R the_stripper --output-on-failure`
  Expected: PASS.
- [x] **Step 6: Commit**

```bash
git add src/the-stripper
git commit -m "feat(the-stripper): add peak/clip/gain-reduction metering as read-only parameters"
```

---

### Task 5: Messaggi di caricamento del modello

**Files:**
- Modify: `src/the-stripper/src/processor.{h,cpp}`, `src/the-stripper/src/controller.{h,cpp}`, `src/the-stripper/src/cids.h` (identificatori dei messaggi), `src/the-stripper/tests/test_params.cpp`

**Interfaces:**
- Consumes: `NamDrive::requestLoad/loadState/loadedModelName/lastError` (Task 2), `PluginState` (Task 3).
- Produces: messaggi `IMessage` con id `"LoadModel"` (attributo stringa `path`, controller → processor) e `"ModelStatus"` (attributi `name` e `error`, processor → controller); il controller espone `std::string modelName() const`, `std::string modelError() const` e `void requestModelLoad(const std::string& path)`.

- [x] **Step 1: Scrivere i test** sulle parti pure: `test_model_message_roundtrip` (funzioni libere `encodeLoadMessage`/`decodeLoadMessage` su una classe `IMessage` di test); `test_state_restores_model_path_triggers_load` (dopo `setState` con percorso valido, `NamDrive` va in `Loading`/`Loaded`).
- [x] **Step 2: Eseguire, verificare il fallimento.**
- [x] **Step 3: Implementare.** Il processor gestisce `"LoadModel"` in `notify`, chiama `requestLoad`; ogni volta che lo stato cambia (controllato a fine `process`, senza allocazioni) invia `"ModelStatus"` tramite il canale di messaggi del host; `getState`/`setState` salvano e ricaricano il percorso. Il controller memorizza nome ed errore ricevuti e li passa all'editor.
- [x] **Step 4: Eseguire i test.** Expected: PASS.
- [x] **Step 5: Commit**

```bash
git add src/the-stripper
git commit -m "feat(the-stripper): load NAM model via controller/processor messages"
```

---

### Task 6: Tessera di grana del pannello

> Dal 2026-10-10 la grana fine è la texture di **tutti** i plugin (non solo The Stripper): lo script e l'asset restano in `src/the-stripper/` finché non nasce il kit comune in `shared/`.

**Files:**
- Create: `src/the-stripper/tools/gen_panel_grain.py`, `src/the-stripper/tools/test_gen_panel_grain.py`
- Create: `src/the-stripper/assets/panel_grain.png` (generato e committato)

**Interfaces:**
- Produces: `gen_panel_grain.py [--out FILE] [--seed N]` che scrive una tessera 256 × 256 RGBA ripetibile senza giunture visibili: pixel bianchi con alfa derivato da rumore illuminato (equivalente al filtro `tfine` della preview `stripper_rack.html`), alfa massimo circa 0,07, deterministica per un dato seed.

- [x] **Step 1: Scrivere il test** (`pytest`): dimensioni 256 × 256 RGBA; stesso seed → file identici (hash); seed diversi → file diversi; alfa massimo ≤ 0,10 e media ≤ 0,04 (grana quasi impercettibile); la tessera è ripetibile (differenza media tra bordo sinistro e destro, e tra alto e basso, non maggiore di quella tra colonne adiacenti interne).
- [x] **Step 2: Eseguire, verificare il fallimento.**
  Run: `python3 -m pytest src/the-stripper/tools -q`
  Expected: FAIL (script mancante).
- [x] **Step 3: Implementare con NumPy e Pillow** (rumore bianco filtrato con una piccola sfocatura periodica per la ripetibilità, illuminazione da un'altezza derivata dal rumore, alfa ricavato dalle sole luci). Documentare le dipendenze in testa allo script.
- [x] **Step 4: Eseguire i test, poi generare l'asset con seed fisso** e confrontarlo a occhio con la preview.
  Run: `python3 -m pytest src/the-stripper/tools -q && python3 src/the-stripper/tools/gen_panel_grain.py --out src/the-stripper/assets/panel_grain.png --seed 4`
  Expected: PASS e PNG creato.
- [x] **Step 5: Commit**

```bash
git add src/the-stripper/tools src/the-stripper/assets
git commit -m "feat(the-stripper): add procedural panel grain tile generator"
```

---

### Task 7: Integrazione VSTGUI4 e controlli disegnati dal codice

> **Stato:** quasi tutto esisteva già in `shared/ui` (Pillow Talk). Qui restava solo ciò che mancava per The Stripper (meter e LED di clip). I nomi e i percorsi sotto sono quelli del piano originale; la libreria vera è `shared/ui` (namespace `FairyTapes::Ui`).

**Files:**
- Modify: `src/the-stripper/CMakeLists.txt`, `.github/workflows/build.yml` (pacchetti di sistema per VSTGUI su Linux)
- Create: `src/the-stripper/src/ui/knob_math.h`, `src/the-stripper/src/ui/controls.{h,cpp}`
- Test: `src/the-stripper/tests/test_params.cpp`

**Interfaces:**
- Produces:
  - `ui/knob_math.h` (senza dipendenze da VSTGUI): `double knobAngleFromNorm(double n)` (da 225° a -45°, rotazione oraria), `double normFromDrag(double startNorm, double dyPixels, bool fine)` (200 px = pieno range, 1/10 in modalità fine), `double normOnDoubleClick(double defaultNorm)` (restituisce il default, clampato a 0..1), `int choiceIndexFromNorm(double n, int count)`, `double normFromChoiceIndex(int i, int count)`, `double meterNormFromDb(double db, double minDb, double maxDb)` (lineare in dB, clamp a 0..1).
  - `ui/controls.h`: `class AnalogKnob : public VSTGUI::CControl`, `class ToggleButton : public VSTGUI::CControl`, `class ChoiceButtons : public VSTGUI::CControl`, `class MeterBar : public VSTGUI::CControl` (barra a LED orizzontale, picco mantenuto, ingresso in dB) e `class ClipLed : public VSTGUI::CControl` (LED che al clic invia l'azzeramento), tutti disegnati con `CDrawContext` (gradienti, ombra, tacche, indicatore) e con il PNG opzionale dalla cartella degli asset se presente.

- [x] **Step 1: Scrivere i test** per `knob_math.h`: `test_knobAngle_endpoints` (0 → 225°, 1 → -45°, 0.5 → 90°), `test_normFromDrag_clamps_and_fine_mode`, `test_choice_index_roundtrip` (per count 2, 3, 4), `test_double_click_returns_default` (per i default reali di `params.h`; l'HPF torna a 20 Hz e il LPF a 20 kHz), `test_meterNormFromDb` (-60 → 0, +6 → 1, -27 → 0.5 entro 1e-6, fuori range clampato).
- [x] **Step 2: Eseguire, verificare il fallimento.**
- [x] **Step 3: Implementare `knob_math.h`.**
- [x] **Step 4: Aggiungere VSTGUI al CMake** del plugin (sorgenti e include da `vendors/vst3sdk/vstgui4`, librerie di sistema per Linux: X11, xcb, cairo, freetype, fontconfig; framework nativi su macOS e Windows) e i pacchetti `apt` al job Linux del workflow.
- [x] **Step 5: Implementare le classi di controllo.** Disegno vettoriale; trascinamento verticale con `normFromDrag`; doppio clic riporta al valore predefinito del parametro (`getDefaultValue()`) come modifica unica annullabile; la modalità fine con il tasto Maiuscolo.
- [x] **Step 6: Eseguire test e build del plugin.**
  Run: `cmake --build build --target the_stripper the_stripper_tests && ctest --test-dir build -R the_stripper --output-on-failure`
  Expected: build senza errori, test PASS.
- [x] **Step 7: Commit**

```bash
git add src/the-stripper .github/workflows/build.yml
git commit -m "feat(the-stripper): add VSTGUI4 integration and code-drawn controls"
```

---

### Task 8: Editor, layout e pulsante Load .nam

**Files:**
- Create: `src/the-stripper/src/ui/editor.{h,cpp}`
- Modify: `src/the-stripper/src/controller.{h,cpp}` (`createView` ritorna l'editor), `src/the-stripper/src/plug.cpp` se serve per i metadati dell'editor

**Interfaces:**
- Consumes: `AnalogKnob`, `ToggleButton`, `ChoiceButtons` (Task 7); `Controller::requestModelLoad/modelName/modelError` (Task 5); la tessera `assets/panel_grain.png` (Task 6); `MeterBar`, `ClipLed` (Task 7) e i valori di meter (Task 4).
- Produces: `class Editor : public Steinberg::Vst::VSTGUIEditor, public VSTGUI::IControlListener`, 1000 × 628; layout come nello spec §4 e nella preview `stripper_rack.html`; pulsanti IN per modulo (acceso = bypass 0) con attenuazione dei controlli a IN spento, e Bypass globale che attenua tutto; `Editor::onModeChanged()` mostra/nasconde Voice/Drums/Classic senza ricostruire la finestra.

- [x] **Step 1: Implementare `Editor::open`/`close`** costruendo le view in codice e registrandosi come listener dei parametri; `close` deve rimuovere tutti i listener e rilasciare le view.
- [x] **Step 2: Layout.** Una tabella `struct ControlSpec { ParamID id; ControlKind kind; Rect rect; const char* label; }` per sezione, senza calcolare coordinate sparse nel codice. Sfondo: vedi Step 4.
- [x] **Step 2b: Fascia dei meter** sotto l'intestazione: due `MeterBar` stereo (Input, Output), due `MeterBar` per le gain reduction (Comp, Limiter, disegnate dall'alto verso il basso) e sei `ClipLed` (Input, EQ, Comp, Drive, Limiter, Output). I valori arrivano dai parametri `kMeter*` con un timer da 30 ms; il clic su un `ClipLed` scrive `kClearClips`.
- [x] **Step 3: Nastro con il nome del modello NAM** (in basso a destra nel riquadro Drive, striscia fissa cliccabile, niente pulsante né riquadro di input; nome senza `.nam`, nel font del nastro, troncato per larghezza misurata con puntino di sospensione) che apre il popup di scelta modello (spec §3.1, preview `stripper_model_browser.html`): scheda LOCAL con `LocalFolderSource` (scansione ricorsiva delle cartelle, ricerca, NO MODEL) e chiama `Controller::requestModelLoad`; mostra `modelName()` oppure `modelError()` in rosso, con icona a chip grigia per la GPU. Senza modello: passthrough e controllo Drive attenuato. Aggiungere anche la schermata CREDITS con flip (spec §4).
- [x] **Step 4: Sfondo.** Tinta `#161617`, tessera di grana ripetuta sopra, gradiente di luce disegnato dal codice; se il PNG manca, solo tinta e gradiente. Bordi dei riquadri tracciati con un'interruzione per il titolo (nessun rettangolo di mascheratura), così la grana passa anche dietro i titoli.
- [x] **Step 5: Verifica build e test.** (2026-10-21: 32 test verdi, bundle compilato con `the_stripper`.)
  Run: `cmake --build build --target the_stripper the_stripper_tests && ctest --test-dir build -R the_stripper --output-on-failure && ls "dist/TheStripper.vst3"`
  Expected: build senza errori, test PASS, bundle presente.
- [ ] **Step 6: Verifica manuale in Reaper e Cubase** (a cura dell'utente): aprire e chiudere l'editor 50 volte; automazione dei parametri; cambio di Strip Mode; caricamento di un `.nam` valido, di un file inesistente e di un file di testo.
- [x] **Step 7: Commit** (fatto a più riprese, uno per compito)

```bash
git add src/the-stripper
git commit -m "feat(the-stripper): add VSTGUI editor with NAM model picker"
```

---

### Task 9: Ricerca SSL 360 Link e docs

**Files:**
- Modify: `src/the-stripper/docs/FEATURES.md` (§4), `docs/PARAMS_STATUS.md`, `docs/CONTROLS.md`, `docs/BUILD_AND_USE.md`, `docs/UI_ASSETS_SPEC.md` (note: la GUI è disegnata dal codice), `src/the-stripper/README.md`, `README.md` radice

- [x] **Step 1: Ricerca dei requisiti ufficiali SSL 360 Link** sul web (documentazione SSL, forum, note di rilascio); annotare fonte e data. Riportare in `FEATURES.md §4` ciò che è documentato; se non esiste nulla di pubblico, scriverlo e mantenere la regola attuale.
- [x] **Step 2: Allineare i docs al codice:** conteggio parametri (`kNumParams`), Drive (`kDriveAmount`), i due nuovi parametri, caricamento del modello, GUI, stato v8, comandi di build con le dipendenze di VSTGUI.
- [x] **Step 3: Verifica finale della suite**
  Run: `cmake --build build && ctest --test-dir build --output-on-failure`
  Expected: tutti i test PASS (Stripper, nam_drive, test di Pillow Talk se già presenti).
- [x] **Step 4: Commit**

```bash
git add src/the-stripper README.md
git commit -m "docs(the-stripper): SSL 360 Link findings and docs aligned to code"
```

---

## Extra di interfaccia (Task 10-14)

Compiti nati dopo le preview; ciascuno ha la propria spec nel §4 e §6 del documento di design. Si fanno dopo il Task 8 e dipendono dal Task 3 (stato v8 esteso).

### Task 10: Preferenze dell'utente

**Fatto (2026-10-14)**, vedi lo stato dei compiti.

- File di preferenze (percorso per piattaforma), lettura e scrittura atomica; chiavi stabili (spec §6); nessuna dipendenza dallo stato del progetto.
- Test: file mancante → default; file corrotto → default senza crash; scrittura e rilettura.

### Task 11: Livelli e usura

- Include le sigle serigrafate dei blocchi (spec §11) e il LED giallo dei selettori; Bell attenuato in modalità Bus e scritta BUS al posto dei preset del compressore (proposti, da confermare).

- Livelli dell'editor nell'ordine dello spec §4 (grana, bordi, serigrafia e valori, usura, viti, controlli) con bitmap generata una sola volta dal seme e rilasciata alla chiusura dell'editor.
- Generatore dell'usura a partire dal seme (graffi, aloni, macchie, residui, croste a pennello); usura dei controlli (alluminio tornito: grana, bordi consumati, sporco alla base, puntini bruni e grigi, tacchetta) al 25% di opacità.
- Test: stesso seme → stessa bitmap; Wear Off → nessun elemento; costo di generazione sotto una soglia ragionevole.

### Task 12: Nastro, scritte e font a tracciati

- Script che converte i glifi dei tre font (Zeyada, Rock Salt, Homemade Apple) in tracciati vettoriali con licenze incluse; disegno del testo con variazione per lettera dal seme; normalizzazione della dimensione per font.
- Nastro scrivibile (campo di testo, 14 caratteri, salvato nello stato); striscia del nome del modello con troncamento per larghezza misurata.

### Task 13: Retro del pannello (Settings e Credits)

- Scheda dedicata che spiega i pulsanti che impongono configurazioni o cambiano modelli, con i valori che impongono, **generata dalla stessa tabella usata dal codice** (spec §4; allineare al caso di Hotline, scheda DEVICES).
- Flip 3D, schede SETTINGS e CREDITS, controlli delle impostazioni (preferenze e comportamento) e testi di credits in inglese; licenze ricontrollate.

### Task 14: Viti, valori digitabili, richiamo

- Viti interattive (a taglio o a croce, angolo casuale, una può mancare con probabilità bassa), inserimento manuale del valore sotto la manopola (mappa inversa dei parametri), segni di richiamo e interruttore Notes (dopo la decisione sul §11).

### Task 15: Drive con interruttore NAM / Saturate

**Fatto (2026-10-14); icona GPU nel pulsante NAM fatta il 2026-10-15** (campo `gpu` in `ModelStatus`, `ui/gpu_chip_view`, visibile solo con NAM acceso e modello caricato su GPU, attenuata col modulo; test del messaggio con mutazione; resa verificata con `the_stripper_render ... gpu`): `kDriveNamOn` (ID 35, stato v9), `SSLSaturation` spostata nel processor come motore alternativo del Drive e tolta dal 4K-EQ, due pulsanti alternati NAM / 4K (`ChoiceButtons`, parametro invertito) + manopola DRIVE condivisa + striscia "4K Saturate" nell'editor; test: tabella bloccata, stato v8→v9, layout, e nel modulo vero (la quantità di Saturate non cambia più l'EQ; con NAM spento è udibile).

Decisione del 2026-10-14 (spec §11). Dopo i Task 10-14, o prima se serve.

- [x] Nuovo parametro `kDriveNamOn` (ID 35, in coda, default 1) con tabella bloccata e stato v9 (stati più vecchi: valore di default, nessuna migrazione: non esistono progetti da proteggere).
- DSP: `SSLSaturation` esce da `FourKEQ` e diventa una fase del Drive dopo l'EQ; NAM acceso → NAM (senza modello: passthrough), spento → Saturate; il bypass del Drive vale per entrambi; l'EQ non ha più saturazione né dipende dal suo bypass.
- Editor: due pulsanti alternati NAM / 4K nel riquadro Drive; una sola manopola DRIVE che si lega a `kDriveAmount` o `kEqSaturation` secondo l'interruttore; Saturate sparisce dal riquadro Equaliser (restano 10 manopole); striscia del modello che a NAM spento mostra "4K Saturate" al posto del nome (non cliccabile a NAM spento: il popup si apre solo con il NAM acceso).
- Test: tabella bloccata, stato v8 → v9, testo della striscia per motore, scelta del motore (uscita diversa con NAM spento e Saturate > 0), EQ senza saturazione, layout (nessuna sovrapposizione, manopola Drive unica).

### Task 16: Meter del gate

**Fatto (2026-10-14):** `kMeterGrGate` (dopo `kClearClips`), `GainReductionMeter::setReleaseDbPerSec` (600 dB/s per il gate), riga "G" nel riquadro Level (righe GR a 192/205/218, LED di clip a 240, scala del gate 0…40 dB), parametro visibile e in sola lettura nella codifica SSL; test unit, di layout e nel modulo vero (soglia alta con segnale a −54 dBFS → riduzione > 0; gate fuori, soglia bassa e bypass globale → 0). Estensione del gate misurata con segnali di test: Denoise chiude sotto circa −48 dB alla soglia massima.

Decisione del 2026-10-14 (spec §11). Dopo il Task 15, o prima se serve.

- `kMeterGrGate` in coda ai meter (prima di `kNumMeterIDs`; nessun ID esistente cambia), visibile e in sola lettura, codifica SSL (`grValueFromDb`); `GainReductionMeter` con rilascio veloce configurabile per il gate; il processor misura i picchi prima e dopo il gate.
- Layout: riga "G" nel riquadro Level (righe GR e LED di clip ricompattati, tabella `kMeterRows` e test di geometria aggiornati); attenuazione col Bypass globale; FEATURES §4 e CONTROLS aggiornati (terza corsia SSL).
- Test: gate aperto → 0; gate che chiude → riduzione positiva e discesa rapida; in bypass → 0; codifica SSL; geometria senza sovrapposizioni.
