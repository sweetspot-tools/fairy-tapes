# The Stripper — Parametri: previsti, attuali, plugin esterni, mancanti

Riepilogo in tabelle: parametri di design (FEATURES.md), parametri implementati, corrispondenza con i plugin esterni e parametri mancanti.

---

## 1. Parametri previsti (design)

Da FEATURES.md: layout della strip, stage bypassabili, due modalità (Channel / Mix bus), semplificazione "rock n roll" (no Solo/Mute per band, §1.1).

| Stage / ambito | Parametri previsti | Note (FEATURES) |
|----------------|-------------------|------------------|
| **Globale** | Bypass, Strip Mode (Channel / Mix bus), Input Gain, Output Gain | Un solo strip mode per istanza; gain in dB. |
| **Gain staging** | Bypass, Trim | Channel only. RMS readout in UI (non necessariamente un param). |
| **LP/HP** | Bypass, HPF Freq, HPF On, LPF Freq, LPF On | Attivi in entrambe le modalità quando fattibile (Channel: 4K-EQ; Mix bus: solo HPF/LPF 4K-EQ). Frequenze stepped. |
| **EQ (Channel)** | 4 bande: Low (Freq, Gain, Bell), LM (Gain, Freq, Q), HM (Gain, Freq, Q), High (Gain, Freq, Bell); Saturation | 4K-EQ Brown (automatic). No Solo/Mute per band (UI semplificata, §1.1). |
| **EQ (Mix bus)** | Same 4K-EQ controls as Channel; type auto = Brown (E-series) | 4K-EQ Brown; stessi parametri kEq* del Channel. |
| **Compression** | Amount, Mix (dry/wet) | Un param "amount" e mix; engine per mode (Multi-Comp). |
| **Drive** | Bypass; opzionale: amount / character | TapeHack2 o NAM; design permette "pochi param musicali". |
| **Limiter** | Bypass; opzionale: ceiling / input | BlockParty; può restare solo bypass. |
| **Gate** | Bypass; opzionale: threshold, range | Denoise; può restare solo bypass. |

---

## 2. Parametri attuali (implementati)

Fonte: `params.h` + `controller.cpp` + uso in `processor.cpp`. Totale: **35** parametri (`kNumParams`), più i parametri di meter da `kMeterBase = 1000` (vedi [CONTROLS.md](CONTROLS.md)).

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
| kCompressAmount | Compress Amount | — | Multi-Comp |
| kCompressMix | Compress Mix | — | Multi-Comp |
| kEqHPFFreq | EQ HPF Freq | Hz | 4K-EQ (Channel) |
| kEqHPFOn | EQ HPF On | — | 4K-EQ |
| kEqLPFFreq | EQ LPF Freq | Hz | 4K-EQ |
| kEqLPFOn | EQ LPF On | — | 4K-EQ |
| kEqLFGain, kEqLFFreq, kEqLFBell | EQ LF Gain/Freq/Bell | dB, Hz, — | 4K-EQ Low |
| kEqLMGain, kEqLMFreq, kEqLMQ | EQ LM Gain/Freq/Q | dB, Hz, — | 4K-EQ Mid Low |
| kEqHMGain, kEqHMFreq, kEqHMQ | EQ HM Gain/Freq/Q | dB, Hz, — | 4K-EQ Mid High |
| kEqHFGain, kEqHFFreq, kEqHFBell | EQ HF Gain/Freq/Bell | dB, Hz, — | 4K-EQ High |
| kEqSaturation | EQ Saturation | % | 4K-EQ |
| kDriveAmount | Drive Amount | — | Drive (NAM) |
| kLimiterPound | Limiter Pound | — | Limiter (BlockParty) |
| kGateThreshold | Gate Threshold | — | Gate (Denoise) |

Solo/Mute per banda non previsti. `kEqType` è stato rimosso (il tipo deriva da Strip Mode).

---

## 3. Plugin esterni corrispondenti

Per ogni stage, quale libreria/plugin esterno è usato (o previsto) e quali parametri vi si mappano.

| Stage | Modalità | Plugin esterno | Parametri attuali mappati | Note |
|-------|----------|----------------|----------------------------|------|
| **EQ + LP/HP** | Channel | **4K-EQ** (Dusk Audio, ex luna-co-software) — controlli **attivi** in Channel | kBypassLPHP, kBypassEQ, kEq* (tutti 4K-EQ) | In Channel: 4K-EQ attivo. LP/HP attivi (da 4K-EQ). Upstream: https://github.com/dusk-audio/dusk-audio-plugins. |
| **EQ** | Mix bus | **4K-EQ Brown** (E-series, automatic) | Same kEq* params as Channel; type auto = Brown | In Mix bus: stessi controlli 4K-EQ del Channel, tipo forzato Brown (E-series). |
| **Compression** | Channel | **Multi-Comp** (Dusk Audio, GPL-3.0) — Voice/Drums/Classic modes | kBypassCompression, kCompressAmount, kCompressMix | Multi-band compression con modalità per contesto. |
| **Compression** | Mix bus | **Multi-Comp Bus** (Dusk Audio, GPL-3.0) — SSL G-Series | kBypassCompression, kCompressAmount, kCompressMix | SSL-style 2-buss. |
| **Drive** | — | **NAM with GPU extension** (primary) o **TapeHack2** (Airwindows) | kBypassDrive | NAM deve girare su GPU (gpuaudio-sdk); TapeHack2 quando NAM non usato. |
| **Limiter** | — | **BlockParty** (Airwindows) | kBypassLimiter | Nessun param ceiling/input. |
| **Gate** | — | **Denoise** (Airwindows) | kBypassGate | Nessun param threshold/range. |
| **Gain staging** | Channel | In-house (RMS + trim) | kBypassGainStaging, kGainStagingTrim | **Non implementato**; solo in Channel. |

---

## 4. Parametri mancanti

Rispetto al design (FEATURES.md) e all'uso dei plugin esterni.

| Parametro / gruppo | Previsto da | Priorità | Note |
|--------------------|-------------|----------|------|
| **Drive amount / tone** | Opzionale (§1.2a) | Bassa | Se si espone "un amount" per TapeHack2. |
| **Limiter ceiling / input** | Opzionale | Bassa | Se si espone un controllo da BlockParty. |
| **Gate threshold / range** | Opzionale | Bassa | Se si espone un controllo da Denoise. |

In sintesi: nessun parametro obbligatorio mancante. I parametri opzionali riguardano Drive, Limiter e Gate (se si espandono gli stage). Solo/Mute per band non sono previsti (§1.1).

---

## 5. Riepilogo numerico

| Voce | Valore |
|------|--------|
| Parametri attuali totali | **32** |
| Parametri previsti (design) | ~32 + eventuali Drive/Limiter/Gate opzionali (no Solo/Mute per band) |
| Parametri mancanti obbligatori | Nessuno |
| Parametri mancanti opzionali | Drive amount, Limiter, Gate (se si espandono gli stage) |

---

*Ultimo aggiornamento: in base a FEATURES.md, params.h, controller.cpp e processor.cpp.*
