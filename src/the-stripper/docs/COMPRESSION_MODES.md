# Compression: usage-based modes (Multi-Comp)

The Stripper uses **one** compressor engine from **Dusk Audio Multi-Comp** (GPL-3.0). Which mode is shown and used depends on **strip mode**:

- **Channel mode:** User chooses one of **three** usage-based styles: **Voice**, **Drums**, **Classic**. Each label maps to one Multi-Comp algorithm. We show these three buttons, not the technical names (Opto, FET, VCA).
- **Mix bus mode:** Only **SSL Bus** is used. No mode selector; the compressor is fixed to the Bus algorithm.

So we always use the same Multi-Comp DSP; we only expose the modes that fit the context (channel vs mix bus).

---

## Channel mode: usage labels → Multi-Comp algorithm

| UI label (user-facing) | Multi-Comp mode | Typical use | Why this mapping |
|-------------------------|-----------------|-------------|------------------|
| **Voice**               | Vintage Opto (LA-2A) | Vocals, smooth leveling | Program-dependent attack/release, gentle, musical. Classic vocal choice. |
| **Drums**               | Vintage FET (1176)   | Drums, percussion, punch | Very fast attack, aggressive. Fits drums and transient-heavy material. |
| **Classic**             | Classic VCA (dbx 160)| Bass, punch, general | OverEasy soft knee, punchy; ideal for bass and all-round use. |

**Parameter:** One enum (or stepped) parameter **Compressor Mode** (Channel only), values 0…2 → Voice, Drums, Classic. In Mix bus mode this parameter is hidden or ignored; the engine is always Bus.

---

## Mix bus mode: Bus only

- **No** mode selector. The compression stage uses **only** the **SSL G-Series Bus** algorithm from Multi-Comp.
- Same Amount and Mix parameters as in Channel; character is fixed to “bus glue”.

---

## Summary

| Strip mode | Compressor engine | UI (mode selector) |
|------------|--------------------|----------------------|
| **Channel** | Multi-Comp (one of: Opto, FET, VCA dbx 160) | 3 buttons: **Voice**, **Drums**, **Classic** |
| **Mix bus** | Multi-Comp **Bus** only | No mode selector (fixed Bus) |

---

## Implementation note

**Current:** A simple in-house compressor in `dsp/compression_stub.cpp` provides the four characters (Voice, Drums, Classic, Bus) with mode-specific attack/release and ratio. No JUCE; real-time safe. Amount maps to threshold; Mix is dry/wet.

**Future:** The design target is **Dusk Audio Multi-Comp** (`vendors/dusk-audio-plugins`). A full port would map our enum Voice=0, Drums=1, Classic=2 to Multi-Comp’s Opto, FET, VCA; Bus only in Mix bus mode. The current simple compressor can be replaced by that port when needed.
