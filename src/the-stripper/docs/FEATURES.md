# The Stripper — Feature design & requirements

Design of the channel strip, NAM integration, algorithm sources, and external compatibility (SSL 360 Link).

**Quick summary:** See [OVERVIEW.md](OVERVIEW.md) for a one-page recap of all stage choices and where each is documented.

**Orientation:** The Stripper is a **bus-oriented** channel strip (e.g. mix bus, subgroup, master), not aimed at a single track only. Stage choices (e.g. **Multi-Comp** (Channel: usage modes; Mix bus: Bus only), **4K-EQ** in both modes (Channel = Black/G-series auto, Mix bus = Brown/E-series auto); **Gain/Drive** = **NAM with GPU extension**) reflect this bus use case.

---

## 1. Channel strip layout

The plugin is a **channel strip** with multiple stages in series. Each stage is **bypassable** (on/off per section). Two **strip modes** — **Channel** and **Mix bus** — select a coherent EQ + compressor personality each (see §1.2c.1).

| Stage            | Role                         | Bypassable |
|------------------|------------------------------|------------|
| **LP/HP**        | **Active in both modes** (from 4K-EQ HPF/LPF). Part of EQ stage. | Yes (both modes) |
| **EQ**           | **4K-EQ** in both modes — Channel = Black (G-series, auto), Mix bus = Brown (E-series, auto). §1.2c.1 | Yes        |
| **Compression**  | Multi-Comp (Channel: Voice, Drums, Classic; Mix bus: Bus only); **sidechain** (2nd input bus) for detection | Yes        |
| **Gain / Drive** | **Core stage:** NAM with GPU extension (Tone3000 / local); TapeHack2 when NAM off | Yes        |
| **Limiter**      | BlockParty (Airwindows)       | Yes        |
| **Gate / Expander** | Denoise (Airwindows)       | Yes        |
| **Gain staging** | Optional level/RMS trim      | **Channel mode only**; **not implemented** |

User can run only EQ, only NAM, EQ + NAM, or any combination. No stage is mandatory.

**Order of the chain (signal flow):**  
1. **Gain staging** (optional; **Channel mode only** — see §1.2; placement at start or end)  
2. **EQ** (4K-EQ in both modes; Channel = Black/G-series auto, Mix bus = Brown/E-series auto; LP/HP active in both modes — §1.2c.1)  
3. **Compression** (Multi-Comp in Channel with usage modes, Multi-Comp Bus only in Mix bus — §1.2c.1)  
4. **Gain / Drive** (NAM with GPU extension; TapeHack2 when NAM not used)  
5. **Limiter** (BlockParty)  
6. **Gate / Expander** (Denoise)  
7. **Gain staging** (if placed at end; **Channel mode only** — in Mix bus mode this stage is not present)

So the core flow is: **EQ → Compression → Drive → Limiter → Gate**. Gain staging is **Channel mode only**. **EQ stage:** **4K-EQ** is used in **both** modes; EQ type (Brown/Black) is **automatic** — **Channel** = Black (G-series, tighter Q), **Mix bus** = Brown (E-series, wider curves). **LP/HP** (from 4K-EQ HPF/LPF) remain active in both modes.

**EQ stage: which controls are active per mode.** **4K-EQ** controls (bands + HPF/LPF) are **active in both modes**. The EQ **type** (Brown = E-series / Black = G-series) is **derived automatically** from Strip Mode — no user-facing selector (kEqType parameter removed). **Channel** = Black (G-series, tighter Q, more precise for channel work). **Mix bus** = Brown (E-series, wider curves, musical, better for bus glue). LP/HP active in both modes.

### 1.2a Taking “pieces” and simplifying (rock n roll, easy to use)

We do **not** have to use each source plugin as a 1:1 copy. We can:

- **Take only the parts we need** from an algorithm (e.g. the core gain-reduction or saturation math) and drop the rest.
- **Expose fewer, more musical parameters** in our UI — e.g. one “amount” and one “character” or “speed” instead of every internal control — so the strip stays **easy to use** and “rock n roll” (fast to dial, hard to break).
- **Curate ranges** (min/max, curves) so the default is useful and extremes are musical.

So: we **can** take pieces, simplify, and make the channel strip more user-friendly; we are not bound to exposing every parameter of the original plugins.

### 1.2b Stepped frequencies (HP/LP and EQ bands)

**Which mixers have stepped (detented) frequencies?**

- **Neve** (1073, 1084, etc.): EQ frequencies are **stepped** — fixed values per band (e.g. 35, 60, 110, 220 Hz low; 3.2k, 4.8k, 7k, 10k, 12k Hz high). No continuous sweep.
- **API 550A / 550B:** **Stepped** frequency selectors per band (e.g. 50, 75, 100, 150, 200, 300, 400, 500 Hz and similar steps for mid/high).
- **SSL** (E/G series): HPF often **stepped** (e.g. 80, 100, 120 Hz or similar); EQ centre frequencies can be stepped on some models.
- **Trident, Mackie** (various): Some consoles have stepped HPF and/or EQ frequencies.

So **stepped** is very “banco analogico” and user-friendly: the fonico picks a step, no hunting for the exact Hz.

**Decision:** We use **stepped frequencies** for:
- **LP/HP:** HP cutoff and LP cutoff are **stepped** values (e.g. from 4K-EQ: HP 30–200 Hz, LP 4k–20k Hz). **LP/HP active in both strip modes** (from 4K-EQ HPF/LPF).
- **EQ bands:** Same idea — **stepped** centre frequency per band (and optionally stepped gain steps if we want). No per-band Solo/Mute (UI simplified).

So: **sì, frequenze a scatti sia per passa alto/basso che per le altre bande.**

### 1.2c EQ: analog-style, 2 shelves + 2 mid bands (one character per strip mode)

Channel9 in the source has **Console Type** (Neve / API / SSL / Teac / Mackie) + Drive + Output — it’s a “tone colour” box, not a multi-band EQ. To keep the strip **simple and one-voice** (like the compressor) and to deliver an **analog, harmonic EQ that sounds real**:

- **One EQ + one compressor per strip mode:** **Channel** = **4K-EQ** in both modes (4 parametric + HPF/LPF); EQ type **automatic**: Channel = **Black** (G-series, tighter Q) + **Multi-Comp** (usage modes: Voice, Drums, Classic). **Mix bus** = **Brown** (E-series, wider curves) + **Multi-Comp Bus** (SSL G-Series only). No user-facing EQ type selector (kEqType parameter removed). The **Channel / Mix bus** button switches EQ type and compression personality together. **Gain staging** is **Channel mode only** and **not implemented**. See §1.2c.1, §3.3.1, [COMPRESSION_MODES.md](COMPRESSION_MODES.md).

**Target: analog-style, “real” character — not necessarily Neve.**

- The EQ must follow the **analog line** of the plugin: warm, musical, harmonic, not digital/plastic. We are **not** tied to Neve 1073; we can choose **another “pasta”** (another hardware imitation) if we find a suitable open-source implementation: e.g. **API 550**, **SSL**, **Pultec**, **Quad Eight**, **MCI**, or other console/vintage EQ with one coherent character and topology that fits (ideally 2 shelf + 2 mid, or adaptable). Neve was a strong candidate for “analog, harmonic, vero” and 2 shelf + 2 mid topology; if a different flavour (API, SSL, Pultec, etc.) is available as open-source and sounds right, we can adopt that instead.

**EQ band layout (all controls stepped):**

| Band        | Type   | Controls (all stepped)     |
|-------------|--------|----------------------------|
| **Low**     | Shelf  | **Freq**, **Gain**          |
| **Mid 1**   | Bell   | **Freq**, **Gain**, **Q**   |
| **Mid 2**   | Bell   | **Freq**, **Gain**, **Q**   |
| **High**    | Shelf  | **Freq**, **Gain**          |

Stage **Bypass** only (no per-band Solo/Mute). No Console Type / Drive selector in UI.

**Algorithm source — open-source analog-style EQ (no in-house filter design)**

We need **2 shelves + 2 bells** (or similar) with **one consistent analog character**. We do **not** implement filter design in-house; we use an **existing open-source** EQ plugin whose algorithm we can port or embed. The character does **not** have to be Neve: any **analog-style** imitation that fits the plugin’s line (warm, real, harmonic) is acceptable — **Neve, API, SSL, Pultec, Quad Eight, MCI**, or other.

**Parametric (Airwindows):** Only 3 **peaking** bands (no shelves); mixing it with other sources would give inconsistent character. So we do not use Parametric as the EQ engine.

**Candidates (to find and evaluate)**

- **1073-pre-eq-plugin** (JackieAlacrity2373): Neve 1073–style, MIT, JUCE, VST3. Low shelf + mid peak + high shelf. **No known reviews/videos/docs** — evaluate by listening. Repository: https://github.com/JackieAlacrity2373/1073-pre-eq-plugin  
- **Other flavours:** Search for open-source **API 550**, **SSL**, **Pultec**, **Quad Eight**, or other console/vintage-style EQ (MIT/GPL-3.0, portable to our VST3). Prefer projects with some documentation or reputation; otherwise evaluate by listening before committing.  
- **Next step:** Broaden search to "open source [API 550 / SSL EQ / Pultec / console EQ] VST3" (or JUCE); shortlist candidates; **evaluate quality by listening** before choosing the algorithm source. If no suitable open-source EQ is found, revisit (e.g. in-house RBJ + colour stage).

**EQ candidates — search checklist and shortlist**

Suggested search queries (GitHub, search engines) to find alternative open-source analog-style EQs:

| Target flavour | Example queries |
|----------------|------------------|
| **API 550**    | `API 550 EQ plugin open source`, `API 550A 550B VST3 GitHub`, `API style EQ JUCE` |
| **SSL E/G**    | `SSL channel strip EQ open source`, `SSL E series EQ plugin GitHub`, `SSL style EQ VST3` |
| **Pultec**     | `Pultec EQ plugin open source`, `Pultec EQP-1A VST GitHub`, `Pultec style EQ JUCE` |
| **Quad Eight / MCI / console** | `Quad Eight EQ open source`, `console EQ plugin GitHub`, `vintage console EQ VST3` |

**Criteria:** MIT / BSD / LGPL preferred (GPL-3.0 possible but may affect distribution); VST3 or JUCE (portable to our build); topology with **shelves and/or bells** (2 shelf + 2 mid ideal, or adaptable); one coherent "character" (no mixing bands from different plugins).

**Shortlist (to update as candidates are found):**

| Flavour | Project | License | URL | Bands (shelf/bell) | Notes | Status |
|---------|---------|---------|-----|--------------------|-------|--------|
| Neve 1073 | 1073-pre-eq-plugin (JackieAlacrity2373) | MIT | https://github.com/JackieAlacrity2373/1073-pre-eq-plugin | Low shelf, 1 mid peak, High shelf | No viable open-source Neve EQ found for strip use. | **Not adopted** |
| API 550   | *(to search)* | — | — | — | Search: API 550A/550B style, stepped or fixed freqs. | **To search** |
| SSL       | **4K-EQ** (Dusk Audio, formerly Luna Co. Software) | GPL-3.0 | https://github.com/dusk-audio/dusk-audio-plugins | 4 parametric + HPF/LPF | SSL 4000 style. Brown (E-series, wide/glue) / Black (G-series, tighter). Saturation (transformers, NE5534). **Used in both Channel and Mix bus modes** (type auto-derived from strip mode). Repo moved from luna-co-software/plugins. | **Adopted — both modes** |
| Pultec    | **EQP-WDF-1A** (ABSounds) | check repo | https://github.com/ABSounds/EQP-WDF-1A | Pultec EQP-1A (boost/cut) | WDF, "Pultec trick". Previously considered for Mix bus; **removed from project** — 4K-EQ used in both modes instead. | **Removed** |
| Other     | **Luftikus** (apresta) | check repo | https://github.com/apresta/luftikus | Fixed bands + shelves | Analog-flavor, simple. Good for air/sparkle. | **Candidate — optional** |

When a new candidate is found: add a row with URL, license, band topology, and set Status accordingly. After listening tests, update Status to "Adopted" / "Rejected (reason)" and document in this file or in DISCUSSION_RESUME.

**EQ algorithm decision (narrowing):**

- **Neve:** No open-source plugin that credibly imitates a Neve EQ was found; 1073-pre-eq-plugin remains unvalidated and is not chosen as primary.
- **Pultec (EQP-WDF-1A):** **Removed from project.** Previously considered for Mix bus only; ruled out as it is a colour/presence box (Pultec trick), not a full channel EQ. 4K-EQ is now used in both modes (Brown/E-series for Mix bus, Black/G-series for Channel).
- **Target:** We want **one analog EQ** for the channel strip: proper bands (shelves + mids), analog character, usable as the main EQ on the bus/channel.
- **Primary candidate:** **4K-EQ** (SSL 4000 style) — analog character, saturation, 4 parametric + HPF/LPF, used in **both** Channel and Mix bus modes. EQ type (Brown/Black) auto-derived from strip mode.

**Quick reference for evaluation (mix bus / strip use):**

| Candidate    | Best for (strip context) | Character | Decision |
|--------------|---------------------------|-----------|----------|
| **4K-EQ**    | Both modes (Channel = Black/G-series; Mix bus = Brown/E-series) | SSL 4000, 4 parametric + HPF/LPF; type auto from strip mode | **Adopted — both modes** |
| **EQP-WDF-1A** | *(removed)* | Pultec EQP-1A, vintage, "Pultec trick" | **Removed** |
| **Luftikus** | Quick tonal shaping, air  | Simple, analog-flavor | Optional |
| **1073-pre-eq-plugin** | Neve-style colour         | 1073, 3 bands | Not adopted (no viable Neve open-source) |

**Decision:** **4K-EQ** is used in **both** modes (4 parametric + HPF/LPF). EQ type is **automatic** (derived from strip mode): **Channel** = **Black** (G-series, tighter Q), **Mix bus** = **Brown** (E-series, wider curves). No user-facing EQ type selector (kEqType parameter removed). LP/HP active in both modes.

### 1.2c.1 Strip modes: Channel vs Mix bus (two buttons)

**Idea:** Two buttons in the UI let the user load one of two **strip personalities**. Each mode selects the **EQ type** (Brown/Black, automatic) and compression personality; see §3.3.1.

**EQ section (this section):**

- **4K-EQ has high-pass and low-pass (HPF/LPF):** Yes — 4K-EQ is 4 parametric + HPF/LPF (SSL 4000 style).
- **Channel mode:** **4K-EQ Black** (G-series, tighter Q, more precise for channel work). All 4K-EQ controls active (bands + HPF/LPF).
- **Mix bus mode:** **4K-EQ Brown** (E-series, wider curves, musical, better for bus glue). Same 4K-EQ controls active. EQ type is automatic — no user-facing selector (kEqType parameter removed).

| Mode        | Role                    | EQ engine (this stage)              | Compression (§3.3.1) | Gain staging (§1.2) |
|-------------|-------------------------|-------------------------------------|----------------------|---------------------|
| **Channel** | Single track / channel  | **4K-EQ Black** (G-series, auto; tighter Q, 4 parametric + HPF/LPF) | **Multi-Comp** (usage modes: Voice, Drums, Classic) | **Yes** (not implemented)  |
| **Mix bus** | Bus / subgroup / master | **4K-EQ Brown** (E-series, auto; wider curves, bus glue) | **Multi-Comp Bus** (SSL G-Series only) | **No** (not present) |

- **LP/HP:** Active in **both modes** (from 4K-EQ HPF/LPF). Same controls in Channel and Mix bus.
- **One click** loads the mode: EQ type and compression switch together (Channel = 4K-EQ Black + Multi-Comp with usage-based mode selector; Mix bus = 4K-EQ Brown + Multi-Comp Bus only, no gain staging). Gain staging is not implemented. User can still tweak parameters after loading.
- **State:** The current mode (Channel / Mix bus) is part of plugin state (getState/setState) so sessions recall the right personality.

**Summary:** Two buttons → two strip modes. **Channel** = 4K-EQ Black/G-series (bande + HP/LP) + **Multi-Comp** (three usage buttons: Voice, Drums, Classic) + **gain staging** (not impl.). **Mix bus** = 4K-EQ Brown/E-series + **Multi-Comp Bus** (SSL only); **no gain staging**. EQ type is automatic (derived from strip mode). See [COMPRESSION_MODES.md](COMPRESSION_MODES.md).

---

### 1.2d Major open-source projects — which hardware-inspired models they offer

Reference for “marchi importanti open-source e quali modelli ispirati offrono” (no endorsement; licensing and suitability must be checked per use).

| Project | License | Hardware-inspired models (summary) |
|--------|---------|------------------------------------|
| **Airwindows** | MIT | **Tone color / console:** **Neverland** (Neve-style colour), **Apicolypse** (API-style), **BussColors4** (Lush=Neve, Rock=SSL, Dark=MCI/Focusrite, Vibe=LA-2A, Holo=Precision8). **Channel4–Channel9**: blend of Neve/API/SSL/Teac/Mackie-style colour (Channel9 adds Teac/Mackie). **Consoles:** **ConsoleLA** (Quad Eight), **ConsoleMC/MD** (MCI), **Console5/6/7/8/9** (various “console” flavours). **TubeDesk** (tube console). **No** dedicated 1073-style *EQ with bands* (Channel9 = colour + drive, not multi-band EQ). |
| **Surge** | GPL-3.0 | Synth + filters (VA, etc.). No dedicated “Neve 1073 EQ” or console strip; filters are general-purpose. |
| **ChowDSP** | GPL-3.0 / BSD | **BYOD** (build-your-own distortion), **CHOW** (distortion). No console/1073-style EQ in the main suite. |
| **Calf Studio** | GPL-2.0 | EQ parametrici, compressori, ecc. Modelli “ispirati” non dichiarati come 1073/Neve/API; EQ generici. |
| **LSP (Linux Studio Plugins)** | GPL-3.0 | Molti filtri ed EQ; alcuni “vintage” o “analog-style”. Non un 1073 dedicato; licenza GPL. |
| **1073-pre-eq-plugin** (JackieAlacrity2373) | MIT | **Neve 1073** preamp/EQ (low shelf, mid peak, high shelf, saturation). VST3 (JUCE). Candidato da valutare in ascolto. |
| **4K-EQ** (Dusk Audio, formerly Luna Co. Software) | GPL-3.0 | **SSL 4000**–style: 4 parametric + HPF/LPF, Brown/Black, saturation. Repo: https://github.com/dusk-audio/dusk-audio-plugins. Vedi shortlist §1.2c. |
| **EQP-WDF-1A** (ABSounds) | check repo | **Pultec EQP-1A** (WDF). Vintage, "Pultec trick". **Removed from project** — 4K-EQ used in both modes. |
| **Luftikus** (apresta) | check repo | Analog-flavor, bande fisse + shelf. Semplice, air/sparkle. Vedi shortlist §1.2c. |

**Takeaway:** **Channel** = 4K-EQ Black/G-series (bande + HP/LP) + Multi-Comp (usage modes) + gain staging (not implemented). **Mix bus** = 4K-EQ Brown/E-series + Multi-Comp Bus only; no gain staging. EQ type automatic (derived from strip mode). §3.3.1.

**Summary:** **Channel** = 4K-EQ Black/G-series + Multi-Comp (usage modes) + gain staging (not implemented). **Mix bus** = 4K-EQ Brown/E-series + Multi-Comp Bus; gain staging not present. EQ type automatic.

### 1.2 Gain staging stage (Channel only; not implemented)

**Gain staging** = controlling signal level so that nothing clips, processors see their ideal input range, and headroom is consistent (often referenced to a nominal level, e.g. −18 dBFS RMS).

- **Planned for Channel mode only; not implemented:** The strip **will include** a gain-staging stage **only when in Channel mode**. In **Mix bus** mode the gain-staging stage is **not present** (no RMS/trim block in the chain). When implemented, it will be bypassable like the others, **in-house** (RMS measurement + gain trim), providing level trim and **RMS-based** reference. **Current status: not implemented.**
- **UI/control:** A **gain selector** (trim) with **RMS** readout: user adjusts gain while viewing RMS (or similar level metering). Exact behaviour (target RMS vs. simple trim with RMS readout) to be defined in implementation.
- **Algorithm source:** **In-house** only. ReaPlugs (Cockos) excluded — proprietary license. Small RMS + gain stage, no external dependency.
- **Placement in UI:** Put the gain-staging controls in a **position that doesn’t get in the way** — e.g. at one end of the strip (start or end of chain) or in a compact row, so the main stages (EQ, Dynamics, Drive, Limiter, Gate) remain the visual focus. Implementation can choose the least intrusive layout (e.g. compact block at top or bottom).

### 1.1 EQ: no per-band Solo/Mute (simplified UI)

We **do not** implement per-band **Solo** or **Mute** on the EQ. The strip stays simple and fast to use: one bypass per stage (EQ stage on/off), and per-band controls only for Freq, Gain, and Q/Bell where applicable. This simplifies the UI and avoids extra parameters.

---

## 2. Gain / Drive stage (core of the system): NAM with GPU extension

The **Gain / Drive** stage is the **core** of The Stripper. It uses **NAM (Neural Amp Modeler)** exclusively, with GPU acceleration planned via **gpuaudio-sdk**. There is no classic fallback (TapeHack2 removed from design). See [DRIVE_STAGE_PARAMS.md](DRIVE_STAGE_PARAMS.md) for full parameter and signal-flow documentation.

### 2.1 Single-knob design

- **One parameter:** `kDriveAmount` (0..1) — how hard the signal is pushed into the NAM model.
- **Always-on coloration:** When the stage is active and a model is loaded, the NAM model is always in the signal path. At Drive = 0, the model provides natural "analog" coloration at unity gain. Increasing Drive adds saturation and harmonics.
- **Auto-compensation:** Output level is automatically matched to input level via smoothed RMS tracking. No manual output trim needed.
- **100% wet:** No dry/wet mix control. The stage is either on (model active, coloring the signal) or bypassed (`kBypassDrive`).

### 2.2 NAM integration

- **Target:** NAM A2 — model format and inference API follow NAM A2 when available.
- **Source of models:** The user loads models from a **local file** (`.nam` or NAM A2–compatible) with the model picker. Downloading from **Tone3000** and online search are **not in version 1** (decision 2026-10-23); models downloaded from the Tone3000 website are loaded by hand.
- **Single NAM stage:** The Gain / Drive stage is the **only** stage that can run a NAM model. Other stages use classic algorithms only.
- **Execution: GPU (with CPU fallback).** NAM in the Drive stage is planned to run on **GPU** via integration with **gpuaudio-sdk** (see §6). Currently runs on CPU via `NeuralAmpModelerCore`. If the GPU is unavailable or unsupported, the plugin **falls back to CPU** so it remains usable everywhere.

### 2.3 Summary

- **Drive stage = NAM only.** No classic/TapeHack2 fallback. If no model is loaded, the stage is a passthrough.
- **Single knob (Drive Amount) + bypass toggle.** Auto-compensated output. Rock & roll, easy to use.

---

## 3. Algorithm sources (Airwindows)

For **non-NAM** stages (EQ, compression, gain when “classic”, limiter, gate), we use algorithms from **Airwindows** (MIT) or other open sources. **Compression** uses **Multi-Comp** (Dusk Audio, GPL-3.0) — Channel: usage modes; Mix bus: Bus only. See §3.3.1, §3.4. Other stages: algorithms and techniques only; no copying of names or branding.

- **Repository:** https://github.com/airwindows/airwindows/tree/master/plugins  
- **Mix and match:** We can take **pieces** from different sources per stage: EQ from **4K-EQ** (both modes; Channel = Black/G-series, Mix bus = Brown/E-series, auto), **compression from Multi-Comp (Channel: usage modes; Mix bus: Bus only)**, **Gain/Drive (classic) from TapeHack2** (Airwindows), limiter **BlockParty** (Airwindows), gate/expander from **Denoise** (Airwindows). When NAM is enabled, Gain/Drive uses **NAM with GPU extension** (Tone3000 or local file, NAM-compatible). **Note:** Multi-Comp and 4K-EQ are GPL-3.0 (Dusk Audio); project is GPL-3.0 (§3.4).  

### 3.0 Criteria for algorithm choice and sources (summary)

We do **not** use only Airwindows. Criteria and sources:

**Criteria**

- **License:** Must be compatible with the project. Compression and EQ use Dusk Audio (GPL-3.0); project license is GPL-3.0 (§3.4).
- **Use case:** Algorithms must suit a **bus-oriented** channel strip (mix bus, subgroup, master), not only single-track (e.g. bus-style compression).
- **Integrability:** C++ (or portable to our C++ VST3), self-contained (no heavy external deps). We port and adapt; **no copying of names or branding** from the source in our UI — only algorithms and techniques.
- **Strategy:** Prefer a single “channel strip” plugin if one exists in a source; otherwise pick **one plugin per stage** and mix-and-match from multiple sources.

**Sources (all allowed)**

| Source | License | Used for |
|--------|---------|----------|
| **Airwindows** | MIT | 4K-EQ (both modes, LP/HP from 4K-EQ), Gain/Drive classic (TapeHack2), Gate (Denoise), **Limiter (BlockParty)** ✓. |
| **Multi-Comp** (Dusk Audio) | GPL-3.0 | Compression **Channel** (usage modes: Voice, Drums, Classic) + **Mix bus** (Bus only). ✓ chosen. |
| **NAM** | — | Gain/Drive when enabled: **NAM with GPU extension** (Tone3000 or local file, NAM-compatible). |
| **In-house** (or open-source) | — | Gain staging (Channel only, optional): RMS + trim. **Not implemented.** ReaPlugs (Cockos) **excluded** — proprietary license. |

So: **4K-EQ** (both modes; Channel = Black/G-series, Mix bus = Brown/E-series, auto), **Multi-Comp** (Channel + Mix bus), **NAM with GPU extension** (Drive) + TapeHack2 (classic), BlockParty, Denoise; **gain staging** (Channel only) planned but **not implemented**. 4K-EQ from Dusk Audio (GPL-3.0).

### 3.1 Approach: channel strip first, then per-stage

**Step 1 – Find Airwindows’ best channel strip (web search)**  
Before scanning the whole repo by category, search the web for:

- **“Airwindows channel strip”** – do they have a single plugin that is already a channel strip (EQ + comp + drive + limiter/gate in one)?
- **“Airwindows best channel strip”**, **“Airwindows recommended channel strip”** – what do users/forums consider their best all-in-one strip?
- If a **single** channel-strip plugin exists (e.g. “Channel4”, “Channel7”, or similar): assess whether we can **integrate that one plugin** into The Stripper (one codebase, one algorithm set, our UI and bypass structure). That is simpler than picking five separate plugins.

**Step 2 – Assess integrability**

- **License:** Airwindows is MIT → we can port and adapt. No name/brand copy; only algorithms.
- **Single plugin vs multiple:**  
  - If one “channel strip” plugin exists and fits our stages (EQ, comp, drive, limiter, gate): port that and map our stages to its internal blocks.  
  - If not: fall back to picking one Airwindows plugin **per stage** (see table below).
- **Code:** Airwindows source is C++. We need to port to our C++ VST3 (no AU/other; our parameter set and bypass per stage). Check that the chosen plugin(s) are self-contained (no heavy external deps).

**Step 3 – Document and implement**

- Once the best channel strip (or per-stage choices) is identified, fill the table below and add a short “Chosen: …” line with the plugin name(s) and link to source file(s).
- Implement: port algorithm(s) into our processor; expose parameters and bypass per stage; no Airwindows naming/branding in our UI.

### 3.2 Per-stage candidates (if no single channel strip)

If there is **no** single Airwindows channel strip suitable for integration, choose one plugin **per stage**:

| Stage            | Airwindows candidate (to choose)     | Notes |
|------------------|--------------------------------------|-------|
| **LP/HP**        | Active in **both modes** (4K-EQ HPF/LPF) | LP/HP from 4K-EQ, active in both Channel and Mix bus. |
| **EQ**           | **4K-EQ** (both modes) ✓ | **Channel:** 4K-EQ Black (G-series, tighter Q, auto). **Mix bus:** 4K-EQ Brown (E-series, wider curves, auto). EQ type derived from strip mode; no user-facing selector (kEqType removed). No per-band Solo/Mute. **Implementation:** 4K-EQ DSP from Dusk Audio (formerly Luna Co. Software), GPL-3.0; see `src/dsp/fourk_eq.*`, `ssl_saturation.h`. Upstream: https://github.com/dusk-audio/dusk-audio-plugins. |
| **Compression**  | **Multi-Comp** (Dusk Audio, GPL-3.0) ✓ chosen | Channel = usage modes (Voice, Drums, Classic). Mix bus = Bus only. Source: `vendors/dusk-audio-plugins` (Multi-Comp). |
| **Gain / Drive** | **NAM with GPU extension** (primary) **or** TapeHack2 (classic) | **Core stage.** NAM: runs on GPU via gpuaudio-sdk extension; models from Tone3000 or local file; NAM-compatible. Classic: Airwindows TapeHack2. Source (TapeHack2): `vendors/airwindows/plugins/`. |
| **Limiter**      | **BlockParty** ✓ chosen              | Airwindows BlockParty — “moderately saturated analog limiter”; fits the suite’s analog, “real” character. Source: `vendors/airwindows/plugins/`. |
| **Gate / Expander** | **Denoise** ✓ chosen               | Airwindows Denoise for the gate/expander stage. Source: `vendors/airwindows/plugins/`. |
| **Gain staging** | In-house | **Channel mode only**; RMS + trim, bypassable. **Not implemented.** Not present in Mix bus. See §1.2. |

**Search queries to run (Step 1):**  
`Airwindows channel strip` · `Airwindows best channel strip` · `Airwindows Channel4` · `Airwindows strip plugin`

### 3.3 Findings from Airwindows repo (submodule `vendors/airwindows`)

Review of `Airwindopedia.txt` and `what.txt` in the cloned repo:

- **No single “channel strip” plugin** in Airwindows. There is no one plugin that does EQ + compression + drive + limiter + gate in one. Integration will be **per-stage** (one Airwindows plugin per stage).

- **CStrip2** – Only plugin with “strip” in the name; Filter category (EQ/filter), not a full strip. Reference only; **EQ stage is inspired by Channel9** (see below).

- **Channel4 … Channel9** – In **Tone Color** category. **Channel9** ✓ chosen as inspiration for our **EQ** stage only.

- **Gain / Drive (classic)** – **TapeHack2** ✓ chosen. Airwindows TapeHack2 provides the classic saturation/drive when NAM is not used. Source: `vendors/airwindows/plugins/`. When NAM is enabled, the stage uses NAM models (Tone3000 or local file, NAM-compatible); see §2.

- **Consoles** (Console4Channel, Console5Channel, …) – Console channel/buss saturation, not a full strip. Could inform gain stage or be an alternative to Channel.

- **Compression** – **Multi-Comp** (Dusk Audio, GPL-3.0) ✓ chosen. Channel: usage modes; Mix bus: Bus only. See §3.3.1, [COMPRESSION_MODES.md](COMPRESSION_MODES.md). Project license: GPL-3.0 (§3.4).

- **Limiter** – **BlockParty** ✓ chosen. Airwindows BlockParty is a “moderately saturated analog limiter”; chosen for its analog imprint and alignment with the suite’s goal of sounding as real and analog as possible.

- **Gate / Expander** – **Denoise** ✓ chosen. Airwindows Denoise provides the algorithm for our **Gate / Expander** stage. Source: `vendors/airwindows/plugins/`. (SoftGate, Gatelope, etc. remain alternative references for gate-only use cases.)

**Result:**  
- *Best channel strip plugin (if any):* None. Use per-stage selection.  
- *Concrete candidates:* EQ → **4K-EQ** (both modes; Channel = Black/G-series, Mix bus = Brown/E-series, auto) ✓; **Compression** → **Multi-Comp** (Channel: usage modes; Mix bus: Bus only) ✓; **Gain/Drive** → **NAM with GPU extension** (primary) + **TapeHack2** (classic); **Gate / Expander** → **Denoise** ✓ chosen; **Limiter** → **BlockParty** ✓ chosen.

### 3.3.1 Compression: Multi-Comp (Dusk Audio) — one engine, usage-based UI, sidechain

**Decision:** The compression stage uses **one** compressor engine: **Multi-Comp** (Dusk Audio, GPL-3.0), from `vendors/dusk-audio-plugins`. What the user sees depends on strip mode:

- **Channel mode:** Three **usage-based** mode buttons — **Voice**, **Drums**, **Classic** — mapping to Multi-Comp’s Opto (LA-2A), FET (1176), and Classic VCA (dbx 160). We show these labels instead of technical names (FET, VCA, Opto). See [COMPRESSION_MODES.md](COMPRESSION_MODES.md).
- **Mix bus mode:** **No** mode selector. The compressor uses **only** the **SSL G-Series Bus** algorithm from Multi-Comp (bus glue).
- **Sidechain:** A second **stereo input bus** (Sidechain) is available. When the host routes a signal to it, all four compressor modes use that signal for **envelope detection** only; gain reduction is applied to the main input (classic key/ducking). See [COMPRESSION_MODES.md](COMPRESSION_MODES.md).

Same Amount and Mix parameters in both modes; in Channel the user also picks the style (Voice / Drums / Classic). One codebase (Multi-Comp DSP), two presentations: channel = choice of character; mix bus = Bus only.

**Source:** Dusk Audio Multi-Comp, GPL-3.0. We use a subset of its modes and expose them with usage-based names. Implementation: port or link the relevant Multi-Comp DSP (Opto, FET, VCA, Studio VCA, Digital, Bus) into The Stripper; Multiband and other modes are not used.

### 3.4 Project license (GPL-3.0 with Dusk Audio)

The **compression** stage uses **Multi-Comp** (Dusk Audio, GPL-3.0). The **EQ** stage uses **4K-EQ** (Dusk Audio, GPL-3.0). Both are **GPL-3.0**, which is *copyleft*: derivative works must be licensed under GPL-3.0 and source must be provided on distribution. The **entire project** is licensed under **GPL-3.0**. See [LICENSE](../../../LICENSE).

- **Repository:** The root of the repo must contain a `LICENSE` file with the full GNU GPL v3 text.
- **Attribution:** Retain Dusk Audio (and other third-party) attribution and license notices as required by GPL-3.0.
- **No AGPL:** We no longer use ZLCompressor (AGPL-3.0); the project is GPL-3.0 only (no network-use clause like AGPL).

---

## 4. SSL 360 Link compatibility (requirement)

**Goal:** The Stripper must work well when hosted in **SSL 360 Link** (the SSL plug-in that hosts third-party VST3 plug-ins and maps their parameters to the UC1, UF8 and UF1 controllers and to the 360 Plug-in Mixer).

### 4.1 What SSL documents (researched 2026-10-10)

Sources: [SSL 360 Link – User Guide](https://support.solidstatelogic.com/hc/en-gb/articles/17183407536285-SSL-360-Link-User-Guide) (including the release notes up to v1.1.7, November 2024) and [SSL 360 Link: How 3rd Party Plug-ins can send Gain Reduction Metering Data to UC1](https://support.solidstatelogic.com/hc/en-gb/articles/26329849372317-SSL-360-Link-How-3rd-Party-Plug-ins-can-send-Gain-Reduction-Metering-Data-to-UC1) (28 May 2025).

- **No SDK, certification or registration.** Hosted plug-ins are found by scanning the user's VST3 folders; mapping of generic VST3 parameters to 360 controls is done entirely on the SSL side, by the user (factory maps exist only for some popular plug-ins). Buttons can be inverted and can cycle through more than two states, so the inverted polarity of our IN buttons (parameter `kBypass*`, lit = bypass 0) is mappable.
- **Format:** only **VST3** is hosted. Link supports up to **stereo** input/output and a mono or stereo sidechain, no MIDI.
- **Gain reduction metering** (UC1 5-segment Comp/Gate meter) works only if the plug-in exposes it, in one of two ways: **read-only VST3 parameters** (`kIsReadOnly`, recommended by SSL, any number of lanes: e.g. one for compression, one for gate), or the PreSonus `IGainReductionInfo` extension (one value per plug-in). Value encoding for parameters: a 0…1 float, **0 = no gain reduction, 1 = full scale reduced to silence**; Link computes `dB = 20 * log10(1 - value)`. The plug-in applies its own ballistics. Meters appear under "ASSIGNABLE METERING DATA" in Link's mapping tab.
- Not documented: whether parameters flagged `kIsHidden` are listed; naming or ordering rules for parameters.

### 4.2 What The Stripper does

- Stereo VST3 only, stable parameter IDs (frozen table), clear parameter names.
- **`Compressor Gain Reduction`, `Limiter Gain Reduction` and `Gate Gain Reduction`** are read-only, **visible** (not hidden) parameters in the encoding above, with the plug-in's own ballistics (instant attack, 60 dB/s release) already applied. The other meter parameters are hidden (the hidden flag is not needed by SSL).
- The gate lane is **estimated** from the gate's input and output peaks (Denoise exposes no read-out) with a fast release (600 dB/s) so the meter shows when and how quickly the gate closes; it is less precise with very low signals. `IGainReductionInfo` is not implemented (read-only parameters cover the same need with more lanes).
- **To verify with the real software:** that Link lists both parameters under assignable meter data and that the dB values look right. No test against SSL 360 Link has been made.

---

## 5. Reference

- **Conceptual / UI inspiration:** Channel-strip style and “load NAM from inside plugin” idea (e.g. Chiarello/PreBrowser). No code or brand copy.
- **Algorithm inspiration:** 4K-EQ (EQ, both modes; type auto from strip mode), Multi-Comp (Channel + Mix bus), Airwindows (TapeHack2, BlockParty, Denoise). NAM with GPU extension for Gain/Drive when enabled (Tone3000 or local, NAM-compatible). See `.cursorrules` and this doc for stage-by-stage choices.

---

## 6. GPU execution and mixed CPU/GPU pipeline

### 6.1 Drive (NAM A2) on GPU

The **Gain / Drive** stage when using NAM A2 runs on **GPU** (integration with **gpuaudio-sdk**). Benefits: lower CPU load, room for larger or more complex models. **Fallback to CPU** when GPU is unavailable or unsupported. Total latency (NAM receptive field + GPU transfer/execution) must be reported via `getLatencySamples()`.

### 6.2 Which other stages can run on GPU?

| Stage | On GPU? | Reason |
|-------|---------|--------|
| **LP/HP (in 4K-EQ)** | Not planned | LP/HP are part of 4K-EQ (both modes); very light. Keep on CPU. |
| **EQ (Channel9-inspired)** | Not planned | Typically biquad/IIR-based; same as above. Convolution-based EQs could benefit from GPU, but our choice is classic EQ on CPU. |
| **Compression (Multi-Comp)** | Theoretically possible | Compression has lookahead and envelope logic; often sequential. GPU helps when work is highly parallel (e.g. NAM); porting a compressor to GPU is possible but not standard. **Keep on CPU** for simplicity and predictability. |
| **Limiter (BlockParty)** | Not planned | Analog-style limiter; keep on CPU for simplicity and low latency. |
| **Gate (Denoise)** | Theoretically possible | Simple gate is cheap on CPU. If the algorithm were FFT-heavy, GPU could help. **Keep on CPU** with current choice. |
| **Gain staging** | Not planned | RMS + trim is a tiny amount of work; transfer to GPU would dominate. CPU only. |

**Summary:** Only the **Drive (NAM)** stage is targeted for GPU. Other stages stay on CPU for simplicity, lower latency, and because their cost is small compared to NAM.

### 6.3 Mixed CPU/GPU pipeline: can it cause problems?

Yes, a pipeline that mixes CPU and GPU stages has to be designed carefully:

- **Latency:** Each time audio crosses the CPU↔GPU boundary, there is transfer latency and (for GPU) execution latency. With **one** GPU stage (Drive), we have: CPU stages → transfer to GPU → NAM → transfer back → CPU stages. That’s one round-trip. Adding more GPU stages would mean more round-trips and higher total latency. Keeping only NAM on GPU **minimizes** these crossings.
- **Buffering and sync:** The host gives us a block of samples; we run CPU stages, then send a block to the GPU, wait for the result, then continue with CPU stages. Buffers and block sizes must be chosen so that GPU work completes in time (double-buffering or queues as per gpuaudio-sdk). The audio thread must not block on GPU calls; it should only submit work and consume results.
- **Real-time safety:** No allocations in the process callback. All GPU buffers and resources allocated at init or when the model is loaded; process only reads/writes pre-allocated buffers and triggers GPU work in a way compatible with the SDK.

**Mitigation:** (1) Only Drive (NAM) on GPU; all other stages on CPU. (2) Batch at the boundaries (process in blocks, not per-sample). (3) Report total latency (NAM + GPU) via `getLatencySamples()` so the host can compensate. (4) CPU fallback when GPU is missing so the plugin still works everywhere.
