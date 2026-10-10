# Pillow Talk Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Costruire Pillow Talk, un plugin VST3 tape echo il cui livello di mandata si abbassa mentre la voce canta e si apre nelle pause.

**Architecture:** Plugin VST3 in `src/pillow-talk/`, stessa struttura di The Stripper. Tre unità DSP indipendenti e testabili (`Ducker`, `TapeEcho`, helper dei parametri) sono collegate dal `Processor`. Il `Ducker` produce un gain per campione che il `Processor` applica all'ingresso del `TapeEcho`; il mix wet/dry è in parallelo.

**Tech Stack:** C++20, VST3 SDK (`vendors/vst3sdk`), CMake/CTest. Nessuna dipendenza da JUCE, NAM o CUDA.

**Spec:** `docs/superpowers/specs/2026-10-08-pillow-talk-design.md`

## Global Constraints

- Namespace `FairyTapes::PillowTalk`; DSP in `FairyTapes::PillowTalk::DSP`.
- DSP senza JUCE; solo VST3 SDK e libreria standard. Nessun codice condiviso con altri plugin (niente include da `the-stripper`).
- Licenza GPL-3.0, come il resto della suite. Il riferimento `vendors/dusk-audio-plugins/plugins/tape-echo` si consulta ma non si include.
- Bundle di output `Pillow Talk.vst3` in `dist/` (stesso schema per piattaforma di The Stripper).
- Ingresso principale stereo + sidechain stereo opzionale; uscita stereo.
- Default BPM 120 se l'host non lo fornisce.
- Feedback massimo 0.95. Saturazione con `tanh`, oversampling 2x solo su quello stadio.
- Test come in `src/the-stripper/tests/test_params.cpp`: eseguibile unico con macro `ASSERT_*` / `RUN_TEST`, registrato in CTest.

## Review Focus

- Blocco di 0 campioni e blocco di 1 campione: nessun crash, nessuna uscita corrotta.
- Ingresso di silenzio digitale prolungato: nessun NaN né denormali, nessun rumore dal feedback.
- Feedback 0.95 con saturazione massima e ingresso forte: l'uscita resta limitata (nessuna autoscillazione esplosiva).
- Sidechain abilitato ma bus non attivo (puntatori nulli o 0 canali): il rilevatore ripiega sull'ingresso principale senza crash.
- Cambio di sample rate (44.1 → 192 kHz) tra due `setActive`: buffer ricalcolati, nessun click.

---

### Task 1: Parametri e helper puri

**Files:**
- Create: `src/pillow-talk/src/params.h`, `src/pillow-talk/src/cids.h`, `src/pillow-talk/tests/test_pillow_talk.cpp`
- Create: `src/pillow-talk/CMakeLists.txt` (solo target `pillow_talk_tests` per ora)
- Modify: `CMakeLists.txt` (aggiungere `add_subdirectory(src/pillow-talk)`)

**Interfaces:**
- Produces, in `params.h`:
  - `enum ParamID : Steinberg::Vst::ParamID { kBypass = 0, kTimeSync, kTimeMs, kTimeNote, kFeedback, kTone, kSaturation, kWowFlutter, kMix, kThreshold, kDuckDepth, kHoldSync, kHoldMs, kHoldNote, kRelease, kSidechain, kNumParams };`
  - `double noteToMs(int noteIndex, double bpm)` — indici 0..3 = 1/16, 1/8, 1/4, 1/2; se `bpm <= 0` usa 120.
  - `double timeMsFromNorm(double n)` (20..1200 ms, log), `holdMsFromNorm(double n)` (50..1000 ms, log), `releaseMsFromNorm(double n)` (50..2000 ms, log).
  - `double feedbackFromNorm(double n)` (0..0.95 lineare), `double toneHzFromNorm(double n)` (800..12000 Hz, log).
  - `double thresholdDbFromNorm(double n)` (-60..0 dB lineare), `double duckDepthDbFromNorm(double n)` (0..60 dB lineare; **a n = 1.0 significa muto totale**, vedi `kDuckMuteDb`).
  - `constexpr double kDuckMuteDb = 60.0;`
  - Costanti `kDefault*` per ogni parametro: Time 0.5, Feedback 0.4, Tone 0.6, Saturation 0.3, WowFlutter 0.2, Mix 0.3, Threshold 0.42 (≈ -35 dB), Depth 0.6 (≈ 36 dB), Hold 0.5, Release 0.5, TimeSync 0, HoldSync 0, Sidechain 0.
- `cids.h`: `kProcessorUID`, `kControllerUID` con FUID nuovi e diversi da quelli dello Stripper.

- [x] **Step 1: Scrivere i test (falliscono perché `params.h` non esiste)** in `tests/test_pillow_talk.cpp` copiando le macro di `the-stripper/tests/test_params.cpp`: `test_noteToMs_120bpm` (0→125, 1→250, 2→500, 3→1000), `test_noteToMs_bpm_zero_uses_120`, `test_ranges_boundaries` (estremi di ogni `*FromNorm`), `test_duckDepth_full_is_mute_constant` (`duckDepthDbFromNorm(1.0) == kDuckMuteDb`), `test_param_enum_count` (`kNumParams == 16`), `test_feedback_never_exceeds_095`.
- [x] **Step 2: Creare `CMakeLists.txt` del plugin** con il solo eseguibile `pillow_talk_tests` + `add_test`, modellato su `the-stripper/CMakeLists.txt` (C++20, include di `src/` e del VST3 SDK, path `VST3_SDK_PATH`). Aggiungere `add_subdirectory(src/pillow-talk)` alla radice.
- [x] **Step 3: Eseguire, verificare il fallimento**
  Run: `cmake -S . -B build && cmake --build build --target pillow_talk_tests`
  Expected: errore di compilazione (`params.h` mancante).
- [x] **Step 4: Implementare `params.h` e `cids.h`** con le firme sopra. Mapping logaritmico: `min * pow(max/min, n)`.
- [x] **Step 5: Eseguire i test**
  Run: `cmake --build build --target pillow_talk_tests && ctest --test-dir build -R pillow_talk --output-on-failure`
  Expected: PASS.
- [x] **Step 6: Commit**

```bash
git add CMakeLists.txt src/pillow-talk
git commit -m "feat(pillow-talk): add params, class IDs and test target"
```

---

### Task 2: Ducker (rilevatore e gain di mandata)

**Files:**
- Create: `src/pillow-talk/src/dsp/ducker.h`, `src/pillow-talk/src/dsp/ducker.cpp`
- Modify: `src/pillow-talk/tests/test_pillow_talk.cpp`, `src/pillow-talk/CMakeLists.txt` (aggiungere `ducker.cpp` al target di test)

**Interfaces:**
- Consumes: `kDuckMuteDb` da `params.h`.
- Produces: `class Ducker` in `ducker.h` con
  - `void prepare(double sampleRate)` — ricalcola i coefficienti e chiama `reset()`
  - `void setParams(float thresholdDb, float depthDb, float holdMs, float releaseMs)` — `depthDb >= kDuckMuteDb` significa gain 0
  - `float processSample(float scL, float scR)` — ritorna il gain di mandata in `[0, 1]`
  - `void reset()`

- [x] **Step 1: Scrivere i test** (sample rate 48000, soglia -40 dB, depth 30 dB, hold 200 ms, release 300 ms): `test_ducker_silence_gain_is_unity`; `test_ducker_burst_ducks_to_depth` (burst di sinusoide a 1 kHz di ampiezza 0.5 e 300 ms → gain finale ≈ 10^(-30/20) entro 0.02); `test_ducker_holds_after_burst` (a 100 ms dalla fine del burst il gain è ancora ridotto); `test_ducker_reopens_after_hold_and_release` (a hold + 3×release dalla fine il gain > 0.95); `test_ducker_full_depth_is_mute` (depth = `kDuckMuteDb` → gain < 1e-4 durante il burst); `test_ducker_below_threshold_ignored` (ampiezza 0.001 non abbassa il gain); `test_ducker_ignores_sub_80hz_rumble` (sinusoide 40 Hz ampiezza 0.5 non abbassa il gain di oltre 3 dB).
- [x] **Step 2: Eseguire, verificare il fallimento** (`ducker.h` mancante).
- [x] **Step 3: Implementare `Ducker`.** Rilevatore: somma mono dei due canali → passa-alto 120 Hz e passa-basso 5 kHz (biquad Butterworth locali al file) → envelope peak con attacco 2 ms e rilascio 40 ms. Macchina a stati: sopra soglia → `active`, contatore hold riazzerato; sotto soglia il gain target resta ridotto finché il contatore hold non scade; poi il target torna a 1 con rilascio esponenziale a costante di tempo `releaseMs`. Il gain finale passa in uno smoother a 3 ms per evitare click.
- [x] **Step 4: Eseguire i test** (comando del Task 1). Expected: PASS.
- [x] **Step 5: Commit**

```bash
git add src/pillow-talk
git commit -m "feat(pillow-talk): add vocal-aware ducker with hold and release"
```

---

### Task 3: TapeEcho

**Files:**
- Create: `src/pillow-talk/src/dsp/tape_echo.h`, `src/pillow-talk/src/dsp/tape_echo.cpp`
- Modify: `src/pillow-talk/tests/test_pillow_talk.cpp`, `src/pillow-talk/CMakeLists.txt`

**Interfaces:**
- Produces: `class TapeEcho` in `tape_echo.h` con
  - `void prepare(double sampleRate, int maxBlockSize)` — alloca la linea per almeno 2 s; chiama `reset()`
  - `void setParams(float timeMs, float feedback, float toneHz, float saturation, float wowFlutter)` — `feedback` già in 0..0.95
  - `void process(float* L, float* R, const float* sendGain, int numSamples)` — **sostituisce** `L`/`R` con il solo segnale wet; `sendGain[i]` moltiplica l'ingresso prima della linea; `numSamples == 0` è un no-op
  - `void reset()`

- [x] **Step 1: Scrivere i test** (48 kHz, wowFlutter 0, saturation 0, tone 12000, `sendGain` tutto 1): `test_echo_first_repeat_at_time` (impulso a campione 0, Time 100 ms → picco massimo in 4800 ± 2 campioni); `test_echo_feedback_decays` (feedback 0.5 → ogni ripetizione più bassa della precedente di un fattore tra 0.3 e 0.6); `test_echo_zero_send_gain_is_silent` (sendGain 0 → uscita < 1e-6); `test_echo_send_gain_not_applied_to_tail` (impulso con gain 1 poi gain 0: la ripetizione già in linea esce comunque); `test_echo_tone_darkens` (rumore bianco con tone 800 Hz ha meno energia sopra 6 kHz che con tone 12000); `test_echo_max_feedback_is_bounded` (feedback 0.95, saturation 1.0, ingresso sinusoidale ampiezza 1.0 per 10 s → |uscita| < 4.0 e nessun NaN); `test_echo_silence_stays_silent` (60 s di zeri → tutti i campioni esattamente 0 dopo che la linea si è svuotata, senza denormali); `test_echo_zero_and_one_sample_blocks`; `test_echo_time_change_has_no_click` (Time da 100 a 400 ms a metà di una sinusoide: massima differenza tra campioni consecutivi < 0.5).
- [x] **Step 2: Eseguire, verificare il fallimento.**
- [x] **Step 3: Implementare `TapeEcho`** come da spec §5: linea con interpolazione a 4 punti, tempo effettivo = Time smussato (glide ~120 ms) + modulazione wow (LFO ~0.6 Hz) e flutter (LFO ~7 Hz più rumore filtrato) con profondità proporzionale a `wowFlutter`; nel loop di feedback HP 100 Hz, LP a `toneHz`, `tanh` con bias leggero a oversampling 2x (filtro half-band FIR corto, stato in `reset()`); flush dei denormali all'uscita del loop.
- [x] **Step 4: Eseguire i test.** Expected: PASS.
- [x] **Step 5: Commit**

```bash
git add src/pillow-talk
git commit -m "feat(pillow-talk): add tape echo with wow/flutter and saturated feedback"
```

---

### Task 4: Plugin VST3 (processor, controller, factory) e documentazione

**Files:**
- Create: `src/pillow-talk/src/processor.{h,cpp}`, `src/pillow-talk/src/controller.{h,cpp}`, `src/pillow-talk/src/plug.cpp`, `src/pillow-talk/src/Info.plist`
- Create: `src/pillow-talk/README.md`, `src/pillow-talk/docs/CONTROLS.md`
- Modify: `src/pillow-talk/CMakeLists.txt` (target `pillow_talk` SHARED, output `Pillow Talk.vst3` in `dist/`, come The Stripper), `README.md` radice (terzo plugin), `src/pillow-talk/tests/test_pillow_talk.cpp`

**Interfaces:**
- Consumes: `params.h` (Task 1), `Ducker` (Task 2), `TapeEcho` (Task 3).
- Produces: classi `Processor` e `Controller` in `FairyTapes::PillowTalk`, factory identica a `the-stripper/src/plug.cpp` con nome `"Pillow Talk"`. Stato versionato (`kStateVersion = 1`): versione + `kNumParams` valori `double`.

- [x] **Step 1: Scrivere i test** per la parte non legata all'SDK, sfruttando le unità già testate: `test_chain_sidechain_ducks_echo` (Ducker + TapeEcho + mix come da formula del Processor: con voce nel sidechain il wet è più basso che con sidechain silenzioso) e `test_state_roundtrip` (un helper puro `serializeState(const double*, std::vector<uint8_t>&)` / `deserializeState` in `processor.h`, ripristina tutti i valori e scarta versioni sconosciute).
- [x] **Step 2: Eseguire, verificare il fallimento.**
- [x] **Step 3: Implementare `Processor`.** Bus: `addAudioInput("Main Input", kStereo)`, `addAudioInput("Sidechain", kStereo)` (inattivo di default), `addAudioOutput("Main Output", kStereo)`. In `process`: se `kSidechain` è attivo e il bus è attivo con puntatori validi, il rilevatore legge il sidechain; altrimenti legge l'ingresso principale. BPM da `processContext->tempo` se `kTempoValid`, altrimenti 120. Per ogni campione: `gain = ducker.processSample(...)`; wet = `TapeEcho` con `sendGain = gain`; uscita = `dry*(1-mix) + wet*mix`. `kBypass` passa il dry. Parametri smussati dove cambiano a salti.
- [x] **Step 4: Implementare `Controller`** con `StringListParameter` per `kTimeNote`/`kHoldNote` (1/16, 1/8, 1/4, 1/2), parametri a due stati per Sync/Sidechain/Bypass, e `getParamStringByValue` con unità (ms, dB, %). Nessuna GUI custom, come The Stripper.
- [x] **Step 5: Completare `CMakeLists.txt` e scrivere `README.md` e `docs/CONTROLS.md`** (ogni parametro con range e default; nota su Reaper: sidechain sui canali 3/4; su Cubase: attivare il sidechain dal plugin). Aggiornare il `README.md` radice.
- [x] **Step 6: Build completa e test**
  Run: `cmake --build build --target pillow_talk pillow_talk_tests && ctest --test-dir build -R pillow_talk --output-on-failure && ls "dist/Pillow Talk.vst3"`
  Expected: build senza errori, test PASS, bundle presente.
- [ ] **Step 7: Verifica manuale in Reaper e Cubase** (a cura dell'utente): inserire come insert sulla voce, poi su un bus di ritorno con sidechain dalla voce; controllare che il delay si abbassi cantando e si apra nelle pause.
- [x] **Step 8: Commit**

```bash
git add src/pillow-talk README.md
git commit -m "feat(pillow-talk): add VST3 processor, controller and docs"
```
