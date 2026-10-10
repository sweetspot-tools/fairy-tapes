# The Stripper — Completamento: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Aggiungere a The Stripper la scelta del modello NAM, un editor VSTGUI disegnato dal codice, i parametri di limiter e gate, la conformità SSL 360 Link e i docs allineati.

**Architecture:** `NamDrive` si estrae in `shared/nam_drive` e riceve un caricamento asincrono (thread di lavoro + scambio atomico). Il controller invia al processor il percorso del `.nam` con un `IMessage`. L'editor VSTGUI è costruito in codice, con controlli disegnati da `CDrawContext` e una tessera di grana PNG prodotta da uno script.

**Tech Stack:** C++20, VST3 SDK + VSTGUI4 (`vendors/vst3sdk/vstgui4`), NeuralAmpModelerCore, CUDA opzionale (`shared/gpu_nam`), Python 3 (script della tessera di grana), CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-10-09-the-stripper-completion-design.md`

## Stato dei compiti (2026-10-10)

- [x] Task 1 — `NamDrive` in `shared/nam_drive` (commit `5805520`, adattato a NAM v0.6.0 in `10cb131`; 49 test su 49; CUDA non verificato)
- [x] Task 2 — caricamento asincrono (`requestLoad`, `LoadState`; 5 test, 25 ripetizioni senza errori). Trovato e corretto un difetto del Task 1: i registratori delle architetture NAM sono inizializzatori statici e il linker li scartava dalla libreria statica; i consumatori usano ora `nam_drive_link(<target>)` (collegamento whole-archive)
- [ ] Task 3 — parametri nuovi, stato v8 esteso (vedi spec §6), tabella bloccata
- [ ] Task 4 — meter
- [ ] Task 5 — messaggi del modello
- [ ] Task 6 — tessera di grana
- [ ] Task 7 — VSTGUI e controlli
- [ ] Task 8 — editor (suddiviso in 8a layout/binding, 8b fascia meter, 8c striscia del modello e popup locale)
- [ ] Task 9 — ricerca SSL 360 Link e docs
- [ ] Task 10-14 — extra di interfaccia, vedi in fondo (usura, nastro, retro, viti, preferenze)

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
- Licenza GPL-3.0; nessun nome o branding delle fonti nella UI.

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
- [ ] **Step 5: Commit**

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

- [ ] **Step 1: Scrivere i test:** `test_new_params_appended_at_end` (`kLimiterPound == kNumParams - 2`, `kGateThreshold == kNumParams - 1`, e nessun ID precedente cambiato); `test_param_table_matches_golden` (tabella ID → nome → passi → default letta da `param_table_golden.h`, generata dallo stato attuale del codice **prima** dell'aggiunta e poi estesa con i due nuovi); `test_state_v8_roundtrip` (parametri, percorso con caratteri non ASCII); `test_state_v7_loads_with_defaults` (fixture v7 scritta con la vecchia struttura: percorso vuoto); `test_state_unknown_version_rejected`.
- [ ] **Step 2: Generare `param_table_golden.h` dal codice attuale** (che include già `kDefaultEqHPFFreq = 0.0`, vedi spec §4, Default dei filtri) con un piccolo programma temporaneo, estenderlo a mano con i due nuovi parametri e verificare che il test sul nome/ordine fallisca se si scambiano due ID.
- [ ] **Step 3: Eseguire, verificare il fallimento.**
- [ ] **Step 4: Implementare** parametri, `readState`/`writeState` (versione 8; lettura v7 mantenuta), uso dei due parametri nel processor, titoli e unità nel controller.
- [ ] **Step 5: Eseguire i test.**
  Run: `cmake --build build --target the_stripper_tests && ctest --test-dir build -R the_stripper --output-on-failure`
  Expected: PASS.
- [ ] **Step 6: Commit**

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
  - `float currentGainReductionDb() const` (>= 0) su `MultiCompOpto/FET/VCA/Bus` e `BlockParty` (da `envelope`/coefficienti del motore; per `BlockParty`, se non esposto, dal rapporto tra picchi di uscita e ingresso dell'ultimo blocco).

- [ ] **Step 1: Scrivere i test:** `test_peakmeter_instant_attack_and_release` (impulso a 0.5 → `levelDb ≈ -6` subito; dopo 1 s di silenzio sceso di 20 ± 1 dB); `test_peakmeter_hold_1_5s` (`holdDb` resta al picco per 1.4 s e cade dopo 1.6 s); `test_clip_detects_at_exactly_one` (0.9999 → spento; 1.0 → acceso); `test_clip_hold_2s_and_clear` (acceso a 1.9 s, spento a 2.1 s; `clear()` lo spegne subito); `test_gr_zero_when_not_compressing` e `test_gr_positive_when_compressing` per ciascun motore (rumore a -30 dBFS e a -6 dBFS con Amount 0.8); `test_meter_ids_do_not_collide_with_params` (`kMeterBase >= kNumParams`, intervalli disgiunti); `test_meters_not_in_state` (`writeState` non cambia di dimensione aggiungendo i meter); `test_meter_update_rate_limit` (con blocchi da 64 campioni a 48 kHz, al massimo una scrittura ogni 16 ms).
- [ ] **Step 2: Eseguire, verificare il fallimento.**
- [ ] **Step 3: Implementare `PeakMeter`, `ClipDetector` e `currentGainReductionDb()`** sui motori.
- [ ] **Step 4: Integrare nel `Processor`:** un `ClipDetector` per stadio (misurato all'uscita di Input gain, EQ, Compression, Drive, Limiter e Output gain) e due `PeakMeter` stereo (ingresso prima di tutto, uscita dopo l'Output gain); a fine blocco, se sono passati almeno 16 ms, scrivere i valori normalizzati in `outputParameterChanges` come parametri di sola lettura. Gli stadi in bypass non accendono il loro LED. Il `Controller` registra i parametri con `kIsReadOnly | kIsHidden`.
- [ ] **Step 5: Eseguire i test.**
  Run: `cmake --build build --target the_stripper_tests && ctest --test-dir build -R the_stripper --output-on-failure`
  Expected: PASS.
- [ ] **Step 6: Commit**

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

- [ ] **Step 1: Scrivere i test** sulle parti pure: `test_model_message_roundtrip` (funzioni libere `encodeLoadMessage`/`decodeLoadMessage` su una classe `IMessage` di test); `test_state_restores_model_path_triggers_load` (dopo `setState` con percorso valido, `NamDrive` va in `Loading`/`Loaded`).
- [ ] **Step 2: Eseguire, verificare il fallimento.**
- [ ] **Step 3: Implementare.** Il processor gestisce `"LoadModel"` in `notify`, chiama `requestLoad`; ogni volta che lo stato cambia (controllato a fine `process`, senza allocazioni) invia `"ModelStatus"` tramite il canale di messaggi del host; `getState`/`setState` salvano e ricaricano il percorso. Il controller memorizza nome ed errore ricevuti e li passa all'editor.
- [ ] **Step 4: Eseguire i test.** Expected: PASS.
- [ ] **Step 5: Commit**

```bash
git add src/the-stripper
git commit -m "feat(the-stripper): load NAM model via controller/processor messages"
```

---

### Task 6: Tessera di grana del pannello

**Files:**
- Create: `src/the-stripper/tools/gen_panel_grain.py`, `src/the-stripper/tools/test_gen_panel_grain.py`
- Create: `src/the-stripper/assets/panel_grain.png` (generato e committato)

**Interfaces:**
- Produces: `gen_panel_grain.py [--out FILE] [--seed N]` che scrive una tessera 256 × 256 RGBA ripetibile senza giunture visibili: pixel bianchi con alfa derivato da rumore illuminato (equivalente al filtro `tfine` della preview `stripper_rack.html`), alfa massimo circa 0,07, deterministica per un dato seed.

- [ ] **Step 1: Scrivere il test** (`pytest`): dimensioni 256 × 256 RGBA; stesso seed → file identici (hash); seed diversi → file diversi; alfa massimo ≤ 0,10 e media ≤ 0,04 (grana quasi impercettibile); la tessera è ripetibile (differenza media tra bordo sinistro e destro, e tra alto e basso, non maggiore di quella tra colonne adiacenti interne).
- [ ] **Step 2: Eseguire, verificare il fallimento.**
  Run: `python3 -m pytest src/the-stripper/tools -q`
  Expected: FAIL (script mancante).
- [ ] **Step 3: Implementare con NumPy e Pillow** (rumore bianco filtrato con una piccola sfocatura periodica per la ripetibilità, illuminazione da un'altezza derivata dal rumore, alfa ricavato dalle sole luci). Documentare le dipendenze in testa allo script.
- [ ] **Step 4: Eseguire i test, poi generare l'asset con seed fisso** e confrontarlo a occhio con la preview.
  Run: `python3 -m pytest src/the-stripper/tools -q && python3 src/the-stripper/tools/gen_panel_grain.py --out src/the-stripper/assets/panel_grain.png --seed 4`
  Expected: PASS e PNG creato.
- [ ] **Step 5: Commit**

```bash
git add src/the-stripper/tools src/the-stripper/assets
git commit -m "feat(the-stripper): add procedural panel grain tile generator"
```

---

### Task 7: Integrazione VSTGUI4 e controlli disegnati dal codice

**Files:**
- Modify: `src/the-stripper/CMakeLists.txt`, `.github/workflows/build.yml` (pacchetti di sistema per VSTGUI su Linux)
- Create: `src/the-stripper/src/ui/knob_math.h`, `src/the-stripper/src/ui/controls.{h,cpp}`
- Test: `src/the-stripper/tests/test_params.cpp`

**Interfaces:**
- Produces:
  - `ui/knob_math.h` (senza dipendenze da VSTGUI): `double knobAngleFromNorm(double n)` (da 225° a -45°, rotazione oraria), `double normFromDrag(double startNorm, double dyPixels, bool fine)` (200 px = pieno range, 1/10 in modalità fine), `double normOnDoubleClick(double defaultNorm)` (restituisce il default, clampato a 0..1), `int choiceIndexFromNorm(double n, int count)`, `double normFromChoiceIndex(int i, int count)`, `double meterNormFromDb(double db, double minDb, double maxDb)` (lineare in dB, clamp a 0..1).
  - `ui/controls.h`: `class AnalogKnob : public VSTGUI::CControl`, `class ToggleButton : public VSTGUI::CControl`, `class ChoiceButtons : public VSTGUI::CControl`, `class MeterBar : public VSTGUI::CControl` (barra a LED orizzontale, picco mantenuto, ingresso in dB) e `class ClipLed : public VSTGUI::CControl` (LED che al clic invia l'azzeramento), tutti disegnati con `CDrawContext` (gradienti, ombra, tacche, indicatore) e con il PNG opzionale dalla cartella degli asset se presente.

- [ ] **Step 1: Scrivere i test** per `knob_math.h`: `test_knobAngle_endpoints` (0 → 225°, 1 → -45°, 0.5 → 90°), `test_normFromDrag_clamps_and_fine_mode`, `test_choice_index_roundtrip` (per count 2, 3, 4), `test_double_click_returns_default` (per i default reali di `params.h`; l'HPF torna a 20 Hz e il LPF a 20 kHz), `test_meterNormFromDb` (-60 → 0, +6 → 1, -27 → 0.5 entro 1e-6, fuori range clampato).
- [ ] **Step 2: Eseguire, verificare il fallimento.**
- [ ] **Step 3: Implementare `knob_math.h`.**
- [ ] **Step 4: Aggiungere VSTGUI al CMake** del plugin (sorgenti e include da `vendors/vst3sdk/vstgui4`, librerie di sistema per Linux: X11, xcb, cairo, freetype, fontconfig; framework nativi su macOS e Windows) e i pacchetti `apt` al job Linux del workflow.
- [ ] **Step 5: Implementare le classi di controllo.** Disegno vettoriale; trascinamento verticale con `normFromDrag`; doppio clic riporta al valore predefinito del parametro (`getDefaultValue()`) come modifica unica annullabile; la modalità fine con il tasto Maiuscolo.
- [ ] **Step 6: Eseguire test e build del plugin.**
  Run: `cmake --build build --target the_stripper the_stripper_tests && ctest --test-dir build -R the_stripper --output-on-failure`
  Expected: build senza errori, test PASS.
- [ ] **Step 7: Commit**

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

- [ ] **Step 1: Implementare `Editor::open`/`close`** costruendo le view in codice e registrandosi come listener dei parametri; `close` deve rimuovere tutti i listener e rilasciare le view.
- [ ] **Step 2: Layout.** Una tabella `struct ControlSpec { ParamID id; ControlKind kind; Rect rect; const char* label; }` per sezione, senza calcolare coordinate sparse nel codice. Sfondo: vedi Step 4.
- [ ] **Step 2b: Fascia dei meter** sotto l'intestazione: due `MeterBar` stereo (Input, Output), due `MeterBar` per le gain reduction (Comp, Limiter, disegnate dall'alto verso il basso) e sei `ClipLed` (Input, EQ, Comp, Drive, Limiter, Output). I valori arrivano dai parametri `kMeter*` con un timer da 30 ms; il clic su un `ClipLed` scrive `kClearClips`.
- [ ] **Step 3: Nastro con il nome del modello NAM** (in basso a destra nel riquadro Drive, striscia fissa cliccabile, niente pulsante né riquadro di input; nome senza `.nam`, nel font del nastro, troncato per larghezza misurata con puntino di sospensione) che apre il popup di scelta modello (spec §3.1, preview `stripper_model_browser.html`): scheda LOCAL con `LocalFolderSource` (scansione ricorsiva delle cartelle, ricerca, NO MODEL) e chiama `Controller::requestModelLoad`; mostra `modelName()` oppure `modelError()` in rosso, con icona a chip grigia per la GPU. Senza modello: passthrough e controllo Drive attenuato. Aggiungere anche la schermata CREDITS con flip (spec §4).
- [ ] **Step 4: Sfondo.** Tinta `#161617`, tessera di grana ripetuta sopra, gradiente di luce disegnato dal codice; se il PNG manca, solo tinta e gradiente. Bordi dei riquadri tracciati con un'interruzione per il titolo (nessun rettangolo di mascheratura), così la grana passa anche dietro i titoli.
- [ ] **Step 5: Verifica build e test.**
  Run: `cmake --build build --target the_stripper the_stripper_tests && ctest --test-dir build -R the_stripper --output-on-failure && ls "dist/The Stripper.vst3"`
  Expected: build senza errori, test PASS, bundle presente.
- [ ] **Step 6: Verifica manuale in Reaper e Cubase** (a cura dell'utente): aprire e chiudere l'editor 50 volte; automazione dei parametri; cambio di Strip Mode; caricamento di un `.nam` valido, di un file inesistente e di un file di testo.
- [ ] **Step 7: Commit**

```bash
git add src/the-stripper
git commit -m "feat(the-stripper): add VSTGUI editor with NAM model picker"
```

---

### Task 9: Ricerca SSL 360 Link e docs

**Files:**
- Modify: `src/the-stripper/docs/FEATURES.md` (§4), `docs/PARAMS_STATUS.md`, `docs/CONTROLS.md`, `docs/BUILD_AND_USE.md`, `docs/UI_ASSETS_SPEC.md` (note: la GUI è disegnata dal codice), `src/the-stripper/README.md`, `README.md` radice

- [ ] **Step 1: Ricerca dei requisiti ufficiali SSL 360 Link** sul web (documentazione SSL, forum, note di rilascio); annotare fonte e data. Riportare in `FEATURES.md §4` ciò che è documentato; se non esiste nulla di pubblico, scriverlo e mantenere la regola attuale.
- [ ] **Step 2: Allineare i docs al codice:** conteggio parametri (`kNumParams`), Drive (`kDriveAmount`), i due nuovi parametri, caricamento del modello, GUI, stato v8, comandi di build con le dipendenze di VSTGUI.
- [ ] **Step 3: Verifica finale della suite**
  Run: `cmake --build build && ctest --test-dir build --output-on-failure`
  Expected: tutti i test PASS (Stripper, nam_drive, test di Pillow Talk se già presenti).
- [ ] **Step 4: Commit**

```bash
git add src/the-stripper README.md
git commit -m "docs(the-stripper): SSL 360 Link findings and docs aligned to code"
```

---

## Extra di interfaccia (Task 10-14)

Compiti nati dopo le preview; ciascuno ha la propria spec nel §4 e §6 del documento di design. Si fanno dopo il Task 8 e dipendono dal Task 3 (stato v8 esteso).

### Task 10: Preferenze dell'utente

- File di preferenze (percorso per piattaforma), lettura e scrittura atomica; chiavi stabili (spec §6); nessuna dipendenza dallo stato del progetto.
- Test: file mancante → default; file corrotto → default senza crash; scrittura e rilettura.

### Task 11: Livelli e usura

- Livelli dell'editor nell'ordine dello spec §4 (grana, bordi, serigrafia e valori, usura, viti, controlli) con bitmap generata una sola volta dal seme e rilasciata alla chiusura dell'editor.
- Generatore dell'usura a partire dal seme (graffi, aloni, macchie, residui, croste a pennello); usura dei controlli (alluminio tornito: grana, bordi consumati, sporco alla base, puntini bruni e grigi, tacchetta) al 25% di opacità.
- Test: stesso seme → stessa bitmap; Wear Off → nessun elemento; costo di generazione sotto una soglia ragionevole.

### Task 12: Nastro, scritte e font a tracciati

- Script che converte i glifi dei tre font (Zeyada, Rock Salt, Homemade Apple) in tracciati vettoriali con licenze incluse; disegno del testo con variazione per lettera dal seme; normalizzazione della dimensione per font.
- Nastro scrivibile (campo di testo, 14 caratteri, salvato nello stato); striscia del nome del modello con troncamento per larghezza misurata.

### Task 13: Retro del pannello (Settings e Credits)

- Flip 3D, schede SETTINGS e CREDITS, controlli delle impostazioni (preferenze e comportamento) e testi di credits in inglese; licenze ricontrollate.

### Task 14: Viti, valori digitabili, richiamo

- Viti interattive (a taglio o a croce, angolo casuale, una può mancare con probabilità bassa), inserimento manuale del valore sotto la manopola (mappa inversa dei parametri), segni di richiamo e interruttore Notes (dopo la decisione sul §11).
