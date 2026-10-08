# Doggystyle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Costruire Doggystyle, un plugin VST3 di compressione parallela con un compressore FET e uno Opto, blend, mix col dry e NAM opzionale in coda.

**Architecture:** I motori FET e Opto si estraggono dallo Stripper in `shared/multicomp`. Una classe `ParallelComp` li esegue in parallelo e fa blend a potenza costante e mix col dry; il `Processor` aggiunge input/output gain e lo stadio `NamDrive` condiviso.

**Tech Stack:** C++20, VST3 SDK, `shared/multicomp` (Dusk Audio, GPL-3.0), `shared/nam_drive` + `shared/gpu_nam`, nlohmann/json (già in `vendors/NeuralAmpModelerCore`), CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-10-09-doggystyle-design.md`

**Prerequisito:** il piano `2026-10-09-the-stripper-completion.md` deve aver completato i Task 1 e 2 (`shared/nam_drive` con caricamento asincrono).

## Global Constraints

- Namespace `FairyTapes::Doggystyle`; DSP condiviso in `FairyTapes::Shared::DSP`.
- Solo topologia parallela; niente seriale, niente GUI custom, niente sidechain esterno.
- Bundle `Doggystyle.vst3` in `dist/`; ingresso e uscita stereo.
- Blend a potenza costante: guadagni `gFet = cos(blend·π/2)`, `gOpto = sin(blend·π/2)`.
- Il NAM sta dopo il Mix; la sua latenza si riporta all'host con `getLatencySamples()`.
- `kStateVersion = 1`: versione, `kNumParams` valori `double`, percorso del modello (stringa UTF-8 con lunghezza).
- Licenza GPL-3.0.

## Review Focus

- Blocchi da 0 e da 1 campione: nessun crash né uscita corrotta.
- Ingresso mono (L = R): l'uscita resta identica sui due canali, nessun panning spurio.
- Entrambi gli Amount a 0: il wet coincide con il dry (nessun colore né cambio di livello oltre ±0.1 dB).
- NAM bypassato o senza modello: il segnale dopo il Mix passa invariato.
- Cambio di sample rate tra due `setActive`: motori ricalcolati senza click.

---

### Task 1: Estrarre i motori in `shared/multicomp`

**Files:**
- Create: `shared/multicomp/CMakeLists.txt`
- Move: `src/the-stripper/src/dsp/multicomp_engines.{h,cpp}` → `shared/multicomp/include/multicomp_engines.h`, `shared/multicomp/src/multicomp_engines.cpp`
- Move: `src/the-stripper/src/dsp/hardware_emulation/` → `shared/multicomp/include/hardware_emulation/`
- Modify: `src/the-stripper/CMakeLists.txt`, `src/the-stripper/src/dsp/compression_stub.{h,cpp}`, `src/the-stripper/tests/test_params.cpp`

**Interfaces:**
- Produces: libreria CMake `multicomp` (include `PUBLIC` in `include/`); classi `MultiCompFET` e `MultiCompOpto` in `FairyTapes::Shared::DSP`, con l'API attuale: `prepare(double sampleRate)`, `setParams(float amount, float mix)`, `processStereo(float* left, float* right, int numSamples, const float* sidechainL = nullptr, const float* sidechainR = nullptr)`.

- [ ] **Step 1: Verificare la baseline**
  Run: `cmake -S . -B build && cmake --build build --target the_stripper_tests && ctest --test-dir build -R the_stripper --output-on-failure`
  Expected: PASS.
- [ ] **Step 2: Spostare i file con `git mv`**, cambiare il namespace in `FairyTapes::Shared::DSP` e creare la libreria `multicomp`.
- [ ] **Step 3: Aggiornare lo Stripper** perché colleghi `multicomp` (plugin e test) e usi il nuovo namespace in `compression_stub.{h,cpp}` e nei test.
- [ ] **Step 4: Rieseguire i test dello Stripper.** Expected: PASS identico alla baseline.
- [ ] **Step 5: Commit**

```bash
git add shared/multicomp src/the-stripper
git commit -m "refactor: extract Multi-Comp engines to shared/multicomp"
```

---

### Task 2: Parametri, class ID e legge del blend

**Files:**
- Create: `src/doggystyle/src/params.h`, `src/doggystyle/src/cids.h`, `src/doggystyle/tests/test_doggystyle.cpp`, `src/doggystyle/CMakeLists.txt` (target `doggystyle_tests` per ora)
- Modify: `CMakeLists.txt` (aggiungere `add_subdirectory(src/doggystyle)`)

**Interfaces:**
- Produces, in `params.h`:
  - `enum ParamID : Steinberg::Vst::ParamID { kBypass = 0, kInput, kFetAmount, kOptoAmount, kBlend, kMix, kNamBypass, kNamDrive, kOutput, kNumParams };`
  - `double gainDbFromNorm(double n)` (-12..+12 dB lineare), `double dbToLinear(double db)`
  - `struct BlendGains { float fet; float opto; }; BlendGains blendGains(float blend);`
  - Default come da spec §5: Input 0.5 (0 dB), FetAmount 0.5, OptoAmount 0.3, Blend 0.5, Mix 1.0, NamBypass 1.0, NamDrive 0.3, Output 0.5.
- `cids.h`: `kProcessorUID`, `kControllerUID` con FUID nuovi e diversi da quelli delle altre due.

- [ ] **Step 1: Scrivere i test** (macro di `the-stripper/tests/test_params.cpp`): `test_blendGains_endpoints` (0 → fet 1, opto 0; 1 → fet 0, opto 1); `test_blendGains_constant_power` (per 0, 0.25, 0.5, 0.75, 1: `fet² + opto²` = 1 entro 0.012, cioè ±0.05 dB); `test_gainDb_range` (0 → -12 dB, 0.5 → 0 dB, 1 → +12 dB); `test_param_enum_count` (`kNumParams == 9`); `test_defaults_in_range` (ogni default in [0, 1]).
- [ ] **Step 2: Creare il CMake del plugin** (solo target di test) modellato su `the-stripper/CMakeLists.txt`.
- [ ] **Step 3: Eseguire, verificare il fallimento.**
  Run: `cmake -S . -B build && cmake --build build --target doggystyle_tests`
  Expected: errore di compilazione (`params.h` mancante).
- [ ] **Step 4: Implementare `params.h` e `cids.h`.**
- [ ] **Step 5: Eseguire i test.**
  Run: `cmake --build build --target doggystyle_tests && ctest --test-dir build -R doggystyle --output-on-failure`
  Expected: PASS.
- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/doggystyle
git commit -m "feat(doggystyle): add params, class IDs and blend law"
```

---

### Task 3: ParallelComp

**Files:**
- Create: `src/doggystyle/src/dsp/parallel_comp.{h,cpp}`
- Modify: `src/doggystyle/tests/test_doggystyle.cpp`, `src/doggystyle/CMakeLists.txt` (collegare `multicomp`, aggiungere `parallel_comp.cpp`)

**Interfaces:**
- Consumes: `blendGains` (Task 2), `MultiCompFET`, `MultiCompOpto` (Task 1).
- Produces: `class ParallelComp` in `FairyTapes::Doggystyle::DSP` con
  - `void prepare(double sampleRate, int maxBlockSize)` — alloca i buffer di lavoro, ricalcola i motori
  - `void setParams(float fetAmount, float optoAmount, float blend, float mix)`
  - `void process(float* left, float* right, int numSamples)` — in-place; `numSamples == 0` è un no-op; il dry usato per il Mix è l'ingresso del blocco
  - `void reset()`

- [ ] **Step 1: Scrivere i test** (48 kHz): `test_mix_zero_is_dry` (uscita identica all'ingresso entro 1e-6); `test_both_amounts_zero_wet_matches_dry` (stesso ingresso, Mix 1, differenza < 0.1 dB di livello RMS); `test_blend_extremes_select_branch` (Blend 0 con OptoAmount 0 e Blend 1 con FetAmount 0 danno risultati diversi, e Blend 0 è identico a un `MultiCompFET` da solo con gli stessi parametri entro 1e-5); `test_fet_reduces_peak_more_than_opto` (serie di clic a 120 BPM, Amount 0.7 per entrambi: il picco in uscita con Blend 0 è inferiore a quello con Blend 1); `test_mono_input_stays_identical_on_both_channels` (L = R in ingresso → L = R in uscita, entro 1e-6); `test_zero_and_one_sample_blocks`; `test_no_nan_on_silence_and_full_scale`.
- [ ] **Step 2: Eseguire, verificare il fallimento.**
- [ ] **Step 3: Implementare `ParallelComp`:** copia dell'ingresso in due coppie di buffer, un motore per ramo con `mix = 1`, somma `wet = gFet·fet + gOpto·opto`, uscita `dry·(1 - mix) + wet·mix`. Nessuna allocazione in `process`.
- [ ] **Step 4: Eseguire i test.** Expected: PASS.
- [ ] **Step 5: Commit**

```bash
git add src/doggystyle
git commit -m "feat(doggystyle): add parallel FET/Opto compressor with constant-power blend"
```

---

### Task 4: Plugin VST3, NAM, stato e docs

**Files:**
- Create: `src/doggystyle/src/processor.{h,cpp}`, `src/doggystyle/src/controller.{h,cpp}`, `src/doggystyle/src/plug.cpp`, `src/doggystyle/src/model_config.{h,cpp}`, `src/doggystyle/src/Info.plist`
- Create: `src/doggystyle/docs/CONTROLS.md`
- Modify: `src/doggystyle/CMakeLists.txt` (target `doggystyle` SHARED, output `Doggystyle.vst3` in `dist/` come The Stripper; collega `multicomp`, `nam_drive`), `src/doggystyle/README.md`, `src/doggystyle/docs/CONCEPT.md` (checklist chiusa), `README.md` radice (stato)
- Test: `src/doggystyle/tests/test_doggystyle.cpp`

**Interfaces:**
- Consumes: `ParallelComp` (Task 3), `NamDrive` (`shared/nam_drive`), `params.h` (Task 2).
- Produces:
  - `std::optional<std::filesystem::path> readModelConfig(const std::filesystem::path& configFile)` in `model_config.h` — legge la chiave `"nam_model"` di `doggystyle.json`; assente, malformato o file mancante → `std::nullopt`
  - `std::filesystem::path pluginDirectory()` — cartella del modulo (`dladdr` su Linux/macOS, `GetModuleHandleExW` su Windows)
  - Struttura di stato `struct PluginState { double params[kNumParams]; std::string modelPath; }` con `readState`/`writeState` come nel piano dello Stripper (Task 3)
  - Classi `Processor` e `Controller` con bus Main Input stereo, Main Output stereo (nessun sidechain)

- [ ] **Step 1: Scrivere i test:** `test_readModelConfig_valid` / `_missing_key` / `_malformed_json` / `_missing_file` (file temporanei); `test_state_roundtrip` (parametri e percorso non ASCII); `test_state_unknown_version_rejected`; `test_chain_nam_bypassed_is_transparent` (catena `ParallelComp` + `NamDrive` con `kNamBypass = 1` e senza modello: uscita uguale a quella del solo `ParallelComp`).
- [ ] **Step 2: Eseguire, verificare il fallimento.**
- [ ] **Step 3: Implementare `model_config` e lo stato.**
- [ ] **Step 4: Implementare `Processor`.** Catena: input gain → `ParallelComp` → (se `kNamBypass < 0.5`) `NamDrive` → output gain; `kBypass` passa il segnale. All'inizializzazione legge `doggystyle.json` in `pluginDirectory()` e, se la chiave c'è e lo stato non ha già un percorso, chiama `NamDrive::requestLoad`. `getLatencySamples()` riporta la latenza del NAM quando attivo.
- [ ] **Step 5: Implementare `Controller` e `plug.cpp`** (parametri con unità, nessuna GUI; factory come in `the-stripper/src/plug.cpp`, nome `"Doggystyle"`).
- [ ] **Step 6: Completare `CMakeLists.txt` e scrivere i docs:** `CONTROLS.md` (ogni parametro con range e default, esempio di `doggystyle.json`), `README.md` del plugin, chiusura della checklist in `CONCEPT.md`, riga di stato nel `README.md` radice.
- [ ] **Step 7: Build e test**
  Run: `cmake --build build --target doggystyle doggystyle_tests && ctest --test-dir build --output-on-failure && ls dist/Doggystyle.vst3`
  Expected: build senza errori, test PASS (anche quelli dello Stripper), bundle presente.
- [ ] **Step 8: Verifica manuale in Reaper e Cubase** (a cura dell'utente): inserire su un gruppo batteria; regolare Blend e Mix; con un `.nam` indicato in `doggystyle.json` attivare e disattivare il NAM.
- [ ] **Step 9: Commit**

```bash
git add src/doggystyle README.md
git commit -m "feat(doggystyle): add VST3 processor, controller, NAM stage and docs"
```
