# The Stripper — Controlli (parametri) modificabili da UI

Elenco di **tutti i parametri** esposti al host (lista standard, nessuna UI custom). L’host (es. Reaper) li mostra nella sua finestra parametri. Ordine **per ID** (come appare nel host).

**Totale: 33 parametri** (ID 0…32). **Compressor Mode** (ID 14, solo Channel: Voice / Drums / Classic) è esposto; DSP Multi-Comp integrato (port C++ da Dusk Audio), vedi [COMPRESSION_MODES.md](COMPRESSION_MODES.md).

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
| 31 | `kEqType` | EQ Type | Brown/Black | 0 = Brown (E-Series), 1 = Black (G-Series). **Channel.** |
| 32 | `kEqSaturation` | EQ Saturation | % | Saturazione SSL 4K-EQ (0…100%). **Channel.** |

---

## Raggruppamento per stadio

- **Globali:** Bypass, Strip Mode, Input Gain, Output Gain.
- **Gain staging (solo Channel):** Bypass, Trim (parametri 4, 11).
- **LP/HP (entrambe le modalità quando fattibile):** Bypass LP/HP (5), EQ HPF Freq/On (14–15), EQ LPF Freq/On (16–17). Channel: full 4K-EQ; Mix bus: solo HPF/LPF (bande flat).
- **EQ (Channel = 4K-EQ, Mix bus = Pultec stub):** Bypass EQ (6), parametri 14–31 (in Channel); in Mix bus LP/HP attivi (stesso 5, 14–17), bande EQ non usate.
- **Compressione:** Bypass (7), Amount (12), Mix (13), **Compressor Mode** (14, solo Channel: Voice / Drums / Classic). Vedi [COMPRESSION_MODES.md](COMPRESSION_MODES.md).
- **Drive / Limiter / Gate:** solo bypass (8, 9, 10); stadi ancora stub (passthrough).

---

## Fonte / mappatura per stadio

| Stadio | Channel | Mix bus | Stato |
|--------|---------|---------|--------|
| Input/Output Gain, Bypass, Strip Mode | In-house | In-house | ✅ |
| Gain Staging | In-house (trim) | — | ✅ |
| LP/HP | 4K-EQ (HPF/LPF) | 4K-EQ HPF/LPF only (bande flat) | ✅ Both modes |
| EQ | **4K-EQ** (Dusk Audio, ex Luna Co., GPL-3.0) | **Pultec** (stub) | ✅ Channel / ⏳ Mix bus |
| Compressione | **Multi-Comp** (Dusk Audio, GPL-3.0) | Channel: usage modes (Voice, Drums, Classic); Mix bus: Bus only | ✅ Channel / ✅ Mix bus |
| Drive | TapeHack2 / NAM | TapeHack2 / NAM | ⏳ Stub |
| Limiter | BlockParty (Airwindows) | BlockParty | ⏳ Stub |
| Gate | Denoise (Airwindows) | Denoise | ⏳ Stub |

---

## In sospeso (da implementare)

1. **EQ Mix bus** — Stadio EQ in modalità Mix bus: integrare **Pultec EQP-WDF-1A** (o equivalente); oggi passthrough.
2. **Compressione Mix bus** — Integrare **Logical4** (Airwindows) e mappare Amount/Mix sui suoi parametri; oggi passthrough.
3. **Drive** — Integrare **TapeHack2** (classic) e/o **NAM** (Tone3000 / file locale); oggi passthrough.
4. **Limiter** — Integrare **BlockParty** (Airwindows); oggi passthrough.
5. **Gate** — Integrare **Denoise** (Airwindows); oggi passthrough.
6. **UI custom** (opzionale) — Grafica Fairy Tapes, nascondere LP/HP e parametri 4K-EQ in Mix bus, texture per istanza, ecc. (doc in `docs/UI_*.md`).
7. **SSL 360 Link** — Compatibilità mappatura plugin esterni (vedi FEATURES.md §4).
8. **Licenza** — Il progetto è sotto **GPL-3.0** (4K-EQ e Multi-Comp, Dusk Audio). Testo completo in [LICENSE](../../../LICENSE).
