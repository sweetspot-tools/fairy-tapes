# The Stripper — Controlli (parametri) modificabili da UI

Elenco di **tutti i parametri** esposti al host. Il plugin ha un editor VSTGUI (1000 × 628) con un controllo per ognuno (tranne quelli inerti: gain staging, `kEqHPFOn` e `kEqLPFOn`); l’host li mostra anche nella sua lista. Ordine **per ID** (come appare nel host).

**Totale: 38 parametri** (ID 0…37, `kNumParams`; la tabella è bloccata in `tests/param_table_golden.h`). **Compressor Mode** (ID 14, solo Channel: Voice / Drums / Classic) è esposto; DSP Multi-Comp integrato (port C++ da Dusk Audio), vedi [COMPRESSION_MODES.md](COMPRESSION_MODES.md).

---

## Lista completa (ordine ID)

| # | ID   | Titolo (host)     | Unità / valori      | Note |
|---|-----|-------------------|---------------------|------|
| 0 | `kBypass` | Bypass | On/Off | Bypass globale plugin (standard VST3). |
| 1 | `kStripMode` | Strip Mode | Channel / Mix bus | 0 = Channel (4K-EQ + Multi-Comp usage modes), 1 = Mix bus (Pultec + Multi-Comp Bus only). |
| 2 | `kInputGain` | Input Gain | dB | Trim ingresso (−20 … +20 dB). |
| 3 | `kOutputGain` | Output Gain | dB | Trim uscita (−20 … +20 dB). |
| 4 | `kBypassGainStaging` | Gain Staging Bypass | On/Off | Bypass stadio gain staging. **Solo Channel.** |
| 5 | `kBypassLPHP` | LP/HP Bypass | On/Off | Bypass HPF/LPF. **Entrambe le modalità:** Channel = 4K-EQ HPF/LPF, Mix bus = LP/HP attivi quando fattibile. |
| 6 | `kBypassEQ` | EQ Bypass | On/Off | Bypass stadio EQ (4K-EQ in Channel, Pultec in Mix bus). |
| 7 | `kBypassCompression` | Compression Bypass | On/Off | Bypass compressione (Multi-Comp in Channel, Multi-Comp Bus in Mix bus). |
| 8 | `kBypassDrive` | Drive Bypass | On/Off | Bypass stadio Drive (TapeHack2/NAM). |
| 9 | `kBypassLimiter` | Limiter Bypass | On/Off | Bypass limiter (BlockParty). |
| 10 | `kBypassGate` | Gate Bypass | On/Off | Bypass gate (Denoise). |
| 11 | `kGainStagingTrim` | Gain Staging Trim | % | Trim gain staging (0…1 → ~0.1×…2×). **Solo Channel.** |
| 12 | `kCompressAmount` | Compress Amount | % | Amount compressione (Multi-Comp). |
| 13 | `kCompressMix` | Compress Mix | % | Dry/wet compressione (0 = dry, 1 = wet). |
| 14 | `kCompressorMode` | Compressor Mode | Voice / Drums / Classic | Solo Channel: 0=Voice (Opto), 1=Drums (FET), 2=Classic (dbx 160). Mix bus ignora; engine = Bus. |
| 15 | `kEqHPFFreq` | EQ HPF Freq | Hz | Frequenza high-pass 4K-EQ (20…500 Hz). **Channel.** |
| 16 | `kEqHPFOn` | EQ HPF On | On/Off | Abilita HPF 4K-EQ. **Channel.** |
| 17 | `kEqLPFFreq` | EQ LPF Freq | Hz | Frequenza low-pass 4K-EQ (3k…20k Hz). **Channel.** |
| 18 | `kEqLPFOn` | EQ LPF On | On/Off | Abilita LPF 4K-EQ. **Channel.** |
| 19 | `kEqLFGain` | EQ LF Gain | dB | Gain banda LF (−20…+20 dB). **Channel.** |
| 20 | `kEqLFFreq` | EQ LF Freq | Hz | Frequenza banda LF (30…480 Hz). **Channel.** |
| 21 | `kEqLFBell` | EQ LF Bell | Shelf/Bell | 0 = shelf, 1 = bell (solo Black). **Channel.** |
| 22 | `kEqLMGain` | EQ LM Gain | dB | Gain banda LM (−20…+20 dB). **Channel.** |
| 23 | `kEqLMFreq` | EQ LM Freq | Hz | Frequenza banda LM (200…2500 Hz). **Channel.** |
| 24 | `kEqLMQ` | EQ LM Q | — | Q banda LM (0.4…4). **Channel.** |
| 25 | `kEqHMGain` | EQ HM Gain | dB | Gain banda HM (−20…+20 dB). **Channel.** |
| 26 | `kEqHMFreq` | EQ HM Freq | Hz | Frequenza banda HM (600…7000 Hz). **Channel.** |
| 27 | `kEqHMQ` | EQ HM Q | — | Q banda HM (0.4…4). **Channel.** |
| 28 | `kEqHFGain` | EQ HF Gain | dB | Gain banda HF (−20…+20 dB). **Channel.** |
| 29 | `kEqHFFreq` | EQ HF Freq | Hz | Frequenza banda HF (1.5k…16k Hz). **Channel.** |
| 30 | `kEqHFBell` | EQ HF Bell | Shelf/Bell | 0 = shelf, 1 = bell (solo Black). **Channel.** |
| 31 | `kEqSaturation` | EQ Saturation | % | Saturazione console 4K (0…100%), **ultimo passo dell'EQ** (manopola SATURATE; serie G in Channel, E in Mix bus; si spegne con l'IN dell'EQ). Dal 2026-10-14 al 2026-10-24 era il motore alternativo del Drive. |
| 32 | `kDriveAmount` | Drive Amount | 0…1 | Quantità del Drive con il NAM acceso: 0 = unità (solo la colorazione del modello), 1 = +20 dB nel modello; uscita compensata. Senza modello il Drive è in passthrough. |
| 33 | `kLimiterPound` | Limiter Pound | 0…1 | Quantità del limiter (BlockParty "pound"). Default 0.5. |
| 34 | `kGateThreshold` | Gate Threshold | 0…1 | Soglia del gate (Denoise). Default 0.0. |
| 35 | `kDriveNamOn` | Drive NAM | On / Off | Motore NAM del Drive acceso (1) o saltato (0), pulsante NAM. Default 1. Fino al 2026-10-24: 1 = NAM, 0 = saturazione 4K. |
| 36 | `kDriveTapeOn` | Drive Tape | On / Off | Motore TAPE del Drive acceso (1) o saltato (0), pulsante TAPE. Default 1; spento negli stati salvati prima della v12. |
| 37 | `kDriveTapeAmount` | Tape | 0…10 | Una manopola sopra slam e head bump (Airwindows Tape); voicing Tracking in Channel, Mixdown in Mix bus; uscita compensata. Default 0. |

---

## Raggruppamento per stadio

- **Globali:** `kBypass`, `kStripMode`, `kInputGain`, `kOutputGain`.
- **Gain staging (solo Channel, inerte):** `kBypassGainStaging`, `kGainStagingTrim`.
- **LP/HP:** `kEqHPFFreq`, `kEqLPFFreq`; nell'editor **un solo pulsante IN** per entrambi i filtri (`kBypassLPHP`, polarità invertita). `kEqHPFOn`/`kEqLPFOn` restano a 1 (default, neutro: 20 Hz / 20 kHz) senza controllo.
- **EQ (4K-EQ in entrambe le modalità):** `kBypassEQ`, i parametri delle quattro bande e `kEqSaturation` (SATURATE, l'ultima manopola).
- **Compressione:** `kBypassCompression`, `kCompressAmount`, `kCompressMix`, `kCompressorMode` (solo Channel: Voice / Drums / Classic). Vedi [COMPRESSION_MODES.md](COMPRESSION_MODES.md).
- **Drive (NAM → TAPE in serie):** `kBypassDrive` (IN del modulo), `kDriveNamOn` e `kDriveAmount` (NAM, manopola DRIVE), `kDriveTapeOn` e `kDriveTapeAmount` (TAPE). Il blocco sta subito dopo l'Input in Channel e tra Compression e Limiter in Mix bus. Il modello si sceglie dal popup sulla striscia di nastro (non è un parametro: il percorso sta nello stato). Vedi [DRIVE_STAGE_PARAMS.md](DRIVE_STAGE_PARAMS.md).
- **Limiter / Gate:** `kBypassLimiter`/`kLimiterPound`, `kBypassGate`/`kGateThreshold`.

Nell'editor i pulsanti **IN** mostrano l'inverso dei `kBypass*` (acceso = modulo inserito); gli ID e i valori dei parametri non cambiano.

## Parametri di meter (da `kMeterBase = 1000`, non fanno parte dei 35)

Scritti dal processor (al massimo ogni 16 ms), in sola lettura, mai salvati nello stato. Quasi tutti nascosti all'host; i tre di **gain reduction** (`Compressor Gain Reduction`, `Limiter Gain Reduction`, `Gate Gain Reduction`; quello del gate è una stima dai picchi con rilascio veloce) sono in sola lettura ma **visibili**, nella codifica richiesta da SSL 360 Link: 0 = nessuna riduzione, dB = 20·log10(1 − valore) (vedi [FEATURES.md](FEATURES.md) §4). Gli altri: picchi In/Out L/R (−60…+6 dBFS), sei LED di clip per stadio (0/1) e `kClearClips` (scrivibile: un clic su un LED li azzera tutti).
