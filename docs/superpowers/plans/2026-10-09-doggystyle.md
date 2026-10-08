# Doggystyle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Costruire Doggystyle, un plugin VST3 di compressione parallela aggressiva con due motori dedicati (Slap e Thump), detector HP, blend, mix col dry e NAM opzionale in coda.

**Architecture:** L'emulazione hardware di Dusk si estrae in `shared/hardware_emulation`. `SlapComp` e `ThumpComp` sono motori nuovi in `src/doggystyle/src/dsp/`; `ParallelComp` li esegue in parallelo e fa blend a potenza costante e mix col dry; il `Processor` aggiunge input/output gain e lo stadio `NamDrive` condiviso.

**Tech Stack:** C++20, VST3 SDK, `shared/hardware_emulation` (Dusk Audio, GPL-3.0), `shared/nam_drive` + `shared/gpu_nam`, nlohmann/json (già in `vendors/NeuralAmpModelerCore`), CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-10-09-doggystyle-design.md`

**Prerequisito:** il piano `2026-10-09-the-stripper-completion.md` deve aver completato i Task 1 e 2 (`shared/nam_drive` con caricamento asincrono).

## Global Constraints

- Namespace `FairyTapes::Doggystyle` (DSP in `...::DSP`); emulazione hardware in `FairyTapes::Shared::HardwareEmulation`.
- Solo topologia parallela; niente seriale, GUI custom o sidechain esterno. I motori `MultiComp*` dello Stripper non si toccano.
- Bundle `Doggystyle.vst3` in `dist/`; ingresso e uscita stereo.
- Blend a potenza costante: `gSlap = cos(blend·π/2)`, `gThump = sin(blend·π/2)`.
- Ogni ramo compensa il livello: segnale stazionario a -18 dBFS RMS in uscita entro ±3 dB dall'ingresso per qualunque Amount.
- Il detector HP filtra solo il rilevamento, mai l'audio.
- Il NAM sta dopo il Mix; la sua latenza si riporta con `getLatencySamples()`.
- `kStateVersion = 1`: versione, `kNumParams` valori `double`, percorso del modello (stringa UTF-8 con lunghezza).
- Licenza GPL-3.0.

## Review Focus

- Blocchi da 0 e da 1 campione: nessun crash né uscita corrotta.
- Ingresso mono (L = R): uscita identica sui due canali, nessun panning spurio.
- Ingresso a fondo scala e silenzio digitale prolungato: nessun NaN né denormali, nessun rumore residuo.
- Slap a ratio "All" con Amount massimo e attacco minimo: uscita limitata, nessuna instabilità del rilevatore.
- NAM bypassato o senza modello: il segnale dopo il Mix passa invariato.

---

### Task 1: Estrarre l'emulazione hardware in `shared/hardware_emulation`

**Files:**
- Create: `shared/hardware_emulation/CMakeLists.txt`
- Move: `src/the-stripper/src/dsp/hardware_emulation/*.h` → `shared/hardware_emulation/include/hardware_emulation/`
- Modify: `src/the-stripper/CMakeLists.txt`, `src/the-stripper/src/dsp/multicomp_engines.{h,cpp}`, `src/the-stripper/tests/test_params.cpp`

**Interfaces:**
- Produces: libreria `INTERFACE` `hardware_emulation` (include `PUBLIC` in `include/`); header `hardware_emulation/TransformerEmulation.h`, `TubeEmulation.h`, `HardwareProfiles.h`, `WaveshaperCurves.h` in `FairyTapes::Shared::HardwareEmulation`, con le API di oggi (`prepare(double sampleRate, int numChannels = 2)`, `setProfile`, `setEnabled`, `processSample(float input, int channel)`, `TubeEmulation::setTubeType/setDrive`, `HardwareProfiles::getFET1176()`).

- [ ] **Step 1: Verificare la baseline**
  Run: `cmake -S . -B build && cmake --build build --target the_stripper_tests && ctest --test-dir build -R the_stripper --output-on-failure`
  Expected: PASS.
- [ ] **Step 2: Spostare i file con `git mv`**, cambiare il namespace e creare la libreria `INTERFACE`.
- [ ] **Step 3: Aggiornare lo Stripper:** collegare `hardware_emulation` (plugin e test), correggere include e namespace in `multicomp_engines.{h,cpp}`, rimuovere la cartella dagli include locali.
- [ ] **Step 4: Rieseguire i test dello Stripper.** Expected: PASS identico alla baseline.
- [ ] **Step 5: Commit**

```bash
git add shared/hardware_emulation src/the-stripper
git commit -m "refactor: extract hardware emulation to shared/hardware_emulation"
```

---

### Task 2: Parametri, class ID, mapping e legge del blend

**Files:**
- Create: `src/doggystyle/src/params.h`, `src/doggystyle/src/cids.h`, `src/doggystyle/tests/test_doggystyle.cpp`, `src/doggystyle/CMakeLists.txt` (target `doggystyle_tests` per ora)
- Modify: `CMakeLists.txt` (aggiungere `add_subdirectory(src/doggystyle)`)

**Interfaces:**
- Produces, in `params.h`:
  - `enum ParamID : Steinberg::Vst::ParamID { kBypass = 0, kInput, kSlapAmount, kSlapRatio, kSlapAttack, kSlapRelease, kThumpAmount, kThumpAttack, kThumpRelease, kDetectorHp, kBlend, kMix, kNamBypass, kNamDrive, kOutput, kNumParams };`
  - `double gainDbFromNorm(double n)` (-12..+12 dB lineare); `double dbToLinear(double db)`
  - `int slapRatioIndexFromNorm(double n)` (5 passi: 0..4 → 4, 8, 12, 20, All); `double slapRatioFromIndex(int i)` (4, 8, 12, 20, 20); `bool slapRatioIsAll(int i)` (vero solo per 4)
  - `double slapAttackMsFromNorm(double n)` (0.02..0.8, log), `slapReleaseMsFromNorm` (20..1100, log), `thumpAttackMsFromNorm` (5..60, log), `thumpReleaseMsFromNorm` (40..800, log), `detectorHpHzFromNorm` (20..250, log)
  - `struct BlendGains { float slap; float thump; }; BlendGains blendGains(float blend);`
  - Default come da spec §5 (norm.): Input 0.5, SlapAmount 0.5, SlapRatio 0.5, SlapAttack 0.3, SlapRelease 0.35, ThumpAmount 0.4, ThumpAttack 0.4, ThumpRelease 0.5, DetectorHp 0.55, Blend 0.5, Mix 1.0, NamBypass 1.0, NamDrive 0.3, Output 0.5.
- `cids.h`: `kProcessorUID`, `kControllerUID` con FUID nuovi, diversi da quelli delle altre due.

- [ ] **Step 1: Scrivere i test** (macro di `the-stripper/tests/test_params.cpp`): `test_blendGains_endpoints` (0 → slap 1, thump 0; 1 → slap 0, thump 1); `test_blendGains_constant_power` (per 0, 0.25, 0.5, 0.75, 1: `slap² + thump²` = 1 entro 0.012); `test_gainDb_range`; `test_slap_ratio_steps` (indici 0..4 → 4, 8, 12, 20, 20; `slapRatioIsAll` vero solo per l'ultimo); `test_time_ranges_boundaries` (estremi di tutti i mapping a 0 e 1); `test_default_detector_hp_about_80hz` (`detectorHpHzFromNorm(default)` entro 78..82 Hz); `test_param_enum_count` (`kNumParams == 15`); `test_defaults_in_range`.
- [ ] **Step 2: Creare il CMake del plugin** (solo target di test), modellato su `the-stripper/CMakeLists.txt`.
- [ ] **Step 3: Eseguire, verificare il fallimento.**
  Run: `cmake -S . -B build && cmake --build build --target doggystyle_tests`
  Expected: errore di compilazione (`params.h` mancante).
- [ ] **Step 4: Implementare `params.h` e `cids.h`.** Mapping logaritmico: `min * pow(max/min, n)`.
- [ ] **Step 5: Eseguire i test.**
  Run: `cmake --build build --target doggystyle_tests && ctest --test-dir build -R doggystyle --output-on-failure`
  Expected: PASS.
- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/doggystyle
git commit -m "feat(doggystyle): add params, class IDs, mappings and blend law"
```

---

### Task 3: SlapComp e filtro del detector

**Files:**
- Create: `src/doggystyle/src/dsp/detector_hp.h`, `src/doggystyle/src/dsp/slap_comp.{h,cpp}`
- Modify: `src/doggystyle/tests/test_doggystyle.cpp`, `src/doggystyle/CMakeLists.txt` (collegare `hardware_emulation`, aggiungere `slap_comp.cpp`)

**Interfaces:**
- Consumes: `slapRatioFromIndex`, `slapRatioIsAll` (Task 2); `TransformerEmulation`, `HardwareProfiles::getFET1176()` (Task 1).
- Produces:
  - `class DetectorHp` in `detector_hp.h`: `void prepare(double sampleRate)`, `void setCutoffHz(float hz)`, `float process(float x, int channel)`, `void reset()` — biquad passa-alto a 12 dB/ott per canale
  - `class SlapComp` in `slap_comp.h`: `void prepare(double sampleRate)`; `void setParams(float amount, int ratioIndex, float attackMs, float releaseMs)`; `void setDetectorHpHz(float hz)`; `void process(float* left, float* right, int numSamples)` (in-place, 0 campioni = no-op); `void reset()`

- [ ] **Step 1: Scrivere i test** (48 kHz): `test_detectorHp_attenuates_below_cutoff` (sinusoide a 40 Hz con HP 250 Hz: ampiezza < 0.2 dell'ingresso; a 1 kHz > 0.95); `test_slap_higher_ratio_more_reduction` (rumore a -12 dBFS RMS: livello RMS in uscita con ratio indice 3 < indice 0, con compensazione di livello disattivata via parametro di test o confrontando la riduzione relativa); `test_slap_faster_attack_lower_peak` (clic ripetuti a 120 BPM: picco massimo con attacco 0.02 ms < picco con 0.8 ms); `test_slap_level_compensation` (rumore rosa a -18 dBFS RMS, Amount 0, 0.5, 1: RMS in uscita entro ±3 dB dall'ingresso); `test_slap_all_mode_is_bounded` (indice 4, Amount 1, attacco minimo, sinusoide a fondo scala per 5 s: |uscita| < 4.0, nessun NaN); `test_slap_detector_hp_reduces_lf_pumping` (sinusoide 40 Hz a -12 dBFS: con HP 250 Hz il livello in uscita è almeno 2 dB più alto che con HP 20 Hz); `test_slap_zero_and_one_sample_blocks`.
- [ ] **Step 2: Eseguire, verificare il fallimento.**
- [ ] **Step 3: Implementare `DetectorHp`** (biquad Butterworth a 12 dB/ott, coefficienti ricalcolati in `setCutoffHz`).
- [ ] **Step 4: Implementare `SlapComp`.** Input gain 0..+30 dB secondo Amount in un `TransformerEmulation` (profilo FET); rilevatore di picco sul segnale filtrato da `DetectorHp` (somma massima dei due canali); soglia fissa -12 dBFS; riduzione `over·(1 - 1/ratio)`; modalità All: ratio 20, attacco e rilascio dimezzati e drive del trasformatore d'uscita aumentato; smoothing attacco/rilascio esponenziale; compensazione di livello pari a una frazione dell'input gain, regolata fino a rispettare ±3 dB nel test.
- [ ] **Step 5: Eseguire i test.** Expected: PASS.
- [ ] **Step 6: Commit**

```bash
git add src/doggystyle
git commit -m "feat(doggystyle): add Slap FET-style compressor and detector high-pass"
```

---

### Task 4: ThumpComp

**Files:**
- Create: `src/doggystyle/src/dsp/thump_comp.{h,cpp}`
- Modify: `src/doggystyle/tests/test_doggystyle.cpp`, `src/doggystyle/CMakeLists.txt` (aggiungere `thump_comp.cpp`)

**Interfaces:**
- Consumes: `DetectorHp` (Task 3); `TubeEmulation` con `TubeType::Triode_12BH7` (Task 1).
- Produces: `class ThumpComp` in `thump_comp.h`: `void prepare(double sampleRate)`; `void setParams(float amount, float attackMs, float releaseMs)`; `void setDetectorHpHz(float hz)`; `void process(float* left, float* right, int numSamples)` (in-place, 0 campioni = no-op); `void reset()`

- [ ] **Step 1: Scrivere i test** (48 kHz): `test_thump_slow_attack_lets_transient_through` (clic ripetuti a 120 BPM: rapporto picco_uscita/picco_ingresso con attacco 60 ms maggiore che con 5 ms di almeno 1.5 dB); `test_thump_more_amount_more_reduction` (rumore a -12 dBFS: la riduzione relativa cresce con Amount 0.2 → 0.8); `test_thump_level_compensation` (rumore rosa a -18 dBFS RMS, Amount 0, 0.5, 1: RMS in uscita entro ±3 dB dall'ingresso); `test_thump_adds_harmonics` (sinusoide a 100 Hz a -12 dBFS con Amount 1: energia di seconda e terza armonica > -60 dBc, con Amount 0 trascurabile); `test_thump_detector_hp`; `test_thump_silence_stays_silent` (60 s di zeri → uscita esattamente 0); `test_thump_zero_and_one_sample_blocks`.
- [ ] **Step 2: Eseguire, verificare il fallimento.**
- [ ] **Step 3: Implementare `ThumpComp`.** Feedforward; rilevatore RMS (finestra 10 ms) combinato al picco (max pesato) sul segnale filtrato; soglia `-6 - 24·amount` dB; ratio 8:1 con ginocchio morbido di 6 dB; attacco e rilascio a due stadi (stadio veloce al 40%); stadio a valvola 12BH7 con drive `0.2 + 0.4·amount` dopo la compressione; compensazione di livello come per Slap.
- [ ] **Step 4: Eseguire i test.** Expected: PASS.
- [ ] **Step 5: Commit**

```bash
git add src/doggystyle
git commit -m "feat(doggystyle): add Thump heavy VCA compressor with tube stage"
```

---

### Task 5: ParallelComp

**Files:**
- Create: `src/doggystyle/src/dsp/parallel_comp.{h,cpp}`
- Modify: `src/doggystyle/tests/test_doggystyle.cpp`, `src/doggystyle/CMakeLists.txt`

**Interfaces:**
- Consumes: `SlapComp` (Task 3), `ThumpComp` (Task 4), `blendGains` (Task 2).
- Produces: in `parallel_comp.h`
  - `struct ParallelParams { float slapAmount; int slapRatioIndex; float slapAttackMs; float slapReleaseMs; float thumpAmount; float thumpAttackMs; float thumpReleaseMs; float detectorHpHz; float blend; float mix; };`
  - `class ParallelComp`: `void prepare(double sampleRate, int maxBlockSize)`; `void setParams(const ParallelParams&)`; `void process(float* left, float* right, int numSamples)` (in-place, 0 campioni = no-op; il dry per il Mix è l'ingresso del blocco); `void reset()`

- [ ] **Step 1: Scrivere i test:** `test_mix_zero_is_dry` (uscita identica all'ingresso entro 1e-6); `test_blend_zero_equals_slap_alone` e `test_blend_one_equals_thump_alone` (entro 1e-5 con gli stessi parametri e Mix 1); `test_slap_peak_lower_than_thump_on_clicks` (clic a 120 BPM, Amount 0.7 per entrambi, attacchi di default: picco con Blend 0 < picco con Blend 1); `test_mono_input_stays_identical_on_both_channels` (entro 1e-6); `test_amounts_zero_wet_close_to_dry` (stesso ingresso, Mix 1: livello RMS entro ±3 dB dal dry); `test_zero_and_one_sample_blocks`; `test_no_nan_on_silence_and_full_scale`.
- [ ] **Step 2: Eseguire, verificare il fallimento.**
- [ ] **Step 3: Implementare `ParallelComp`:** copia dell'ingresso in due coppie di buffer, un motore per ramo, `wet = gSlap·slap + gThump·thump`, uscita `dry·(1 - mix) + wet·mix`. Nessuna allocazione in `process`.
- [ ] **Step 4: Eseguire i test.** Expected: PASS.
- [ ] **Step 5: Commit**

```bash
git add src/doggystyle
git commit -m "feat(doggystyle): add parallel Slap/Thump chain with constant-power blend"
```

---

### Task 6: Plugin VST3, NAM, stato e docs

**Files:**
- Create: `src/doggystyle/src/processor.{h,cpp}`, `src/doggystyle/src/controller.{h,cpp}`, `src/doggystyle/src/plug.cpp`, `src/doggystyle/src/model_config.{h,cpp}`, `src/doggystyle/src/Info.plist`
- Create: `src/doggystyle/docs/CONTROLS.md`
- Modify: `src/doggystyle/CMakeLists.txt` (target `doggystyle` SHARED, output `Doggystyle.vst3` in `dist/` come The Stripper; collega `hardware_emulation`, `nam_drive`), `src/doggystyle/README.md`, `src/doggystyle/docs/CONCEPT.md` (checklist chiusa, motori Slap/Thump), `README.md` radice (stato)
- Test: `src/doggystyle/tests/test_doggystyle.cpp`

**Interfaces:**
- Consumes: `ParallelComp`/`ParallelParams` (Task 5), `NamDrive` (`shared/nam_drive`), `params.h` (Task 2).
- Produces:
  - `std::optional<std::filesystem::path> readModelConfig(const std::filesystem::path& configFile)` in `model_config.h` — legge la chiave `"nam_model"` di `doggystyle.json`; assente, malformato o file mancante → `std::nullopt`
  - `std::filesystem::path pluginDirectory()` — cartella del modulo (`dladdr` su Linux/macOS, `GetModuleHandleExW` su Windows)
  - `struct PluginState { double params[kNumParams]; std::string modelPath; }` con `readState`/`writeState` su `IBStream` (stesso schema del Task 3 dello Stripper)
  - Classi `Processor` e `Controller` con bus Main Input stereo e Main Output stereo (nessun sidechain)

- [ ] **Step 1: Scrivere i test:** `test_readModelConfig_valid` / `_missing_key` / `_malformed_json` / `_missing_file` (file temporanei); `test_state_roundtrip` (parametri e percorso non ASCII); `test_state_unknown_version_rejected`; `test_chain_nam_bypassed_is_transparent` (catena `ParallelComp` + `NamDrive` con `kNamBypass = 1` e nessun modello: uscita uguale a quella del solo `ParallelComp`); `test_params_to_parallel_params_mapping` (funzione libera `toParallelParams(const double* params)` con valori di default e agli estremi).
- [ ] **Step 2: Eseguire, verificare il fallimento.**
- [ ] **Step 3: Implementare `model_config`, lo stato e `toParallelParams`.**
- [ ] **Step 4: Implementare `Processor`.** Catena: input gain → `ParallelComp` → (se `kNamBypass < 0.5`) `NamDrive` → output gain; `kBypass` passa il segnale. All'inizializzazione legge `doggystyle.json` in `pluginDirectory()` e, se la chiave c'è e lo stato non ha già un percorso, chiama `NamDrive::requestLoad`. `getLatencySamples()` riporta la latenza del NAM quando attivo.
- [ ] **Step 5: Implementare `Controller` e `plug.cpp`** (parametri con unità, `StringListParameter` per Slap Ratio con "4", "8", "12", "20", "All", nessuna GUI; factory come in `the-stripper/src/plug.cpp`, nome `"Doggystyle"`).
- [ ] **Step 6: Completare `CMakeLists.txt` e scrivere i docs:** `CONTROLS.md` (ogni parametro con range e default, esempio di `doggystyle.json`), `README.md` del plugin, `CONCEPT.md` aggiornato, riga di stato nel `README.md` radice.
- [ ] **Step 7: Build e test**
  Run: `cmake --build build --target doggystyle doggystyle_tests && ctest --test-dir build --output-on-failure && ls dist/Doggystyle.vst3`
  Expected: build senza errori, test PASS (anche quelli dello Stripper), bundle presente.
- [ ] **Step 8: Verifica manuale in Reaper e Cubase** (a cura dell'utente): inserire su un gruppo batteria; provare Slap Ratio "All" con Blend 0, poi Blend verso Thump; regolare Detector HP; con un `.nam` indicato in `doggystyle.json` attivare e disattivare il NAM.
- [ ] **Step 9: Commit**

```bash
git add src/doggystyle README.md
git commit -m "feat(doggystyle): add VST3 processor, controller, NAM stage and docs"
```
