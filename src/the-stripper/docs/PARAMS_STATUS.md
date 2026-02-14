# The Stripper — Parametri: previsti, attuali, plugin esterni, mancanti

Riepilogo in tabelle: parametri di design (FEATURES.md), parametri implementati, corrispondenza con i plugin esterni e parametri mancanti.

---

## 1. Parametri previsti (design)

Da FEATURES.md: layout della strip, stage bypassabili, due modalità (Channel / Mix bus), semplificazione “rock n roll” (no Solo/Mute per band, §1.1).

| Stage / ambito | Parametri previsti | Note (FEATURES) |
|----------------|-------------------|------------------|
| **Globale** | Bypass, Strip Mode (Channel / Mix bus), Input Gain, Output Gain | Un solo strip mode per istanza; gain in dB. |
| **Gain staging** | Bypass, Trim | Channel only. RMS readout in UI (non necessariamente un param). |
| **LP/HP** | Bypass, HPF Freq, HPF On, LPF Freq, LPF On | Attivi in entrambe le modalità quando fattibile (Channel: 4K-EQ; Mix bus: solo HPF/LPF 4K-EQ). Frequenze stepped. |
| **EQ (Channel)** | 4 bande: Low (Freq, Gain, Bell), LM (Gain, Freq, Q), HM (Gain, Freq, Q), High (Gain, Freq, Bell); Type (Brown/Black); Saturation | 4K-EQ. No Solo/Mute per band (UI semplificata, §1.1). |
| **EQ (Mix bus)** | Controlli Pultec EQP-1A (boost/cut, freq low, boost/cut high, freq high, ecc.) | EQP-WDF-1A; strip più semplice, no LP/HP. |
| **Compression** | Amount, Mix (dry/wet) | Un param “amount” e mix; engine per mode (ZLCompressor / Logical4). |
| **Drive** | Bypass; opzionale: amount / character | TapeHack2 o NAM; design permette “pochi param musicali”. |
| **Limiter** | Bypass; opzionale: ceiling / input | BlockParty; può restare solo bypass. |
| **Gate** | Bypass; opzionale: threshold, range | Denoise; può restare solo bypass. |

---

## 2. Parametri attuali (implementati)

Fonte: `params.h` + `controller.cpp` + uso in `processor.cpp`. Totale: **42** parametri (`kNumParams`).

| ID (nome interno) | Titolo (host) | Unità | Plugin/stage |
|-------------------|---------------|-------|--------------|
| kBypass | Bypass | — | Globale |
| kStripMode | Strip Mode | — | Globale |
| kInputGain | Input Gain | dB | Globale |
| kOutputGain | Output Gain | dB | Globale |
| kBypassGainStaging | Gain Staging Bypass | — | Gain staging (Channel) |
| kBypassLPHP | LP/HP Bypass | Both modes | LP/HP (4K-EQ; Mix bus: HPF/LPF only) |
| kBypassEQ | EQ Bypass | — | EQ |
| kBypassCompression | Compression Bypass | — | Compression |
| kBypassDrive | Drive Bypass | — | Drive |
| kBypassLimiter | Limiter Bypass | — | Limiter |
| kBypassGate | Gate Bypass | — | Gate |
| kGainStagingTrim | Gain Staging Trim | — | Gain staging |
| kCompressAmount | Compress Amount | — | ZLCompressor / Logical4 |
| kCompressMix | Compress Mix | — | ZLCompressor / Logical4 |
| kEqHPFFreq | EQ HPF Freq | Hz | 4K-EQ (Channel) |
| kEqHPFOn | EQ HPF On | — | 4K-EQ |
| kEqLPFFreq | EQ LPF Freq | Hz | 4K-EQ |
| kEqLPFOn | EQ LPF On | — | 4K-EQ |
| kEqLFGain, kEqLFFreq, kEqLFBell | EQ LF Gain/Freq/Bell | dB, Hz, — | 4K-EQ Low |
| kEqLMGain, kEqLMFreq, kEqLMQ | EQ LM Gain/Freq/Q | dB, Hz, — | 4K-EQ Mid Low |
| kEqHMGain, kEqHMFreq, kEqHMQ | EQ HM Gain/Freq/Q | dB, Hz, — | 4K-EQ Mid High |
| kEqHFGain, kEqHFFreq, kEqHFBell | EQ HF Gain/Freq/Bell | dB, Hz, — | 4K-EQ High |
| kEqType | EQ Type | — | 4K-EQ Brown/Black |
| kEqSaturation | EQ Saturation | % | 4K-EQ |

Nessun parametro esposto per: **Pultec (Mix bus EQ)**, **Drive** (oltre bypass), **Limiter** (oltre bypass), **Gate** (oltre bypass). Solo/Mute per band non previsti (UI semplificata, §1.1).

---

## 3. Plugin esterni corrispondenti

Per ogni stage, quale libreria/plugin esterno è usato (o previsto) e quali parametri vi si mappano.

| Stage | Modalità | Plugin esterno | Parametri attuali mappati | Note |
|-------|----------|----------------|----------------------------|------|
| **EQ + LP/HP** | Channel | **4K-EQ** (Dusk Audio, ex luna-co-software) — controlli **attivi** in Channel | kBypassLPHP, kBypassEQ, kEq* (tutti 4K-EQ) | In Channel: 4K-EQ attivo, Pultec disattivo. LP/HP attivi (da 4K-EQ). Upstream: https://github.com/dusk-audio/dusk-audio-plugins. |
| **EQ** | Mix bus | **EQP-WDF-1A** (Pultec) — controlli **attivi** in Mix bus; 4K-EQ **disattivi** | *(nessuno; Pultec non impl.)* | In Mix bus: Pultec attivo (stub), 4K-EQ band disattivi. LP/HP restano attivi in entrambe le modalità se fattibile. |
| **Compression** | Channel | **ZLCompressor** (ZL-Audio, AGPL-3.0) | kBypassCompression, kCompressAmount, kCompressMix | Clean style, bus-oriented. |
| **Compression** | Mix bus | **Logical4** (Airwindows, MIT) — clone SSL bus compressor | kBypassCompression, kCompressAmount, kCompressMix | SSL-style 2-buss. |
| **Drive** | — | **NAM with GPU extension** (primary) o **TapeHack2** (Airwindows) | kBypassDrive | NAM deve girare su GPU (gpuaudio-sdk); TapeHack2 quando NAM non usato. |
| **Limiter** | — | **BlockParty** (Airwindows) | kBypassLimiter | Nessun param ceiling/input. |
| **Gate** | — | **Denoise** (Airwindows) | kBypassGate | Nessun param threshold/range. |
| **Gain staging** | Channel | In-house (RMS + trim) | kBypassGainStaging, kGainStagingTrim | **Non implementato**; solo in Channel. |

---

## 4. Parametri mancanti

Rispetto al design (FEATURES.md) e all’uso dei plugin esterni.

| Parametro / gruppo | Previsto da | Priorità | Note |
|--------------------|-------------|----------|------|
| **Parametri Pultec (Mix bus EQ)** | §1.2c.1, tabella EQ Mix bus | Alta | Low Boost/Cut, Low Freq, High Boost/Cut, High Freq (o set equivalente EQP-WDF-1A). Stage attualmente passthrough. |
| **Drive amount / tone** | Opzionale (§1.2a) | Bassa | Se si espone “un amount” per TapeHack2. |
| **Limiter ceiling / input** | Opzionale | Bassa | Se si espone un controllo da BlockParty. |
| **Gate threshold / range** | Opzionale | Bassa | Se si espone un controllo da Denoise. |

In sintesi: **obbligatorio** per il design attuale è il **set parametri EQ Mix bus (Pultec)**; il numero esatto dipende dal set scelto per EQP-WDF-1A. Solo/Mute per band non sono previsti (§1.1).

---

## 5. Riepilogo numerico

| Voce | Valore |
|------|--------|
| Parametri attuali totali | **42** |
| Parametri previsti (design) | ~42 + set Pultec + eventuali Drive/Limiter/Gate opzionali (no Solo/Mute per band) |
| Parametri mancanti obbligatori | **Set Pultec** (Mix bus EQ, da definire) |
| Parametri mancanti opzionali | Drive amount, Limiter, Gate (se si espandono gli stage) |

---

*Ultimo aggiornamento: in base a FEATURES.md, params.h, controller.cpp e processor.cpp.*
