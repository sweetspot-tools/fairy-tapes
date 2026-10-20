# The Stripper — Parameters per stage (source reference)

Reference list of parameters from each chosen plugin, as they appear in the **source** (Airwindows, etc.). Use this to decide what to **expose in our UI** — we can simplify (fewer knobs, combined controls) per §1.2a FEATURES.md.

**Legend:** “In source” = parameters in the original plugin. “Proposed for UI” = suggestion for our strip (simple, rock n roll); to be confirmed.

---

## 1. Gain staging (in-house) — Channel mode only

**Note:** This stage is **present only in Channel mode**. In Mix bus mode the gain-staging block is not in the chain (FEATURES §1.2).

| Param | Role | Proposed for UI |
|-------|------|------------------|
| **Trim** (or **Gain**) | Level in/out or target level | Yes — one knob (or fader). |
| **RMS** | Readout of current RMS level | Display only (no automation). |
| *(Optional)* **Target RMS** | If we implement “set level to X dBFS” | Optional; otherwise trim only. |

**Total proposed:** 1 automated (Trim/Gain) + 1 readout (RMS).

---

## 2. LP/HP — Biquad (Airwindows)

We expose **only two cutoff frequencies** (HP, LP) + bypass. Q/slope **fixed in code** (e.g. Butterworth). **Frequencies are stepped** (FEATURES §1.2b).

| Param | Role | Proposed for UI |
|-------|------|------------------|
| **HP Freq** | High-pass cutoff | **Stepped** (e.g. Off, 30, 40, 60, 80, 100, 120, 150, 200 Hz). |
| **LP Freq** | Low-pass cutoff | **Stepped** (e.g. Off, 4k, 6k, 8k, 10k, 12k, 14k, 16k, 18k, 20k Hz). |
| **Bypass** | Stage on/off | Yes. |

**Total proposed:** 2 stepped freqs + 1 bypass. (Q/slope not exposed.)

---

## 3. EQ — Analog-style, 2 shelves + 2 mid bands, all stepped (FEATURES §1.2b, §1.2c, §1.2c.1)

**Strip modes:** **Channel** = **4K-EQ** (EQ + LP/HP) + **ButterComp** + **gain staging**. **Mix bus** = **Pultec (EQP-WDF-1A)** + **Logical4** (SSL-style 2-buss); **no gain staging**. See FEATURES §1.2c.1, §3.3.1.

**Design:** EQ stage uses **one engine per mode** (4K-EQ or Pultec). Layout (band topology may differ per engine; 4K-EQ has 4 parametric + HP/LP, Pultec has EQP-1A bands). Stepped controls where applicable. Layout: **Low shelf**, **Mid 1** (bell), **Mid 2** (bell), **High shelf**. All controls **stepped** (Freq, Gain, Q where applicable). Solo/Mute per band (FEATURES §1.1).

**Algorithm source (FEATURES §1.2c):** **4K-EQ** (SSL 4000 style) is the **primary candidate** — analog, full channel EQ (4 parametric + HPF/LPF, saturation). Neve not available open-source; Pultec (EQP-WDF-1A) excluded (colour only, not channel EQ). Confirm by listening.

| Band     | Type  | Param   | Role              | Proposed for UI |
|----------|-------|---------|-------------------|------------------|
| **Low**  | Shelf | Freq    | Shelf turnover    | **Stepped** (e.g. 35, 60, 110, 220, 400 Hz). |
|          |       | Gain    | Boost/cut         | **Stepped** (e.g. ±12 dB in steps). |
|          |       | Solo    | Solo band         | Yes. |
|          |       | Mute    | Mute band         | Yes. |
| **Mid 1**| Bell  | Freq    | Centre frequency  | **Stepped** (e.g. 200, 300, 400, 500, 700, 1k, 1.5k Hz). |
|          |       | Gain    | Boost/cut         | **Stepped** (e.g. ±12 dB). |
|          |       | Q       | Bandwidth         | **Stepped** (e.g. narrow / mid / wide, or 0.5–2 in steps). |
|          |       | Solo    | Solo band         | Yes. |
|          |       | Mute    | Mute band         | Yes. |
| **Mid 2**| Bell  | Freq    | Centre frequency  | **Stepped** (e.g. 800, 1.2k, 1.6k, 2k, 2.5k, 3.2k, 4k, 5k Hz). |
|          |       | Gain    | Boost/cut         | **Stepped** (e.g. ±12 dB). |
|          |       | Q       | Bandwidth         | **Stepped** (same as Mid 1). |
|          |       | Solo    | Solo band         | Yes. |
|          |       | Mute    | Mute band         | Yes. |
| **High** | Shelf | Freq    | Shelf turnover    | **Stepped** (e.g. 3.2k, 4.8k, 7k, 10k, 12k, 16k Hz). |
|          |       | Gain    | Boost/cut         | **Stepped** (e.g. ±12 dB). |
|          |       | Solo    | Solo band         | Yes. |
|          |       | Mute    | Mute band         | Yes. |
| —        | —     | Bypass  | Stage on/off      | Yes. |

**No Console Type / Drive / Output** in UI — one character only (analog-style, 4K-EQ).

**Total proposed:** Low (Freq, Gain, Solo, Mute) + Mid1 (Freq, Gain, Q, Solo, Mute) + Mid2 (Freq, Gain, Q, Solo, Mute) + High (Freq, Gain, Solo, Mute) + Bypass. Exact step values TBD at implementation (1073/1084 tables as reference).

---

## 4. Compression — ButterComp (Channel) / Logical4 (Mix bus) (Airwindows) ✓ chosen

**Channel mode:** **ButterComp (Airwindows) — 2 params:**

| Param | In source | Role | Proposed for UI |
|-------|-----------|------|------------------|
| **Compress** | 0–1 | Amount of compression | Yes — “Amount” or “Compress”. |
| **Dry/Wet** | 0–1 | Mix | Yes — “Mix” or “Dry/Wet”. |

**Total in source (ButterComp):** 2. **Proposed:** 2 (ideal for “one knob + mix” simplicity).

**Mix bus mode:** **Logical4** (Airwindows) — SSL-style 2-buss. Params TBD from source (same simple UI idea: amount + mix). **Summary:** Channel = ButterComp; Mix bus = Logical4. See FEATURES §3.3.1.

---

## 5. Gain / Drive — TapeHack2 (classic) or NAM

**TapeHack2 (Airwindows) — 3 params:**

| Param | In source | Role | Proposed for UI |
|-------|-----------|------|------------------|
| **Input** | 0–1 | Input gain/drive | Yes — “Input” or “Drive”. |
| **Output** | 0–1 | Output level | Yes — “Output”. |
| **Dry/Wet** | 0–1 | Mix | Yes — “Mix” or “Dry/Wet”. |

**Total in source:** 3. **Proposed:** 3.

**NAM (when enabled):** typically **Input gain**, **Output gain**, optionally **Mix**. Model selection and load are separate from “params” (file picker / Tone3000 UI). **Proposed:** Input, Output, (Mix) — same idea as TapeHack2 for consistency.

---

## 6. Limiter — BlockParty (Airwindows)

| Param | In source | Role | Proposed for UI |
|-------|-----------|------|------------------|
| **Pound** | 0–1 | Limiter amount/ceiling | Yes — “Limit” or “Amount”. |
| **Dry/Wet** | 0–1 | Mix | Yes — “Mix” or “Dry/Wet”. |

**Total in source:** 2. **Proposed:** 2.

---

## 7. Gate / Expander — Denoise (Airwindows)

| Param | In source | Role | Proposed for UI |
|-------|-----------|------|------------------|
| **High** | 0–1 | Threshold/sensitivity high band | Simplify: see below. |
| **HMid** | 0–1 | High-mid | |
| **Mid** | 0–1 | Mid | |
| **LMid** | 0–1 | Low-mid | |
| **Bass** | 0–1 | Bass | |
| **Sub** | 0–1 | Sub | |
| **Speed** | 0–1 | Attack/release or speed | Yes — keep as “Speed”. |
| **Dry/Wet** | 0–1 | Mix | Yes — “Mix”. |

**Total in source:** 8. **Proposed for UI (simple):** **Threshold** (one control, or link the 6 bands), **Speed**, **Dry/Wet** = 3 params. We can map a single “Threshold” to the internal band logic or expose a simplified “sensitivity” so the gate stays easy to use.

---

## 8. Bypass per stage

Each stage has **Bypass** (on/off). Not listed in the tables above; count as 1 bool per stage in the parameter list.

---

## Summary (proposed counts for “simple” UI)

| Stage | Params in source | Proposed exposed (approx.) |
|-------|------------------|----------------------------|
| Gain staging | — | 1 (Trim) + RMS readout |
| LP/HP (Biquad) | 4 | 4 (Type, Freq, Q, Wet) |
| EQ | 3 (Channel9) or N bands | 3 bands × (Freq, Gain, Q, Solo, Mute) or simplified |
| Compression (ButterComp / Logical4) | 2 per engine | 2 (Amount, Mix) — Channel = ButterComp; Mix bus = Logical4 |
| Gain/Drive (TapeHack2) | 3 | 3 (Input, Output, Mix) |
| Limiter (BlockParty) | 2 | 2 (Amount, Mix) |
| Gate (Denoise) | 8 | 3 (Threshold, Speed, Mix) |
| **+ Bypass per stage** | — | 7 (one per stage) |

Exact parameter IDs, normalization (0–1), units, and labels to be defined in the VST3 processor and editor; this document is the reference for **what** to expose before we lock the parameter list.
