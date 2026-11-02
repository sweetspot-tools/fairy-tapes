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

**Parameter:** One enum (or stepped) parameter **Compressor Mode** (Channel only), values 0…2 → Voice, Drums, Classic. In Mix bus mode the editor hides the three buttons and the processor ignores the parameter (its value is kept); the engine is always Bus.

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

## Sidechain (external)

The plugin exposes a **stereo sidechain input** (second audio bus). When the host routes a signal to this bus (e.g. kick to duck the bass), the compressor uses that signal **only for envelope detection**; gain reduction is still applied to the main input. So you get classic sidechain ducking or “key” compression.

- **Bus layout:** Main Input (stereo) + optional **Sidechain** (stereo). If the host uses only one input bus, detection is internal (main signal). If two input busses are active, the second is the sidechain.
- **All four engines** (Opto, FET, VCA, Bus) support external sidechain: when sidechain is present, detection is driven by the sidechain; when not connected, detection uses the main input.
- **Bus engine:** External sidechain is high-pass filtered at 60 Hz (same as internal detection) for consistent behaviour.

No extra parameter is required: use the host’s routing to connect or disconnect the sidechain.

---

## Implementation note

**Current:** The compression stage uses a **C++ port of Dusk Audio Multi-Comp** DSP including **hardware emulation** (`dsp/multicomp_engines.cpp`, `dsp/hardware_emulation/`, `dsp/compression_stub.cpp`). Algorithms: **Opto** (T4B + input/output transformer + 12BH7 tube), **FET** (1176-style + input/output transformer), **VCA** (dbx 160, clean, no transformer), **Bus** (SSL-style + input/output transformer). Transformer saturation, harmonics, HF rolloff and DC blocking; tube stage (Opto only). **External sidechain** is supported for all modes (envelope detection from second input bus when present). No JUCE; real-time safe. Source: `vendors/dusk-audio-plugins/plugins/multi-comp/` (GPL-3.0; reference submodule, not fetched by a normal clone: `git submodule update --init --checkout vendors/dusk-audio-plugins`). Registered in [THIRD_PARTY.md](../../../THIRD_PARTY.md).
