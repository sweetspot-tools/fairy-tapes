# Deep Throat Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Costruire Deep Throat, un plugin VST3 channel strip per voci (Rider → Breath control → EQ correttivo → De-esser → Compressore stile Distressor → Tone) con tre modalità: Speak, Sing, Puke.

**Architecture:** Motore puro senza SDK in `src/deep-throat/src/` (`engine.{h,cpp}` più blocchi in `dsp/`), come Pillow Talk. Un solo ritardo di look-ahead (80 ms) condiviso da Rider e Breath control, latenza riportata all'host. Il rilevatore di respiri sta dietro l'interfaccia `BreathDetector`: due implementazioni (spettrale per Speak/Sing, a livello per Puke), sostituibili in futuro da una rete RTNeural.

**Tech Stack:** C++20, VST3 SDK, CMake/CTest, FFT radix-2 nostra (nessuna nuova dipendenza). Nessuna GUI custom in questo piano (lista parametri dell'host).

**Spec:** `docs/superpowers/specs/2026-10-10-deep-throat-design.md`

## Global Constraints

- Namespace `FairyTapes::DeepThroat` (DSP in `...::DSP`). Bundle `Deep Throat.vst3` in `dist/`; ingresso e uscita stereo.
- Catena fissa: Rider → Breath control → EQ correttivo → De-esser → Compressore → Tone, poi Output.
- Modalità `Speak / Sing / Puke` (parametro `Mode`, 3 scelte).
- Il Breath control non chiude mai del tutto: riduzione massima 12 dB.
- Il Rider si basa solo sul livello, mai sul pitch.
- In Puke il rilevatore di respiri non usa analisi spettrale: un respiro è un segmento almeno 20 dB sotto il livello medio della frase; un'inalazione forte tra due urla non viene mai tagliata.
- Rilevamento e controlli sono linkati stereo (stesso guadagno su L e R).
- Nessuna allocazione nel thread audio (`process`).
- Test: harness `RUN_TEST`/`ASSERT_TRUE` come `src/pillow-talk/tests/test_pillow_talk.cpp`; ogni comportamento nasce con un test che prima fallisce.
- `kStateVersion = 1`: versione `int32` little endian poi `kNumParams` `double` normalizzati.
- Testo visibile in inglese; identificatori e commenti in inglese; licenza e CIDs unici (non riusare quelli degli altri plugin).
- Build fuori dal repo, es. `/tmp/vc-build`: `cmake -S . -B /tmp/vc-build -DCMAKE_BUILD_TYPE=Release -DGPU_NAM_DISABLE_CUDA=ON`, poi `cmake --build /tmp/vc-build --target deep_throat_tests -j2`; test con `ctest --test-dir /tmp/vc-build -R deep_throat --output-on-failure`.

## Review Focus

- Blocchi da 0 e da 1 campione: nessun crash né uscita corrotta (Task 10).
- Ingresso mono (L = R): uscita identica sui due canali (Task 10).
- Silenzio digitale prolungato e ingresso a fondo scala: nessun NaN, nessun denormale, nessun rumore residuo (Task 10).
- Cambio di modalità, di sample rate (44.1-192 kHz) e parametri estremi a metà flusso: nessun click grave, nessun valore non finito (Task 9, 10).
- Puke: growl continuo (rumore forte) e inalazione forte tra due urla non vengono abbassati (Task 4).

---

### Task 1: Scaffold del plugin, parametri e stato

**Files:**
- Create: `src/deep-throat/CMakeLists.txt`, `src/deep-throat/src/{cids.h,params.h,state.h,plug.cpp,processor.{h,cpp},controller.{h,cpp},Info.plist}`, `src/deep-throat/tests/{test_deep_throat.cpp,test_factory.cpp}`
- Modify: `CMakeLists.txt` (radice): `add_subdirectory(src/deep-throat)` dopo Pillow Talk.

**Interfaces:**
- Produces: `enum ParamID { kBypass=0, kMode, kInput, kRiderTarget, kRiderRange, kBreathAmount, kDeEssAmount, kDeEssListen, kCompAmount, kTone, kOutput, kNumParams }`; `enum class Mode { Speak, Sing, Puke }`; `Mode modeFromNorm(double n)` (3 gradini come una `StringListParameter`); `double inputDbFromNorm(double)` e `outputDbFromNorm(double)` (-12..+12 dB); `double riderTargetDbFromNorm(double)` (-30..-10 dBFS); default normalizzati `kDefault*` (Mode = Speak, Rider Target = 0.5, Rider Range = 0.5, Breath Amount = 0.5, DeEss Amount = 0.5, Comp Amount = 0.5, Tone = 0.5, Input/Output = 0.5, Listen = 0); `serializeState(const double*, std::vector<uint8_t>&)`, `bool deserializeState(const uint8_t*, size_t, double*)` (false su versione ignota, dati corti o valori non finiti; valori ripristinati clampati a [0,1]).

- [ ] **Step 1: Scrivere i test** in `test_deep_throat.cpp` (harness copiato da Pillow Talk): `test_param_enum_count` (`kNumParams == 11`), `test_mode_from_norm_steps` (0.0→Speak, 0.5→Sing, 1.0→Puke), `test_io_gain_ranges` (norma 0→-12 dB, 1→+12 dB), `test_state_roundtrip`, `test_state_rejects_bad_version_and_nan`.
- [ ] **Step 2: Eseguire e vedere fallire** (la build non esiste ancora): `cmake --build /tmp/vc-build --target deep_throat_tests -j2` → errore di target.
- [ ] **Step 3: Implementare** `params.h` e `state.h` sul modello di `src/pillow-talk/src/{params,state}.h`. CMake sul modello di `src/pillow-talk/CMakeLists.txt` (target `deep_throat`, `deep_throat_tests`, `deep_throat_factory_test`; senza `fairy_ui`). `plug.cpp`, `processor`, `controller` sul modello di Pillow Talk: processor stereo che copia ingresso su uscita applicando Input/Output, controller che registra i parametri senza `createView`, due FUID nuovi in `cids.h` generati a caso.
- [ ] **Step 4: Eseguire i test** → PASS; poi `cmake --build /tmp/vc-build --target deep_throat -j2` e `ctest -R deep_throat_factory_test` (PASS, o SKIP 77 se il bundle manca).
- [ ] **Step 5: Commit** (solo i file del plugin, per nome): `feat(deep-throat): plugin scaffold, parameters and state`.

---

### Task 2: Rider

**Files:**
- Create: `src/deep-throat/src/dsp/rider.{h,cpp}`
- Modify: `tests/test_deep_throat.cpp`, `CMakeLists.txt` (aggiungere il sorgente a plugin e test)

**Interfaces:**
- Produces: `class DSP::Rider { void prepare(double sampleRate); void reset(); void setTargetDb(float); void setMaxRangeDb(float); void setSlewDbPerSec(float); float processFrame(float levelDb, bool freeze); }` — `levelDb` è il livello RMS (finestra 200 ms) calcolato dal chiamante; il ritorno è il guadagno lineare da applicare. `processFrame` è chiamato a ogni campione.
- Produces: `class DSP::LevelMeter { void prepare(double sampleRate, float windowMs); void reset(); float process(float monoSample); /* RMS in dB, floor -120 */ }`.

- [ ] **Step 1: Scrivere i test:** `test_rider_raises_quiet_to_target` (livello costante -30 dB, target -20, range 6, slew 8 dB/s: dopo 3 s il guadagno è `10^(6/20)` ±0.5 dB, cioè limitato dal range), `test_rider_lowers_loud` (livello -10 dB → guadagno -6 dB, limitato dal range), `test_rider_slew_limited` (salto del livello: la variazione del guadagno in 1 s non supera lo slew + 0.1 dB), `test_rider_freeze_holds_gain` (con `freeze=true` il guadagno non cambia anche se il livello cambia), `test_level_meter_sine` (seno a -20 dBFS di picco → RMS -23 dB ±0.5).
- [ ] **Step 2: Eseguire e vedere fallire** (simboli non definiti).
- [ ] **Step 3: Implementare** `Rider` e `LevelMeter`: errore dB = target − livello, clampato a ±range, inseguito con limite di pendenza in dB/s; con livello sotto -60 dB l'errore è ignorato (come freeze).
- [ ] **Step 4: Eseguire i test** → PASS.
- [ ] **Step 5: Commit:** `feat(deep-throat): level rider`.

---

### Task 3: BreathDetector spettrale (Speak/Sing)

**Files:**
- Create: `src/deep-throat/src/dsp/fft.h`, `src/deep-throat/src/dsp/breath_detector.h`, `src/deep-throat/src/dsp/spectral_breath_detector.{h,cpp}`
- Modify: `tests/test_deep_throat.cpp`, `CMakeLists.txt`

**Interfaces:**
- Produces: `class DSP::BreathDetector { virtual ~BreathDetector(); virtual void prepare(double sampleRate) = 0; virtual void reset() = 0; virtual float process(float monoSample) = 0; /* probabilità di respiro in [0,1], aggiornata a ogni campione */ virtual int latencySamples() const = 0; }`.
- Produces: `class DSP::SpectralBreathDetector : public BreathDetector` con costanti in `struct SpectralTuning { float flatnessMin = 0.30f, centroidMinHz = 2000.f, harmonicityMax = 0.30f, belowPhraseDb = 10.f, silenceDb = -60.f, onThreshold = 0.6f, offThreshold = 0.4f, minDurationMs = 60.f; }`. Frame di `nextPow2(20 ms · sr)` campioni, hop = metà frame. Per frame: flatness spettrale 300-8000 Hz, centroide, picco normalizzato di autocorrelazione per pitch 60-1000 Hz (harmonicity), livello rispetto a un inseguitore del livello di frase (attacco veloce, rilascio 1.5 s). Respiro = tutte le condizioni vere, con isteresi e durata minima.
- Produces: `fft.h`: `void DSP::fftPowerSpectrum(const float* in, int n /*potenza di 2*/, float* outPower /* n/2+1 */)`.

- [ ] **Step 1: Scrivere i test** (segnali sintetici a 48 kHz, rumore da generatore deterministico): `test_fft_peak_at_bin` (seno a bin esatto), `test_breath_detects_noise_between_phrases` (dente di sega 200 Hz a -18 dB per 1 s, poi rumore passa-alto >2 kHz a -35 dB per 300 ms, poi dente di sega: probabilità massima > 0.6 durante il rumore e < 0.2 durante le frasi), `test_breath_ignores_quiet_low_voice` (dente di sega 90 Hz a -30 dB dopo una frase a -18 dB: probabilità < 0.2 per tutta la durata), `test_breath_ignores_short_click` (rumore di 20 ms: prob < 0.4), `test_breath_silence_is_zero`.
- [ ] **Step 2: Eseguire e vedere fallire.**
- [ ] **Step 3: Implementare** FFT radix-2 in-place con finestra di Hann e `SpectralBreathDetector` come descritto; `latencySamples()` = un frame.
- [ ] **Step 4: Eseguire i test** → PASS.
- [ ] **Step 5: Commit:** `feat(deep-throat): spectral breath detector`.

---

### Task 4: BreathDetector a livello (Puke)

**Files:**
- Create: `src/deep-throat/src/dsp/level_breath_detector.{h,cpp}`
- Modify: `tests/test_deep_throat.cpp`, `CMakeLists.txt`

**Interfaces:**
- Consumes: `DSP::BreathDetector` (Task 3), `DSP::LevelMeter` (Task 2).
- Produces: `class DSP::LevelBreathDetector : public BreathDetector` con `struct LevelTuning { float belowPhraseDb = 20.f; float contextMs = 300.f; float minDurationMs = 80.f; float silenceDb = -60.f; }`. Respiro = livello (RMS 25 ms) almeno `belowPhraseDb` sotto l'inseguitore del livello di frase (rilascio 2 s) e sopra `silenceDb`, con voce (livello entro 15 dB dalla frase) negli ultimi o nei prossimi `contextMs` (la parte "prossimi" è coperta dal look-ahead del chiamante). Nessuna FFT.

- [ ] **Step 1: Scrivere i test:** `test_puke_breath_after_scream` (scream = rumore a banda larga a -10 dB per 1 s, poi rumore a -35 dB per 400 ms, poi scream: prob > 0.6 nella parte a -35 dB), `test_puke_growl_not_breath` (rumore continuo a -12 dB per 3 s: prob < 0.1 sempre), `test_puke_loud_gasp_not_cut` (scream -10 dB, inalazione a -14 dB per 300 ms, scream: prob < 0.1), `test_puke_silence_is_zero`.
- [ ] **Step 2: Eseguire e vedere fallire.**
- [ ] **Step 3: Implementare** `LevelBreathDetector`.
- [ ] **Step 4: Eseguire i test** → PASS.
- [ ] **Step 5: Commit:** `feat(deep-throat): level-based breath detector for extreme vocals`.

---

### Task 5: BreathReducer

**Files:**
- Create: `src/deep-throat/src/dsp/breath_reducer.{h,cpp}`
- Modify: `tests/test_deep_throat.cpp`, `CMakeLists.txt`

**Interfaces:**
- Produces: `class DSP::BreathReducer { void prepare(double sampleRate); void reset(); void setAmount(float amount01); /* 0..1 → 0..12 dB di riduzione massima */ float process(float breathProb); /* guadagno lineare, attacco 5 ms, rilascio 30 ms */ }`.

- [ ] **Step 1: Scrivere i test:** `test_reducer_amount_zero_is_unity`, `test_reducer_full_amount_is_minus_12_db` (prob 1 costante: guadagno → `10^(-12/20)` ±0.3 dB, mai sotto), `test_reducer_scales_with_prob` (prob 0.5 → metà in dB ±1 dB), `test_reducer_smooth` (salto prob 0→1: nessuna variazione di guadagno maggiore di 0.2 in un campione).
- [ ] **Step 2: Eseguire e vedere fallire.**
- [ ] **Step 3: Implementare** `BreathReducer`: guadagno target in dB = `-prob · amount · 12`, inseguito con smoothing a due costanti.
- [ ] **Step 4: Eseguire i test** → PASS.
- [ ] **Step 5: Commit:** `feat(deep-throat): breath reducer`.

---

### Task 6: EQ correttivo

**Files:**
- Create: `src/deep-throat/src/dsp/biquad.h`, `src/deep-throat/src/dsp/corrective_eq.{h,cpp}`
- Modify: `tests/test_deep_throat.cpp`, `CMakeLists.txt`

**Interfaces:**
- Produces: `struct DSP::Biquad` con `setHighPass(double sr, double hz, double q)`, `setPeaking(double sr, double hz, double q, double gainDb)`, `setHighShelf(double sr, double hz, double gainDb)`, `float process(float)`, `reset()`.
- Produces: `class DSP::CorrectiveEq { void prepare(double sampleRate, int channels); void reset(); void setMode(Mode); float process(int channel, float x); }`. HPF: Speak 80 Hz, Sing 70 Hz, Puke 60 Hz (12 dB/ott); taglio del fango a 300 Hz, Q 1: -2, -3, -4 dB (Speak, Sing, Puke). Notch dinamico: un taglio stretto (Q 8) che segue il picco più prominente tra 1 e 6 kHz (prominenza > 8 dB sui vicini, analisi `fftPowerSpectrum` ogni 50 ms su mono), profondità massima 6 dB, rilascio di 200 ms; fuori soglia = 0 dB.

- [ ] **Step 1: Scrivere i test:** `test_eq_hpf_attenuates_rumble` (seno 30 Hz attenuato di almeno 20 dB in Speak), `test_eq_mud_cut_at_300` (seno 300 Hz a -2 dB ±0.5 in Speak, -4 dB ±0.5 in Puke), `test_eq_passband_flat` (seno 1 kHz entro ±0.5 dB), `test_eq_notch_cuts_resonance` (rumore bianco più un seno a 3 kHz con +20 dB: dopo 1 s il seno è calato di almeno 4 dB), `test_eq_notch_idle_on_clean_signal` (rumore bianco: variazione del livello totale < 0.5 dB).
- [ ] **Step 2: Eseguire e vedere fallire.**
- [ ] **Step 3: Implementare** `Biquad` (RBJ cookbook) e `CorrectiveEq`.
- [ ] **Step 4: Eseguire i test** → PASS.
- [ ] **Step 5: Commit:** `feat(deep-throat): corrective eq with dynamic resonance notch`.

---

### Task 7: De-esser

**Files:**
- Create: `src/deep-throat/src/dsp/deesser.{h,cpp}`
- Modify: `tests/test_deep_throat.cpp`, `CMakeLists.txt`

**Interfaces:**
- Consumes: `DSP::Biquad` (Task 6).
- Produces: `class DSP::DeEsser { void prepare(double sampleRate, int channels); void reset(); void setAmount(float amount01); void setListen(bool); void setMode(Mode); void processFrame(float* samples /* un campione per canale */); }`. Crossover Linkwitz-Riley a 6500 Hz; il rilevatore è un passa-banda 4-9 kHz sul mono linkato; soglia -30 dBFS; riduzione sulla sola banda alta con rapporto 4:1, massimo 12 dB · Amount; attacco 1 ms, rilascio 40 ms. In Puke l'Amount effettivo è moltiplicato per 0.15. Con `listen=true` l'uscita è la sola banda alta tolta (la parte tagliata).

- [ ] **Step 1: Scrivere i test:** `test_deesser_reduces_sibilance` (seno 7 kHz a -10 dB, Amount 1, Speak: calo ≥ 6 dB dopo 100 ms), `test_deesser_leaves_low_band` (seno 500 Hz a -10 dB: calo < 0.5 dB), `test_deesser_amount_zero_is_transparent` (rumore bianco: uscita uguale all'ingresso entro -60 dB), `test_deesser_puke_nearly_off` (stesso seno 7 kHz in Puke: calo < 2 dB), `test_deesser_listen_outputs_removed_band` (listen, seno 7 kHz: uscita non nulla; seno 500 Hz: uscita < -40 dB).
- [ ] **Step 2: Eseguire e vedere fallire.**
- [ ] **Step 3: Implementare** `DeEsser`.
- [ ] **Step 4: Eseguire i test** → PASS.
- [ ] **Step 5: Commit:** `feat(deep-throat): split-band de-esser with listen mode`.

---

### Task 8: Compressore stile Distressor

**Files:**
- Create: `src/deep-throat/src/dsp/distressor_comp.{h,cpp}`
- Modify: `tests/test_deep_throat.cpp`, `CMakeLists.txt`

**Interfaces:**
- Produces: `class DSP::DistressorComp { void prepare(double sampleRate, int channels); void reset(); void setMode(Mode); void setAmount(float amount01); void processFrame(float* samples); float lastGainReductionDb() const; }`. Feedforward, rilevatore di picco linkato stereo, ginocchio morbido di 6 dB, soglia = `-6 - 24 · Amount` dBFS, rapporto per modalità: Speak 2:1, Sing 3:1, Puke 8:1. Attacco 1 ms (Puke 0.5 ms). Rilascio programma-dipendente a due stadi: veloce 40 ms e lento 400 ms, il lento prevale con segnale sostenuto (rapporto picco/RMS basso). Distorsione: Speak nessuna, Sing Dist 2 (armoniche pari: waveshaper asimmetrico), Puke Dist 3 (dispari: `tanh`), con drive legato ad Amount. Compensazione del guadagno: un seno a -18 dBFS RMS esce entro ±3 dB dall'ingresso per qualunque Amount.

- [ ] **Step 1: Scrivere i test:** `test_comp_reduces_peaks` (seno a -6 dBFS, Amount 1, Speak: riduzione di guadagno > 6 dB), `test_comp_ratio_per_mode` (stesso segnale: riduzione Puke > Sing > Speak), `test_comp_unity_below_threshold` (segnale a -50 dBFS, Amount 0.5: uscita entro ±0.5 dB), `test_comp_level_compensation` (seno -18 dBFS RMS, Amount 0, 0.5, 1: uscita entro ±3 dB), `test_comp_dist2_adds_even_harmonics` (Sing: seno 1 kHz forte, la 2ª armonica supera la 3ª), `test_comp_dist3_adds_odd_harmonics` (Puke: la 3ª armonica supera la 2ª), `test_comp_extreme_input_finite` (rumore a fondo scala per 1 s: nessun NaN, uscita entro ±2).
- [ ] **Step 2: Eseguire e vedere fallire.**
- [ ] **Step 3: Implementare** `DistressorComp`. Nessun riuso di `shared/hardware_emulation`: i waveshaper sono funzioni locali (YAGNI).
- [ ] **Step 4: Eseguire i test** → PASS.
- [ ] **Step 5: Commit:** `feat(deep-throat): distressor-style compressor`.

---

### Task 9: Tone EQ e preset per modalità

**Files:**
- Create: `src/deep-throat/src/dsp/tone_eq.{h,cpp}`, `src/deep-throat/src/mode_presets.h`
- Modify: `tests/test_deep_throat.cpp`, `CMakeLists.txt`

**Interfaces:**
- Consumes: `DSP::Biquad` (Task 6).
- Produces: `class DSP::ToneEq { void prepare(double sampleRate, int channels); void reset(); void setMode(Mode); void setTone(float tone01); /* 0.5 = neutro */ float process(int channel, float x); }`. Con `t = (tone01 - 0.5) · 2`, tre filtri: fango (peaking 250 Hz, Q 0.8), presenza (peaking 3.5 kHz, Q 0.8), aria (shelf alto 12 kHz). Guadagno a t = +1, in dB: Speak {-2, +3, +2}, Sing {-1.5, +2, +4}, Puke {-3, +1, 0}; a t < 0 i segni si invertono (più caldo), scalati linearmente.
- Produces: `mode_presets.h`: `struct ModePreset { float riderMaxRangeDb; float riderSlewDbPerSec; }` e `constexpr ModePreset kModePresets[3]` = Speak {6, 8}, Sing {4, 4}, Puke {3, 2}; `const ModePreset& presetFor(Mode)`. Il parametro Rider Range (0..1) scala `riderMaxRangeDb`.

- [ ] **Step 1: Scrivere i test:** `test_tone_neutral_is_flat` (tone 0.5: seno 250 Hz, 3.5 kHz, 12 kHz entro ±0.3 dB), `test_tone_up_speak` (tone 1: 3.5 kHz +3 dB ±0.7, 250 Hz -2 dB ±0.7), `test_tone_down_inverts` (tone 0: 3.5 kHz -3 dB ±0.7), `test_tone_mode_curves_differ` (tone 1: aria a 12 kHz in Sing > Speak > Puke), `test_presets_values` (range e slew delle tre modalità come sopra).
- [ ] **Step 2: Eseguire e vedere fallire.**
- [ ] **Step 3: Implementare** `ToneEq` e `mode_presets.h`.
- [ ] **Step 4: Eseguire i test** → PASS.
- [ ] **Step 5: Commit:** `feat(deep-throat): one-knob tone eq and mode presets`.

---

### Task 10: Engine e integrazione nel Processor

**Files:**
- Create: `src/deep-throat/src/engine.{h,cpp}`
- Modify: `src/deep-throat/src/{processor.cpp,controller.cpp}`, `tests/test_deep_throat.cpp`, `CMakeLists.txt`

**Interfaces:**
- Consumes: tutti i blocchi dei Task 2-9, `params.h`, `state.h`.
- Produces: `void loadDefaults(double* values)`; `struct Settings { Mode mode; float inputDb, outputDb, riderTargetDb, riderRangeDb, breathAmount, deEssAmount, compAmount, tone; bool deEssListen, bypass; }`; `Settings mapParams(const double* values)` (valori clampati; non finiti → default); `class Engine { void prepare(double sampleRate); void reset(); void setSettings(const Settings&); int latencySamples() const; void process(const float* inL, const float* inR, float* outL, float* outR, int n); }`.
- Comportamento di `process`: input gain → rilevatore di respiri sul mono linkato (`SpectralBreathDetector` in Speak/Sing, `LevelBreathDetector` in Puke) → ritardo di look-ahead 80 ms dell'audio (più la latenza del rilevatore) → Rider (freeze dove la probabilità > 0.5) → BreathReducer → CorrectiveEq → DeEsser → DistressorComp → ToneEq → output gain. Bypass: l'uscita è l'ingresso ritardato di `latencySamples()` (nessuna variazione di latenza). Il processor chiama `getLatencySamples()` → `engine.latencySamples()` e lo comunica all'host.

- [ ] **Step 1: Scrivere i test:** `test_engine_defaults_finite_and_mapped`; `test_engine_silence_stays_silent` (10 s di zeri: uscita esattamente 0, nessun denormale); `test_engine_full_scale_finite` (rumore a ±1 per 2 s in tutte le modalità: nessun NaN, `|uscita| ≤ 4`); `test_engine_zero_and_one_sample_blocks` (alternare blocchi da 0, 1 e 64 campioni: uscita identica a un unico blocco da 65 entro 1e-6); `test_engine_mono_in_mono_out` (L = R in ingresso → L == R in uscita, in ogni modalità); `test_engine_latency_reported_and_bypass_delay` (bypass: uscita = ingresso ritardato di `latencySamples()`); `test_engine_breath_reduced_end_to_end` (frase, respiro a -35 dB, frase, Speak, Breath Amount 1: livello del respiro in uscita calato di almeno 6 dB rispetto a Amount 0); `test_engine_puke_scream_intact` (rumore forte continuo in Puke: Breath Amount 1 non lo abbassa più di 1 dB rispetto a Amount 0); `test_engine_mode_switch_no_blowup` (cambio di modalità ogni 100 ms con seno 220 Hz: `|campione|` ≤ 4, nessun NaN); `test_engine_sample_rates` (44.1, 48, 96, 192 kHz: prepare + 1 s di voce sintetica, tutto finito); `test_engine_no_alloc_in_process` (contatore `operator new` come in Pillow Talk: 0 allocazioni in `process`).
- [ ] **Step 2: Eseguire e vedere fallire.**
- [ ] **Step 3: Implementare** `Engine`, collegare `processor.cpp` (lettura dei parametri, `setSettings`, `getLatencySamples`, `setActive` che chiama `prepare`) e `controller.cpp` (nomi dei parametri in inglese: Bypass, Mode con scelte `Speak/Sing/Puke`, Input, Rider Target, Rider Range, Breath, De-ess, De-ess Listen, Compressor, Tone, Output).
- [ ] **Step 4: Eseguire tutti i test** → PASS; costruire `deep_throat` e rieseguire `deep_throat_factory_test`.
- [ ] **Step 5: Commit:** `feat(deep-throat): engine and processor integration`.

---

### Task 11: Documentazione e chiusura

**Files:**
- Create: `src/deep-throat/README.md`
- Modify: `docs/superpowers/specs/2026-10-10-deep-throat-design.md` (§6: aggiungere `De-ess Listen`; §9: segnare risolta la decisione sul riuso del drive, deciso di non riusare), questo piano (spuntare i task), `CLAUDE.md` (§8 puntatori: aggiungere i due documenti)

- [ ] **Step 1: Scrivere il README** (in italiano): scopo, modalità, parametri, come costruire (comandi del piano), limiti noti (soglie non tarate su registrazioni reali, nessuna GUI).
- [ ] **Step 2: Allineare spec, piano e CLAUDE.md** come da elenco.
- [ ] **Step 3: Verifica finale:** eseguire `ctest --test-dir /tmp/vc-build -R deep_throat --output-on-failure` e i test di Pillow Talk e The Stripper per escludere regressioni; riportare l'output.
- [ ] **Step 4: Commit:** `docs(deep-throat): readme and aligned spec and plan`.

---

## Self-review

- **Copertura della spec:** §2-3 catena e scelte → Task 2-10; §4 rilevamento → Task 3-4; §5 tabella modalità → Task 2 (rider), 6 (EQ), 7 (de-esser), 8 (comp), 9 (tone); §6 parametri → Task 1 e 10 (con `De-ess Listen` aggiunto, Task 11 aggiorna la spec); §7 struttura → Task 1; §8 test → in ogni task; §9 decisioni aperte → restano aperte: nome, registrazioni di riferimento per tarare Puke, dati per il livello 2, GUI.
- **Coerenza dei tipi:** `Mode`, `Biquad`, `BreathDetector`, `LevelMeter` definiti nei Task 1, 6, 3, 2 e usati dopo con le stesse firme.
- **Soglie:** tutti i numeri sono valori iniziali scelti per i test sintetici; vanno ritarati con voci reali (growl e scream inclusi).
