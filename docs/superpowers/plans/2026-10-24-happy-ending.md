# Happy Ending Implementation Plan — motore audio e plugin VST3

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Costruire Happy Ending, catena di mastering VST3 (INPUT → FINE EQ → VIBE EQ → GLUE → CLIP → LIMIT → FINAL → METALHEAD → OUTPUT) con LOUDNESS, TARGET e MATCH, LUFS, oversampling e stato, usabile dalla lista parametri dell'host.

**Architecture:** Motore puro senza SDK in `src/happy-ending/src/` (`engine.{h,cpp}` più un blocco per file in `dsp/`), come Pillow Talk e Deep Throat; processor e controller VST3 sottili sopra. Due zone sovracampionate con lo stesso `Oversampler` a fase lineare: il TUBE del VIBE EQ e il tratto CLIP → LIMIT → FINAL → METALHEAD (il limiter sta dentro perché è in mezzo agli stadi non lineari: una sola coppia di filtri invece di tre). Latenza totale costante = zona TUBE + zona finale + look-ahead del limiter, ricalcolata solo cambiando OVERSAMPLING.

**Tech Stack:** C++20, VST3 SDK, CMake/CTest. Port senza JUCE da Dusk Audio (GPL-3.0) e Airwindows (MIT); `shared/inflator` già pronto. **Editor grafico escluso da questo piano:** ha un piano a parte, da scrivere dopo questo, insieme a SIZE in `shared/ui` (`docs/DESIGN_SYSTEM.md`).

**Spec:** `docs/superpowers/specs/2026-10-21-happy-ending-design.md`

## Global Constraints

- Namespace `FairyTapes::HappyEnding` (DSP in `...::DSP`). Bundle `HappyEnding.vst3` in `dist/` (`OUTPUT_NAME "HappyEnding"`); stereo in, stereo out (mono → mono accettato).
- Licenza del plugin GPL-3.0. Ogni file portato cita in testa origine, autore, licenza e percorso nel submodule (come `shared/inflator/inflator.h`). Nessun include di JUCE.
- Comandi tutti a scatti: ogni parametro è un `StringListParameter`/`RangeParameter` con `stepCount` = numero di valori − 1; doppio clic = default (editor, piano a parte).
- Nessuna rilevazione true peak: limiter e ceiling lavorano sul picco del campione (spec §2).
- **Nessun campione d'uscita sopra il CEIL** (−0,1 / −0,3 / −0,5 / −1,0 dB) con LIMIT, FINAL o METALHEAD accesi, in qualsiasi combinazione.
- LISTEN e SET **non entrano nello stato** e partono sempre spenti.
- Nessuna allocazione nel thread audio (`process`): buffer dimensionati in `setupProcessing`/`prepare`.
- Test: harness `RUN_TEST`/`ASSERT_TRUE` come `src/pillow-talk/tests/test_pillow_talk.cpp`, un eseguibile per blocco; ogni comportamento nasce con un test che prima fallisce.
- `kStateVersion = 1`: `int32` little endian, poi `kNumParams` `double` normalizzati nell'ordine dell'enum. Gli ID dell'enum sono congelati dal Task 1: si aggiunge solo in coda.
- CIDs nuovi in `cids.h` (non riusare quelli degli altri plugin). Testo visibile e nomi dei parametri in inglese.
- Build fuori dal repo: `cmake -S . -B /tmp/he-build -DCMAKE_BUILD_TYPE=Release -DGPU_NAM_DISABLE_CUDA=ON`, poi `cmake --build /tmp/he-build --target <target> -j2`; test con `ctest --test-dir /tmp/he-build -R happy_ending --output-on-failure`.
- Valori da tarare (DYN, basi di release, soglia di SET, curva della LOUDNESS, passo di MATCH) solo in `src/happy-ending/src/tuning.h`, una tabella di `constexpr` con i valori di partenza della preview.

## Review Focus

- **Frequenze di campionamento 44,1-192 kHz e OVERSAMPLING 4× a 192 kHz:** HI SHELF a 20 kHz e HI MID a 12,5 kHz a 44,1 kHz restano stabili e non esplodono (frequenza limitata sotto Nyquist). Test nel Task 2.
- **Silenzio dopo il segnale:** nessun denormale e uscita esattamente 0 dopo la coda, con tutti gli stadi accesi (CPU stabile su tracce che finiscono). Test nel Task 12.
- **Cambi di valore durante la riproduzione** (automazione a scatti di GAIN, TILT, THRESH, OUTPUT): nessun salto udibile, cioè nessuna differenza tra campioni consecutivi oltre 0,05 su un seno a −6 dBFS. Test nei Task 2 e 12.
- **Stereo con i due canali identici (mono nel bus stereo):** il SIDE è zero, le bande in SIDE non aggiungono niente e l'uscita resta identica sui due canali. Test nel Task 3.
- **Tutto al massimo:** LOUDNESS 10, PUSH +6, EFFECT 100 %, CEIL −0,1 dB su rumore rosa a 0 dBFS: nessun campione sopra il ceiling. Test nel Task 12.

---

### Task 1: Scheletro, tabella dei parametri e build

**Files:**
- Create: `src/happy-ending/CMakeLists.txt`, `src/happy-ending/src/params.h`, `src/happy-ending/src/tuning.h`, `src/happy-ending/tests/test_params.cpp`
- Modify: `CMakeLists.txt` (radice: `add_subdirectory(src/happy-ending)` dopo `src/deep-throat`)

**Interfaces:**
- Produces: `enum ParamID : Steinberg::Vst::ParamID` (ordine qui sotto, `kNumParams` in coda); `struct ParamSpec { const char* name; int steps; int defaultIndex; }`; `constexpr std::array<ParamSpec, kNumParams> kParams`; `int indexFromNorm(ParamID, double)`, `double normFromIndex(ParamID, int)`; tabelle dei valori come `constexpr std::array` nel namespace `Tables::` (nomi come l'enum), più `kAmounts` (0…10 interi per LOW, AIR, TUBE) e `valueTable(ParamID)`: `kHpFreqs`, `kHpSideFreqs`, `kLowShelfFreqs`, `kLoMidFreqs`, `kMidFreqs`, `kHiMidFreqs`, `kHiShelfFreqs`, `kEqGains`, `kQValues`, `kLowFreqs`, `kAirFreqs`, `kTilts`, `kGlueThresh`, `kGlueAttackMs`, `kGlueReleaseS` (0 = Auto), `kGlueRatios`, `kGlueMakeup`, `kGlueMix`, `kScHpfHz` (0 = Off), `kClipDrive`, `kLimitThresh`, `kCeilings`, `kFinalEffect`, `kFinalCurve`, `kFinalDrive`, `kPush`, `kLoudness`, `kTargets`, `kIoGain`.

Ordine congelato e valori (default in grassetto), da spec §3-§5 e dalla preview:
- Globali: `kBypass` (off), `kInputGain` −12…+12 /0,5 (**0**), `kOutputGain` idem (**0**), `kCeiling` −0,1/**−0,3**/−0,5/−1,0, `kLoudness` 0…10 /0,5 (**0**), `kTarget` **−14**/−11/−9/−7, `kOversampling` Off/**2×**/4× (non automatizzabile).
- FINE EQ: `kFineOn`; per banda `On`, `Ms` (**ST**/MID/SIDE), `Freq`, e dove serve `Gain` −4…+4 /0,5 (**0**) e `Q` W/**M**/N/S (0,5/1/2/5). Bande nell'ordine HI PASS (`kHpFreq` 6 posizioni **Off**/20/25/30/35/40; sul SIDE Off/60/80/120/160/200 con lo stesso indice), LOW SHELF (30/50/70/**100**/150/200/300), LO MID (100…800 a terzi, **250**), MID (400…3150, **1000**), HI MID (1600…12500, **4000**), HI SHELF (3/5/8/10/**12**/16/20 kHz).
- VIBE EQ: `kVibeOn`, `kLowFreq` 20/30/**60**/100, `kLow` interi 0…10, 11 scatti (**0**), `kAirFreq` 5/8/10/**12**/16 kHz, `kAir` interi 0…10, 11 scatti (**0**), `kTilt` −3…+3 /0,5 (**0**), `kTube` interi 0…10, 11 scatti (**2**), `kDynLow` (off), `kDynAir` (off).
- GLUE: `kGlueOn`, `kGlueThresh` −20…0 /1 (**−10**), `kGlueAttack` 0,1/0,3/1/**3**/10/30 ms, `kGlueRelease` 0,1/0,3/0,6/1,2 s/**Auto**, `kGlueRatio` 1,5/**2**/4, `kGlueMakeup` 0…6 /0,5 (**0**), `kGlueMix` 0…100 /5 (**50**), `kGlueScHpf` Off/**60**/90/150, `kGlueWarm` (off).
- CLIP: `kClipOn`, `kClipDrive` 0…6 /0,5 (**+2**).
- LIMIT: `kLimitOn`, `kLimitThresh` 0…−4 /0,5 (**−2**), `kLimitStyle` **MODERN**/PUNCH/DYN, `kLimitRelease` 9 scatti (**centro**).
- FINAL: `kFinalOn`, `kFinalMode` **INFLATE**/CLIP, `kFinalEffect` 0…100 /10 (**50**), `kFinalCurve` 0/**25**/50/75/100, `kFinalDrive` 0…2 /0,5 (**+2**).
- METALHEAD: `kMetalOn` (**off**), `kPush` 0…6 /0,5 (**+2**).
- Fuori dallo stato, dopo `kNumParams`: `kListenBand` (−1 = off, 0-2 le tre campane) e `kSetActive`, parametri nascosti non salvati.

- [x] **Step 1: Scrivi `test_params.cpp`**: `testStepsMatchTables` (per ogni parametro `kParams[id].steps == tabella.size()`), `testDefaultsRoundTrip` (`indexFromNorm(id, normFromIndex(id, kParams[id].defaultIndex)) == defaultIndex` per tutti), `testFrozenOrder` (`kBypass == 0`, `kPush == kNumParams - 1`, `kListenBand > kNumParams`), `testHpSideScaleSameIndexRange` (`kHpFreqs.size() == kHpSideFreqs.size() == 6`, lo stesso indice di `kHpFreq` vale sul SIDE e raggiunge i 200 Hz).
- [x] **Step 2: Aggiungi i target CMake** `happy_ending_params_tests` (solo header, include `${VST3_SDK_PATH}`) e `add_test`; esegui: `cmake --build /tmp/he-build --target happy_ending_params_tests -j2` → errori di compilazione (header mancanti).
- [x] **Step 3: Scrivi `params.h` e `tuning.h`** con l'enum, le tabelle e i default qui sopra; `tuning.h` con `kDynThresholdDb`, `kDynAttackMs`, `kDynReleaseMs`, `kLimitReleaseBaseMs = {50, 100, 250}`, `kSetThresholdDb = -8`, `kLoudCurve` (CLIP +1,5 dB, LIMIT −3 dB, FINAL 1,0 sui tre terzi della corsa), `kMatchDbPerStep = 0.75`.
- [x] **Step 4: Esegui** `ctest --test-dir /tmp/he-build -R happy_ending_params --output-on-failure` → PASS.
- [x] **Step 5: Commit** `feat(happy-ending): parameter table and build skeleton`.

### Task 2: Biquad matched e bande del FINE EQ

**Files:**
- Create: `src/happy-ending/src/dsp/matched_biquad.h` (port di `vendors/dusk-audio-plugins/plugins/multi-q/AnalogMatchedBiquad.h`, già senza JUCE), `src/happy-ending/src/dsp/fine_eq.{h,cpp}`, `src/happy-ending/tests/test_fine_eq.cpp`

**Interfaces:**
- Consumes: tabelle di frequenza, `kEqGains`, `kQValues` (Task 1).
- Produces: `struct BandSettings { bool on; int ms; int freqIndex; int gainIndex; int qIndex; }`; `class FineEq { void prepare(double sr, int maxBlock); void setBand(int band, const BandSettings&); void setStageOn(bool); void setListen(int bellOrMinus1); void process(float* L, float* R, int n); double magnitudeDb(int target, double hz) const; }` con `target` 0 = ST, 1 = MID, 2 = SIDE (per lo schermo, piano grafico). **Stato (dopo il Task 3):** `setListen` e l'instradamento M/S esistono; `magnitudeDb(0)` somma le bande su ST, `magnitudeDb(1)`/`(2)` aggiungono quelle su MID/SIDE (le curve ignorano LISTEN). Il Q delle shelf è 0,7071 (`tuning.h`, `kFineShelfQ`), come quello del passa-alto; `testQCoupling` misura la larghezza a metà del picco in dB (a −3 dB non esiste per la campana a +1 dB); `testGainStepNoZipper` usa una HI SHELF (una campana non scatta).

- [x] **Step 1: Test**: `testBellCentreGain` (campana a 1 kHz, +4 dB, Q M: guadagno misurato a 1 kHz = +4 dB ±0,1 a 44,1, 48, 96 kHz), `testNoCrampingNearNyquist` (HI MID 12,5 kHz +4 dB a 44,1 kHz: errore contro il prototipo analogico ≤ 0,5 dB a 10 e 14 kHz), `testQCoupling` (larghezza a −3 dB della campana a +4 dB minore di quella a +1 dB; Q effettivo = Q × (1 + |gain|/8)), `testHighPassOffIsIdentity`, `testHighPassSlope12dB` (HP 40 Hz: −12 dB/ottava ±1 dB tra 10 e 20 Hz), `testStableAt192kAnd44k` (HI SHELF 20 kHz a 44,1 e 192 kHz: uscita finita su rumore bianco), `testGainStepNoZipper` (seno −6 dBFS, gain da 0 a +4 dB a metà blocco: nessun |y[n]−y[n−1]| oltre 0,05).
- [x] **Step 2: Esegui** `happy_ending_fine_eq_tests` → FAIL (simboli mancanti).
- [x] **Step 3: Implementa** i coefficienti matched di peak, shelf e passa-alto 12 dB/ott (Q 0,707), con frequenza limitata da `clampFreq`; le variazioni di coefficienti si interpolano linearmente su 64 campioni per non scattare.
- [x] **Step 4: Esegui** → PASS.
- [x] **Step 5: Commit** `feat(happy-ending): matched biquad port and Fine EQ bands`.

Nota (Task 4): le campane del FINE EQ usano ora il progetto matched di Vicanek (`matched::computePeakingMatched`): boost diretto e taglio come reciproco esatto del boost; LISTEN e lo shelf basso restano com'erano; lo shelf alto (HI SHELF) usa ora il progetto matched (`matched::computeHighShelfMatched`, boost e taglio reciproco), con errore sotto 0,3 dB rispetto al prototipo analogico fino a Nyquist per tutti gli angoli a 44,1 / 48 / 96 kHz.

### Task 3: M/S, IN e LISTEN del FINE EQ

**Files:**
- Modify: `src/happy-ending/src/dsp/fine_eq.{h,cpp}`; Create: `src/happy-ending/src/dsp/ms.h`; Test: `src/happy-ending/tests/test_fine_eq.cpp`

**Interfaces:**
- Produces: `msEncode(L, R, M, S)` (M = (L+R)/2, S = (L−R)/2) e `msDecode`, template su float/double in `dsp/ms.h`; comportamento di `setListen` e del SIDE descritto qui sotto. LISTEN suona la campana sulla propria via (ST, solo MID o solo SIDE); `matched::computeBandPass` in `matched_biquad.h`.

- [x] **Step 1: Test**: `testMsRoundTripLossless` (rumore stereo: decode(encode(x)) == x entro 1e-6), `testSideBandOnMonoIsSilent` (L = R, banda in SIDE +4 dB: uscita identica all'ingresso entro 1e-6 e L == R), `testMidBandLeavesSideUntouched`, `testHpSideUsesMonoBassScale` (HP in SIDE indice 5 = 200 Hz: il SIDE a 100 Hz cala di almeno 5 dB, il MID resta invariato), `testListenSolosBell` (LISTEN: un seno a 100 Hz cala di almeno 20 dB, uno alla frequenza della campana resta entro 1 dB; LO MID arriva solo a 800 Hz, quindi 1 kHz è provato sulla MID), `testBandOffAndStageOffAreIdentity`.
- [x] **Step 2: Esegui** → FAIL.
- [x] **Step 3: Implementa** il percorso a tre filtri per banda (ST, MID, SIDE scelti dal selettore) e LISTEN come passa-banda alla frequenza e al Q effettivo della campana scelta (le altre bande non suonano).
- [x] **Step 4: Esegui** → PASS.
- [x] **Step 5: Commit** `feat(happy-ending): Fine EQ M/S, band IN and LISTEN`.

### Task 4: VIBE EQ

**Files:**
- Create: `src/happy-ending/src/dsp/vibe_eq.{h,cpp}` (port senza JUCE di `InductorModel`, `TubeEqLFSection`, `TubeEQTubeStage` da `vendors/dusk-audio-plugins/plugins/multi-q/TubeEQProcessor.h` e di quanto serve da `ADAASaturation.h`), `src/happy-ending/tests/test_vibe_eq.cpp`

**Interfaces:**
- Produces: `struct VibeSettings { bool on; int lowFreqIndex; int low; int airFreqIndex; int air; int tiltIndex; int tube; bool dynLow; bool dynAir; }`; `class VibeEq { void prepare(double sr, int maxBlock); void set(const VibeSettings&); void processLinear(float* L, float* R, int n); void prepareTube(double sr); void processTube(float* L, float* R, int n); float dynLowAmount() const; float dynAirAmount() const; double magnitudeDb(double hz, bool staticSetting) const; }`. `processTube` è separato perché gira nella zona sovracampionata (Task 13); `prepareTube(sr)` lo prepara a quella frequenza (`prepare()` lo chiama con la frequenza base).

- [x] **Step 1: Test**: `testLowTrickBoostAndDip` (LOW 60 Hz, 6: risposta a 60 Hz > +2 dB e un minimo sotto 0 dB tra 100 e 400 Hz), `testAirBoost` (AIR 10 kHz, 6: +2 dB o più a 10 kHz), `testTiltSymmetric` (TILT +3: guadagno a 50 Hz ≈ −guadagno a 10 kHz entro 0,5 dB; TILT 0 piatto entro 0,1 dB), `testDynPullsBackOnLoudProgram` (LOW 8 + DYN: su un seno a 60 Hz a −30 dBFS il boost misurato supera di almeno 2 dB quello a −6 dBFS; `dynLowAmount()` > 0 solo nel secondo caso), `testTubeZeroIsNearlyLinear` (TUBE 0: THD di un seno 1 kHz a −6 dBFS sotto −60 dB), `testTubeAddsHarmonics` (TUBE 10: THD sopra −46 dB e almeno 6 dB sopra quella di TUBE 2), `testAirDoesNotLiftMidsNearNyquist` (AIR 16 kHz, 10, a 44,1 kHz: a 1 kHz sotto +0,5 dB, a 16 kHz almeno +2 dB), `testPrepareTubeAtOversampledRate`, `testStageOffIsIdentity`.
- [x] **Step 2: Esegui** → FAIL.
- [x] **Step 3: Implementa** il port (soglia e tempi del DYN da `tuning.h`); TILT come coppia di shelf a 1 kHz di segno opposto, ±3 dB.
- [x] **Step 4: Esegui** → PASS.
- [x] **Step 5: Commit** `feat(happy-ending): Vibe EQ port (LOW trick, AIR, TILT, DYN, TUBE)`.

Nota (Task 4 fatto): il taglio di LOW è una campana a 2,5 volte la frequenza del boost, non lo shelf basso alla stessa frequenza della sorgente (che non può fare una gobba sopra il boost). AIR ha Q fisso 0,8 e usa il peaking matched di Vicanek (`matched::computePeakingMatched` in `matched_biquad.h`), quindi la campana tiene la larghezza vicino a Nyquist e non alza i medi; il polinomio del Q della sorgente non si usa per AIR (appiattirebbe un boost a 10 kHz). TUBE è fedele alla sorgente (circa −44 dB di THD a TUBE 10, −56,6 dB a drive 0,2); TUBE 0 è un bypass esatto. `prepareTube(sr)` prepara lo stadio valvolare alla frequenza sovracampionata. Il modello dell'induttore è limitato (eccesso e termini polinomiali non crescono oltre il fondo scala), il limite di sicurezza dello stato dei filtri è 1e3 e gli induttori lavorano molto sotto il livello del segnale (`kVibeInductorLevelDb`, −30 dB: i commenti della sorgente dicono 10-15 dB sotto ma il codice li alimenta a livello pieno, e con LOW 10 comprimeva già a −10 dBFS): la saturazione del nucleo ora tocca solo i picchi più alti (LOW 10 a 60 Hz: THD sotto −50 dB a −10 dBFS e sotto −30 dB a +6 dBFS). Il TILT usa lo shelf alto matched (`matched::computeHighShelfMatched`). Tutto ciò che si tara a orecchio sta in `tuning.h` (`kVibe*`, `kDyn*`).

### Task 5: GLUE

**Files:**
- Create: `src/happy-ending/src/dsp/glue.{h,cpp}` (port senza JUCE di `UniversalCompressor::BusCompressor` da `vendors/dusk-audio-plugins/plugins/multi-comp/multicomp.cpp` riga ~2538, con tutti i comandi; il `MultiCompBus` dello Stripper ha solo amount e mix e non basta), `src/happy-ending/tests/test_glue.cpp`

**Interfaces:**
- Produces: `struct GlueSettings { bool on; float threshDb; float attackMs; float releaseS; /*0 = Auto*/ float ratio; float makeupDb; float mix; /*0..1*/ float scHpfHz; /*0 = Off*/ bool warm; }`; `class Glue { void prepare(double sr, int maxBlock); void set(const GlueSettings&); void process(float* L, float* R, int n); float grDb() const; float netGrDb() const; }` (`netGrDb` = riduzione dopo il MIX, per lo schermo).

- [x] **Step 1: Test**: `testMoreRatioMoreReduction` (rumore rosa −6 dBFS, soglia −20: GR con 4:1 > 2:1 > 1,5:1), `testMixZeroIsDry` (uscita == ingresso entro 1e-6, compreso il MAKEUP che vale solo sul ramo compresso), `testMixHalfIsParallel` (rapporto tra uscita e somma metà/metà dei due rami entro 0,1 dB), `testScHpfReducesBassPumping` (seno 40 Hz −6 dBFS: GR con SC HPF 150 almeno 3 dB meno che con Off), `testWarmSoftensAttack` (picco del primo transiente di un clic: con WARM più alto che senza), `testAutoReleaseRuns` (release Auto: GR torna sotto 0,5 dB entro 2 s dopo la fine del segnale).
- [x] **Step 2: Esegui** → FAIL.
- [x] **Step 3: Implementa** il port; WARM = costante d'attacco ×3 sul rilevatore; MIX parallelo dentro il blocco.
- [x] **Step 4: Esegui** → PASS.
- [x] **Step 5: Commit** `feat(happy-ending): Glue bus compressor with full controls and parallel mix`.

Nota (Task 5 fatto): il rilevatore resta in retroazione come nella sorgente (prende l'uscita compressa precedente, passa-alto a 2 poli selezionabile Off / 60 / 90 / 150 Hz al posto dei 60 Hz fissi, media RMS di 5 ms), ginocchio morbido di 10 dB, riduzione massima 20 dB, release Auto 150-450 ms. Stereo sempre collegato: un solo guadagno dal rilevatore più forte. Lo stadio colore è quello del profilo Console Bus: trasformatore d'ingresso e d'uscita attivi, armoniche k2/k3, compensazione di guadagno misurata in `prepare()` (a 1 kHz, −18 dBFS il blocco è a guadagno unitario); la convoluzione corta della sorgente è un bypass e non è portata. WARM moltiplica la costante d'attacco per `Tuning::kGlueWarmAttackFactor` (3); MAKEUP e MIX scorrono linearmente in `Tuning::kGlueRampMs` (10 ms) e così l'accensione/spegnimento (a stadio spento: identità esatta); il primo `set()` dopo `prepare()` non scorre. `grDb()` è la riduzione del compressore, `netGrDb()` quella dopo il MIX e prima del MAKEUP. Il rilevatore non dipende da MIX né da MAKEUP, quindi MIX 0 è l'ingresso bit per bit.

### Task 6: Oversampler a fase lineare

**Files:**
- Create: `src/happy-ending/src/dsp/oversampler.{h,cpp}`, `src/happy-ending/tests/test_oversampler.cpp`

**Interfaces:**
- Produces: `class Oversampler { void prepare(int factor /*1, 2, 4*/, int maxBlock); int latencySamples() const; /*a frequenza base, intero*/ int up(const float* in, int n, float* out); void down(const float* in, int nUp, float* out); }`; una istanza per canale e per zona.

- [x] **Step 1: Test**: `testFactorOneIsPassThrough` (latenza 0, uscita == ingresso), `testRoundTripIsPureDelay` (×2 e ×4: down(up(x)) == x ritardato di `latencySamples()` entro −90 dB su rumore limitato a 18 kHz a 44,1 kHz), `testPassbandFlat` (±0,1 dB fino a 0,4·fs), `testImageRejection` (seno a 15 kHz a 44,1 kHz in ×2: energia dell'immagine a 29,1 kHz nel segnale sovracampionato sotto −90 dB), `testLatencyIsInteger` (per entrambi i fattori).
- [x] **Step 2: Esegui** → FAIL.
- [x] **Step 3: Implementa** semibanda FIR simmetrica in polifase (×4 = due stadi ×2), lunghezza scelta perché la latenza sia intera a frequenza base. Nota: stadi da 4k+3 tap (Kaiser, 100 dB, bordo banda passante 0,4535·fs = 20 kHz a 44,1 kHz, deciso dal controller: ×2 è il default e la zona copre CLIP→METALHEAD): 139 tap in ×2 (latenza 69), +27 tap per il secondo stadio in ×4 (latenza totale 76); in ×4 un campione extra a 2× nella catena di decimazione rende intera la latenza.
- [x] **Step 4: Esegui** → PASS.
- [x] **Step 5: Commit** `feat(happy-ending): linear-phase polyphase oversampler`.

### Task 7: CLIP

**Files:**
- Create: `src/happy-ending/src/dsp/clip_softly.h` (port di `vendors/airwindows/plugins/LinuxVST/src/ClipSoftly/ClipSoftlyProc.cpp`, MIT, senza dither di uscita), `src/happy-ending/tests/test_clip.cpp`

**Interfaces:**
- Produces: `class ClipSoftly { void prepare(double sr); void setDriveDb(float); void process(float* L, float* R, int n); float shavedDb() const; }` (dB tolti ai picchi per la riga GR "C"). Il drive in ingresso è compensato in uscita: livello compensato.

- [ ] **Step 1: Test**: `testQuietPassesUnchanged` (seno 1 kHz −40 dBFS, DRIVE +6: livello entro 0,1 dB), `testPeaksAreRounded` (seno −1 dBFS, DRIVE +6: picco d'uscita più basso di quello d'ingresso, `shavedDb()` > 1), `testDriveZeroTransparent` (DRIVE 0 su seno −6 dBFS: THD sotto −60 dB).
- [ ] **Step 2: Esegui** → FAIL. **Step 3: Implementa** il port. **Step 4: Esegui** → PASS.
- [ ] **Step 5: Commit** `feat(happy-ending): soft clipper (ClipSoftly port), level matched`.

### Task 8: LIMIT

**Files:**
- Create: `src/happy-ending/src/dsp/limiter.{h,cpp}`, `src/happy-ending/tests/test_limiter.cpp`

**Interfaces:**
- Produces: `struct LimiterSettings { bool on; float threshDb; int style; int releaseTrimIndex; float ceilingDb; bool set; }`; `class Limiter { void prepare(double sr, int maxBlock); void set(const LimiterSettings&); void process(float* L, float* R, int n); int latencySamples() const; float grDb() const; float effectiveThreshDb() const; }`. Recupero di guadagno come L2: uscita portata al ceiling meno la soglia.

Valori: look-ahead MODERN 1,5 ms, PUNCH 0,3 ms (il ritardo resta quello di MODERN: PUNCH guarda meno avanti, quindi lascia passare il primo istante del transiente), DYN 0,8 ms; release base da `kLimitReleaseBaseMs`, trim = 2^(k/3), k = −4…+4; DYN con release dipendente dal programma e ginocchio morbido; SET porta la soglia a `kSetThresholdDb` in 150 ms e ritorno.

- [ ] **Step 1: Test**: `testNeverAboveCeiling` (rumore rosa a +6 dBFS, ogni stile e ogni ceiling: max |y| ≤ ceiling lineare), `testLatencyConstantAcrossStyles`, `testReleaseTrim` (k = +4 tempo di ritorno ≈ base × 2,52 ±10 %; k = 0 ≈ base), `testPunchLetsTransientThrough` (clic: picco del primo campione oltre la soglia più alto con PUNCH che con MODERN, sempre ≤ ceiling), `testSetForcesThreshold` (`set = true`: `effectiveThreshDb()` arriva a −8 entro 200 ms e torna al valore entro 200 ms da `set = false`), `testOffIsDelayedIdentity`.
- [ ] **Step 2: Esegui** → FAIL. **Step 3: Implementa.** **Step 4: Esegui** → PASS.
- [ ] **Step 5: Commit** `feat(happy-ending): look-ahead sample-peak limiter with styles and SET`.

### Task 9: FINAL e METALHEAD

**Files:**
- Create: `src/happy-ending/src/dsp/final_stage.h`, `src/happy-ending/src/dsp/clip_only2.h` (port di `vendors/airwindows/plugins/LinuxVST/src/ClipOnly2/ClipOnly2Proc.cpp`, MIT), `src/happy-ending/src/dsp/metalhead.{h,cpp}`, `src/happy-ending/tests/test_final.cpp`
- Modify: `src/happy-ending/CMakeLists.txt` (include `shared/inflator`)

**Interfaces:**
- Consumes: `inflator::Inflator<float>` da `shared/inflator/inflator.h`.
- Produces: `class FinalStage { void prepare(double sr); void set(bool on, int mode, float effect /*0..1*/, float curve /*0..1*/, float driveDb, float ceilingDb); void process(float*, float*, int); float shavedDb() const; }`; `class Metalhead { void prepare(double sr, int maxBlock); void set(bool on, float pushDb, float ceilingDb); void process(float*, float*, int); int latencySamples() const; float grDb() const; }`.

- [ ] **Step 1: Test**: `testInflateEffectZeroIsIdentity`, `testInflateNeverAboveCeiling` (EFFECT 100 %, CURVE 0-100, rumore a 0 dBFS), `testClipModeNeverAboveCeiling` (DRIVE +2), `testModesKeepTheirValues` (cambiare modo non tocca i valori dell'altro), `testMetalheadOffIsDelayedIdentity` (spento: uscita = ingresso ritardato di `latencySamples()`, latenza uguale da acceso e da spento), `testMetalheadNeverAboveCeiling` (PUSH +6).
- [ ] **Step 2: Esegui** → FAIL. **Step 3: Implementa** (METALHEAD: brickwall con look-ahead fisso di 1 ms e release 30 ms). **Step 4: Esegui** → PASS.
- [ ] **Step 5: Commit** `feat(happy-ending): Final (inflate or clip) and Metalhead brickwall`.

### Task 10: Misura LUFS

**Files:**
- Create: `src/happy-ending/src/dsp/loudness_meter.{h,cpp}`, `src/happy-ending/tests/test_loudness.cpp`

**Interfaces:**
- Produces: `class LoudnessMeter { void prepare(double sr); void process(const float* L, const float* R, int n); double shortTermLufs() const; /*finestra 3 s*/ double integratedLufs() const; /*gating ITU-R BS.1770-4*/ void resetIntegrated(); }`; valore assente = `-INFINITY`.

- [ ] **Step 1: Test** (EBU Tech 3341): `testStereoSine1kAtMinus23` (seno 1 kHz, −23 dBFS di picco su entrambi i canali, 20 s: integrata −23,0 ±0,1 LUFS; short-term idem dopo 3 s), `testStereoSine1kAtMinus33` (−33,0 ±0,1), `testGatingIgnoresSilence` (10 s a −23 poi 10 s di silenzio: integrata −23,0 ±0,1), `testResetIntegrated`, `testWorksAt44kTo192k` (caso −23 a 44,1, 48, 96, 192 kHz).
- [ ] **Step 2: Esegui** → FAIL. **Step 3: Implementa** pesatura K (due biquad ricalcolati per frequenza), blocchi da 400 ms con 75 % di sovrapposizione, gate assoluto −70 e relativo −10. **Step 4: Esegui** → PASS.
- [ ] **Step 5: Commit** `feat(happy-ending): BS.1770 loudness meter (short-term, integrated)`.

### Task 11: LOUDNESS e MATCH

**Files:**
- Create: `src/happy-ending/src/loudness_macro.h`, `src/happy-ending/tests/test_loudness_macro.cpp`

**Interfaces:**
- Produces: `struct LoudPush { float clipDb; float limitDb; float final01; }`; `LoudPush loudPush(float loudness /*0..10*/)`; `int matchLoudnessIndex(int currentIndex, double shortTermLufs, double targetLufs)` (indice 0…20; nessun cambio se lo short-term è `-INFINITY`).

- [ ] **Step 1: Test**: `testZeroIsNoPush` (tutti 0), `testOrderClipThenLimitThenFinal` (a 3,0 solo CLIP > 0; a 6,0 CLIP = 1,5 e LIMIT > 0 con FINAL = 0; a 10 CLIP 1,5, LIMIT 3, FINAL 1), `testMonotonic` (nessun valore cala salendo di 0,5), `testMatchMovesTowardTarget` (indice 4, short-term −12, target −9: indice nuovo = 4 + round(3 / 0,75) = 8), `testMatchClamps` (risultato in 0…20), `testMatchIgnoresNoSignal`.
- [ ] **Step 2: Esegui** → FAIL. **Step 3: Implementa** con i valori di `kLoudCurve` e `kMatchDbPerStep`. **Step 4: Esegui** → PASS.
- [ ] **Step 5: Commit** `feat(happy-ending): LOUDNESS push curve and MATCH`.

### Task 12: Engine (catena completa)

**Files:**
- Create: `src/happy-ending/src/engine.{h,cpp}`, `src/happy-ending/tests/test_engine.cpp`

**Interfaces:**
- Consumes: tutti i blocchi dei Task 2-11.
- Produces: `struct EngineSettings` (tutti i valori già convertiti da indici a unità, compresi `listenBand`, `setActive`, `oversampling`); `EngineSettings mapParams(const double* norm, int listenBand, bool setActive)`; `class Engine { void prepare(double sr, int maxBlock, int oversampling); void set(const EngineSettings&); void process(float* L, float* R, int n); int latencySamples() const; const Meters& meters() const; void clearClips(); void resetIntegrated(); }`; `struct Meters { float inPeakDb[2], outPeakDb[2]; float grDb[5]; /*G C L F M*/ bool clip[9]; /*IN FEQ VEQ GLU CLP LIM FIN MTL OUT*/ double lufsShort, lufsIntegrated; float dynLow, dynAir; }`.

Ordine: INPUT gain → FINE EQ → VIBE (lineare) → [zona OS A: TUBE] → GLUE → [zona OS B: CLIP (+ LOUDNESS clipDb) → LIMIT (soglia − limitDb) → FINAL (EFFECT o DRIVE + final01) → METALHEAD] → OUTPUT gain → LUFS e meter. BYPASS: uscita = ingresso ritardato di `latencySamples()` (un'unica linea di ritardo), meter e LUFS restano vivi. LED di clip: campione |x| ≥ 1,0 all'uscita di ogni stadio, tenuto 2 s.

- [ ] **Step 1: Test**: `testBypassIsDelayedInput` (uguaglianza esatta al campione con l'ingresso ritardato di `latencySamples()`), `testLatencyPerOversampling` (Off < 2× < 4×, costante tra stili e modi), `testAllMaxNeverAboveCeiling` (Review Focus: LOUDNESS 10, PUSH +6, EFFECT 100 %, CEIL −0,1, rumore rosa 0 dBFS, OVERSAMPLING 1×/2×/4×), `testLoudnessZeroEqualsSettings` (LOUDNESS 0 identico bit per bit a una catena senza macro), `testLoudnessDoesNotMoveParams` (dopo `set` con LOUDNESS 10 `mapParams` restituisce gli stessi indici degli stadi), `testSilenceAfterTailIsZero` (Review Focus: 5 s di rumore, poi 10 s di zeri: ultimi 1000 campioni esattamente 0 e nessun denormale), `testClipLedsPerStage` (INPUT +12 su seno 0 dBFS accende IN e FEQ; `clearClips()` li spegne), `testNoAllocationInProcess` (contatore di `operator new` come in Pillow Talk), `testOutputGainStepNoZipper`.
- [ ] **Step 2: Esegui** → FAIL. **Step 3: Implementa** (flush dei denormali con `FTZ/DAZ` all'inizio di `process`; guadagni INPUT/OUTPUT interpolati su 64 campioni). **Step 4: Esegui** → PASS.
- [ ] **Step 5: Commit** `feat(happy-ending): full mastering chain engine with meters and bypass`.

### Task 13: Plugin VST3: processor, controller, stato e latenza

**Files:**
- Create: `src/happy-ending/src/{cids.h, version.h, plug.cpp, processor.{h,cpp}, controller.{h,cpp}, state.h, Info.plist}`, `src/happy-ending/tests/{test_state.cpp, test_factory.cpp, test_processor_levels.cpp}`
- Modify: `src/happy-ending/CMakeLists.txt` (target `happy_ending` MODULE con bundle come `src/pillow-talk/CMakeLists.txt`, `OUTPUT_NAME "HappyEnding"`; test di modulo con `SKIP_RETURN_CODE 77`)

**Interfaces:**
- Consumes: `Engine`, `mapParams`, `matchLoudnessIndex` (Task 11-12).
- Produces: parametri di sola lettura per il futuro editor da `kDisplayBase = 1000` (`kDispInL…kDispOutR`, `kDispGr0…4`, `kDispClip0…8`, `kDispLufsShort`, `kDispLufsInt`, `kDispDynLow`, `kDispDynAir`, `kDispLatency`) più `kClearClips` e `kResetIntegrated` scrivibili, come in Pillow Talk; `void serializeState(const double* norm, std::vector<uint8_t>&)` e `bool deserializeState(const uint8_t*, size_t, double* norm)`; `Controller::doMatch()` (legge `kDispLufsShort`, applica `matchLoudnessIndex` a `kLoudness` come una modifica unica dell'host, per il pulsante MATCH dell'editor).

- [ ] **Step 1: Test**: `test_state.cpp`: `testRoundTrip` (tutti i parametri a un valore non di default, serialize → deserialize identico), `testRejectsUnknownVersion`, `testListenAndSetNotStored` (lo stato non contiene `kListenBand`/`kSetActive`; dopo il caricamento valgono −1 e off), `testTruncatedStateKeepsDefaults`. `test_factory.cpp` e `test_processor_levels.cpp` come quelli di Pillow Talk (bundle caricato, classi registrate, un seno passa con BYPASS e la latenza riportata è `Engine::latencySamples()`).
- [ ] **Step 2: Esegui** → FAIL.
- [ ] **Step 3: Implementa**: `setupProcessing` prepara l'engine; un cambio di `kOversampling` ricostruisce le zone fuori dal thread audio e chiama `restartComponent(kLatencyChanged)`; `kOversampling` senza `kCanAutomate`; `kListenBand` e `kSetActive` nascosti e fuori dallo stato.
- [ ] **Step 4: Esegui** tutti i test `happy_ending*` → PASS; build di `happy_ending` produce `dist/HappyEnding.vst3`.
- [ ] **Step 5: Commit** `feat(happy-ending): VST3 processor, controller, state and latency reporting`.

### Task 14: Taratura a orecchio e documenti

**Files:**
- Modify: `src/happy-ending/src/tuning.h`, `docs/superpowers/specs/2026-10-21-happy-ending-design.md` (§9: valori tarati), `CLAUDE.md` §8 (puntatore al piano), crea `src/happy-ending/README.md` (catena, build, licenze dei componenti portati con percorsi e commit dei submodule)

- [ ] **Step 1:** build `happy_ending` e caricamento in un host (REAPER o Bitwig) con un mix di prova; l'utente ascolta. Si regolano solo i valori di `tuning.h`: soglia e tempi del DYN, basi di release degli stili, soglia di SET, curva della LOUDNESS, passo di MATCH; ascolto finale di FINE EQ e GLUE con tutta la catena.
- [ ] **Step 2:** dopo ogni modifica di `tuning.h`, tutti i test `happy_ending*` restano verdi (i test usano i valori della tabella, non costanti proprie).
- [ ] **Step 3:** riporta i valori finali nella spec §9 e segna `[x]` i compiti chiusi in questo piano.
- [ ] **Step 4: Commit** `docs(happy-ending): tuned values, README and licences`.
