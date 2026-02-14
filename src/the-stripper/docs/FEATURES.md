# The Stripper — Feature design & requirements

Design of the channel strip, NAM integration, algorithm sources, and external compatibility (SSL 360 Link).

**Quick summary:** See [OVERVIEW.md](OVERVIEW.md) for a one-page recap of all stage choices and where each is documented.

**Orientation:** The Stripper is a **bus-oriented** channel strip (e.g. mix bus, subgroup, master), not aimed at a single track only. Stage choices (e.g. **Multi-Comp** (Channel: usage modes; Mix bus: Bus only), **4K-EQ** (Channel) / **Pultec** (Mix bus, not implemented); **Gain/Drive** = **NAM with GPU extension**) reflect this bus use case.

---

## 1. Channel strip layout

The plugin is a **channel strip** with multiple stages in series. Each stage is **bypassable** (on/off per section). Two **strip modes** — **Channel** and **Mix bus** — select a coherent EQ + compressor personality each (see §1.2c.1).

| Stage            | Role                         | Bypassable |
|------------------|------------------------------|------------|
| **LP/HP**        | **Active in both modes when feasible** (e.g. from 4K-EQ HPF/LPF). Part of EQ stage. | Yes (both modes, if feasible) |
| **EQ**           | 4K-EQ (Channel) / Pultec (Mix bus, **not implemented** — stub) — §1.2c.1 | Yes        |
| **Compression**  | Multi-Comp (Channel: Voice, Drums, Classic; Mix bus: Bus only) | Yes        |
| **Gain / Drive** | **Core stage:** NAM with GPU extension (Tone3000 / local); TapeHack2 when NAM off | Yes        |
| **Limiter**      | BlockParty (Airwindows)       | Yes        |
| **Gate / Expander** | Denoise (Airwindows)       | Yes        |
| **Gain staging** | Optional level/RMS trim      | **Channel mode only**; **not implemented** |

User can run only EQ, only NAM, EQ + NAM, or any combination. No stage is mandatory.

**Order of the chain (signal flow):**  
1. **Gain staging** (optional; **Channel mode only** — see §1.2; placement at start or end)  
2. **EQ** (Channel: 4K-EQ controls active, Pultec disabled; Mix bus: Pultec controls active, 4K-EQ disabled; LP/HP active in both modes when feasible — §1.2c.1)  
3. **Compression** (Multi-Comp in Channel with usage modes, Multi-Comp Bus only in Mix bus — §1.2c.1)  
4. **Gain / Drive** (NAM with GPU extension; TapeHack2 when NAM not used)  
5. **Limiter** (BlockParty)  
6. **Gate / Expander** (Denoise)  
7. **Gain staging** (if placed at end; **Channel mode only** — in Mix bus mode this stage is not present)

So the core flow is: **EQ → Compression → Drive → Limiter → Gate**. Gain staging is **Channel mode only**. **EQ stage:** in **Channel** mode the **4K-EQ** controls are active and **Pultec** controls are disabled; in **Mix bus** mode the **Pultec** controls are active and **4K-EQ** controls are disabled. **LP/HP** remain active in both modes when feasible (e.g. from 4K-EQ HPF/LPF).

**EQ stage: which controls are active per mode.** In **Channel mode**, **4K-EQ** controls are **active** (bands + HPF/LPF) and **Pultec** controls are **disabled**. In **Mix bus mode**, **Pultec** controls are **active** and **4K-EQ** (band) controls are **disabled**. **LP/HP** remain **active in both modes when feasible** (e.g. LP/HP from 4K-EQ HPF/LPF in both modes, or a separate stage if that is feasible). Implementation: Channel = 4K-EQ (bands + HPF/LPF), Pultec params hidden; Mix bus = Pultec (EQP-WDF-1A) **not implemented** (stub), 4K-EQ band params hidden; LP/HP active in both if feasible.

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
- **LP/HP:** HP cutoff and LP cutoff are **stepped** values (e.g. from 4K-EQ: HP 30–200 Hz, LP 4k–20k Hz). **LP/HP remain active in both strip modes when feasible** (Channel: from 4K-EQ; Mix bus: same 4K-EQ HPF/LPF or separate stage if feasible).
- **EQ bands:** Same idea — **stepped** centre frequency per band (and optionally stepped gain steps if we want). No per-band Solo/Mute (UI simplified).

So: **sì, frequenze a scatti sia per passa alto/basso che per le altre bande.**

### 1.2c EQ: analog-style, 2 shelves + 2 mid bands (one character per strip mode)

Channel9 in the source has **Console Type** (Neve / API / SSL / Teac / Mackie) + Drive + Output — it’s a “tone colour” box, not a multi-band EQ. To keep the strip **simple and one-voice** (like the compressor) and to deliver an **analog, harmonic EQ that sounds real**:

- **One EQ + one compressor per strip mode:** **Channel** = **4K-EQ controls active** (4 parametric + HPF/LPF), **Pultec controls disabled** + **Multi-Comp** (usage modes: Voice, Drums, Classic). **Mix bus** = **Pultec controls active**, **4K-EQ controls disabled** + **Multi-Comp Bus** (SSL G-Series only). **LP/HP** stay active in both modes when feasible. The **Channel / Mix bus** button switches which EQ engine’s controls are active (and which are disabled). **Gain staging** is **Channel mode only** and **not implemented**. See §1.2c.1, §3.3.1, [COMPRESSION_MODES.md](COMPRESSION_MODES.md).

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
- **Other flavours:** Search for open-source **API 550**, **SSL**, **Pultec**, **Quad Eight**, or other console/vintage-style EQ (MIT/AGPL, portable to our VST3). Prefer projects with some documentation or reputation; otherwise evaluate by listening before committing.  
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
| SSL       | **4K-EQ** (Dusk Audio, formerly Luna Co. Software) | GPL-3.0 | https://github.com/dusk-audio/dusk-audio-plugins | 4 parametric + HPF/LPF | SSL 4000 style. Brown (wide/glue) / Black (tighter). Saturation (transformers, NE5534). Mix bus / drum bus. **Full channel EQ**, not colour-only. Repo moved from luna-co-software/plugins. | **Primary candidate (analog)** |
| Pultec    | **EQP-WDF-1A** (ABSounds) | check repo | https://github.com/ABSounds/EQP-WDF-1A | Pultec EQP-1A (boost/cut) | WDF, "Pultec trick". **Mix bus** = Pultec (decision made). | **Adopted — Mix bus** |
| Other     | **Luftikus** (apresta) | check repo | https://github.com/apresta/luftikus | Fixed bands + shelves | Analog-flavor, simple. Good for air/sparkle. | **Candidate — optional** |
| Other     | **ZL Equalizer** (ZL-Audio) | check repo | https://github.com/ZL-Audio/ZLEqualizer | 16 bands, various | Linear-phase option, surgical/transparent. Less "character", more precision. | **Alternative (transparent)** |

When a new candidate is found: add a row with URL, license, band topology, and set Status accordingly. After listening tests, update Status to "Adopted" / "Rejected (reason)" and document in this file or in DISCUSSION_RESUME.

**EQ algorithm decision (narrowing):**

- **Neve:** No open-source plugin that credibly imitates a Neve EQ was found; 1073-pre-eq-plugin remains unvalidated and is not chosen as primary.
- **Pultec (EQP-WDF-1A):** Ruled out for the strip: it is a **colour / presence** box (Pultec trick, bass/drums/master), not a **full channel EQ** — "un po' tanto da mix bus" and not suited to "dare una vera EQ al canale".
- **Target:** We want **one analog EQ** for the channel strip: proper bands (shelves + mids), analog character, usable as the main EQ on the bus/channel.
- **Primary candidate:** **4K-EQ** (SSL 4000 style) — analog character, saturation, 4 parametric + HPF/LPF, fits mix bus and channel use. **ZL Equalizer** remains the alternative if we need a more transparent / surgical option (less "character").

**Quick reference for evaluation (mix bus / strip use):**

| Candidate    | Best for (strip context) | Character | Decision |
|--------------|---------------------------|-----------|----------|
| **4K-EQ**    | Channel mode (bande + HP/LP) | SSL 4000, 4 parametric + HPF/LPF | **Adopted — Channel mode** |
| **ZL Equalizer** | Precise corrections, mastering | Transparent, linear-phase; less "character" | **Alternative (transparent)** |
| **EQP-WDF-1A** | Mix bus | Pultec EQP-1A, vintage, "Pultec trick" | **Adopted — Mix bus** |
| **Luftikus** | Quick tonal shaping, air  | Simple, analog-flavor | Optional |
| **1073-pre-eq-plugin** | Neve-style colour         | 1073, 3 bands | Not adopted (no viable Neve open-source) |

**Decision:** **Channel mode** = **4K-EQ** controls active (4 parametric + HPF/LPF), **Pultec** controls disabled. **Mix bus mode** = **Pultec (EQP-WDF-1A)** controls active, **4K-EQ** controls disabled. **LP/HP** remain active in both modes when feasible (e.g. from 4K-EQ). ZL Equalizer remains optional for transparent/surgical use if needed.

### 1.2c.1 Strip modes: Channel vs Mix bus (two buttons)

**Idea:** Two buttons in the UI let the user load one of two **strip personalities**. Each mode selects **which EQ engine** the EQ stage uses; compression is reasoned separately (see §3.3.1).

**EQ section (this section):**

- **4K-EQ has high-pass and low-pass (HPF/LPF):** Yes — 4K-EQ is 4 parametric + HPF/LPF (SSL 4000 style).
- **Channel mode:** **4K-EQ** controls are **active** (bands + HPF/LPF). **Pultec** controls are **disabled**.
- **Mix bus mode:** **Pultec (EQP-WDF-1A)** controls are **active**. **4K-EQ** (band) controls are **disabled**. The EQ stage exposes only the active engine’s controls per mode; LP/HP remain active in both when feasible.

| Mode        | Role                    | EQ engine (this stage)              | Compression (§3.3.1) | Gain staging (§1.2) |
|-------------|-------------------------|-------------------------------------|----------------------|---------------------|
| **Channel** | Single track / channel  | **4K-EQ** (4 parametric + HPF/LPF; LP/HP mapped on 4K-EQ) | **Multi-Comp** (usage modes: Voice, Drums, Classic) | **Yes** (not implemented)  |
| **Mix bus** | Bus / subgroup / master | **Pultec (EQP-WDF-1A)** (not implemented; stub) | **Multi-Comp Bus** (SSL G-Series only) | **No** (not present) |

- **LP/HP:** Remain **active in both modes when feasible** (e.g. from 4K-EQ HPF/LPF in both Channel and Mix bus). In Channel, 4K-EQ controls (including LP/HP) active; in Mix bus, Pultec controls active but LP/HP can stay active if we keep the 4K-EQ HPF/LPF block or a separate LP/HP stage.
- **One click** loads the mode: EQ and compression switch together (Channel = 4K-EQ + Multi-Comp with usage-based mode selector; Mix bus = Pultec + Multi-Comp Bus only, no gain staging). Gain staging and Pultec are not implemented. User can still tweak parameters after loading.
- **State:** The current mode (Channel / Mix bus) is part of plugin state (getState/setState) so sessions recall the right personality.

**Summary:** Two buttons → two strip modes. **Channel** = 4K-EQ (bande + HP/LP) + **Multi-Comp** (three usage buttons: Voice, Drums, Classic) + **gain staging** (not impl.). **Mix bus** = Pultec (not impl.) + **Multi-Comp Bus** (SSL only); **no gain staging**. See [COMPRESSION_MODES.md](COMPRESSION_MODES.md).

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
| **EQP-WDF-1A** (ABSounds) | check repo | **Pultec EQP-1A** (WDF). Vintage, "Pultec trick". Vedi shortlist §1.2c. |
| **Luftikus** (apresta) | check repo | Analog-flavor, bande fisse + shelf. Semplice, air/sparkle. Vedi shortlist §1.2c. |
| **ZL Equalizer** (ZL-Audio) | check repo | 16 bande, linear-phase. Più trasparente/chirurgico. Vedi shortlist §1.2c. |

**Takeaway:** **Channel** = 4K-EQ (bande + HP/LP) + Multi-Comp (usage modes) + gain staging (not implemented). **Mix bus** = Pultec (not implemented) + Multi-Comp Bus only; no gain staging. LP/HP e EQ insieme in Channel. §3.3.1.

**Summary:** **Channel** = 4K-EQ + Multi-Comp (usage modes) + gain staging (not implemented). **Mix bus** = Pultec (not implemented) + Multi-Comp Bus; gain staging not present.

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

The **Gain / Drive** stage is the **core** of The Stripper. The primary design is **NAM (Neural Amp Modeler) with an extension to run on GPU**. When NAM is not used, the stage falls back to classic saturation (TapeHack2).

### 2.1 Mode 1: NAM (primary — with GPU extension)

- **Source of models:** The user can load NAM models either:
  - **From Tone3000** — models linked/connected via the Tone3000 integration (when available in the UI), or
  - **From local file** — user loads a `.nam` (or NAM-compatible) file from disk.
- **Compatibility:** All models must be **compatible with NAM (Neural Amp Modeler)**. The plugin runs them through the NAM engine; no other model format is accepted for this mode.
- **Single NAM stage:** The Gain / Drive stage is the **only** stage that can run a NAM model. Other stages use classic algorithms only.
- **Execution: GPU (with CPU fallback).** NAM in the Drive stage **must run on GPU** via a NAM extension integrated with **gpuaudio-sdk** (see §6). This frees CPU and allows larger or more complex models. If the GPU is unavailable or unsupported, the plugin **falls back to CPU** so it remains usable everywhere.

### 2.2 Mode 2: Classic (when NAM is not used)

- **Algorithm:** When the user does **not** enable NAM, the Gain / Drive stage uses **Airwindows TapeHack2** for saturation/drive.
- **Source:** `vendors/airwindows/plugins/` (or equivalent). MIT license; no NAM dependency.

### 2.3 Summary

- **NAM option:** Enable NAM → use models from **Tone3000** or **local file**; models must be **NAM-compatible**.
- **Classic option:** Do not enable NAM → use **TapeHack2** (Airwindows).
- **Plugin must work without NAM:** If the user never enables NAM or loads a model, the plugin is fully usable with **TapeHack2** only. NAM is optional.

---

## 3. Algorithm sources (Airwindows)

For **non-NAM** stages (EQ, compression, gain when “classic”, limiter, gate), we use algorithms from **Airwindows** (MIT) or other open sources. **Compression** uses **Multi-Comp** (Dusk Audio, GPL-3.0) — Channel: usage modes; Mix bus: Bus only. See §3.3.1, §3.4. Other stages: algorithms and techniques only; no copying of names or branding.

- **Repository:** https://github.com/airwindows/airwindows/tree/master/plugins  
- **Mix and match:** We can take **pieces** from different sources per stage: EQ from **4K-EQ** (Channel) and **Pultec EQP-WDF-1A** (Mix bus, not implemented), **compression from Multi-Comp (Channel: usage modes; Mix bus: Bus only)**, **Gain/Drive (classic) from TapeHack2** (Airwindows), limiter **BlockParty** (Airwindows), gate/expander from **Denoise** (Airwindows). When NAM is enabled, Gain/Drive uses **NAM with GPU extension** (Tone3000 or local file, NAM-compatible). **Note:** Multi-Comp and 4K-EQ are GPL-3.0 (Dusk Audio); project is GPL-3.0 (§3.4).  

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
| **Airwindows** | MIT | EQ per mode via 4K-EQ/Pultec (LP/HP from 4K-EQ in Channel), Gain/Drive classic (TapeHack2), Gate (Denoise), **Limiter (BlockParty)** ✓. |
| **Multi-Comp** (Dusk Audio) | GPL-3.0 | Compression **Channel** (usage modes: Voice, Drums, Classic) + **Mix bus** (Bus only). ✓ chosen. |
| **NAM** | — | Gain/Drive when enabled: **NAM with GPU extension** (Tone3000 or local file, NAM-compatible). |
| **In-house** (or open-source) | — | Gain staging (Channel only, optional): RMS + trim. **Not implemented.** ReaPlugs (Cockos) **excluded** — proprietary license. |

So: **4K-EQ** (Channel) + **Pultec** (Mix bus, not implemented), **Multi-Comp** (Channel + Mix bus), **NAM with GPU extension** (Drive) + TapeHack2 (classic), BlockParty, Denoise; **gain staging** (Channel only) planned but **not implemented**. 4K-EQ and Pultec from external open-source repos.

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
| **LP/HP**        | Active in **both modes when feasible** (e.g. 4K-EQ HPF/LPF) | LP/HP stay on in Channel and Mix bus if feasible; in Channel from 4K-EQ; in Mix bus same or separate stage. |
| **EQ**           | **4K-EQ** (Channel) + **Pultec EQP-WDF-1A** (Mix bus) ✓ | **Channel:** 4K-EQ controls active, Pultec disabled. **Mix bus:** Pultec controls active, 4K-EQ disabled. Mix bus = **Pultec not implemented** (stub). No per-band Solo/Mute. **Implementation:** 4K-EQ DSP from Dusk Audio (formerly Luna Co. Software), GPL-3.0; see `src/dsp/fourk_eq.*`, `ssl_saturation.h`. Upstream: https://github.com/dusk-audio/dusk-audio-plugins. |
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
- *Concrete candidates:* EQ → **4K-EQ** (Channel) + **Pultec EQP-WDF-1A** (Mix bus, not implemented) ✓; **Compression** → **Multi-Comp** (Channel: usage modes; Mix bus: Bus only) ✓; **Gain/Drive** → **NAM with GPU extension** (primary) + **TapeHack2** (classic); **Gate / Expander** → **Denoise** ✓ chosen; **Limiter** → **BlockParty** ✓ chosen.

### 3.3.1 Compression: Multi-Comp (Dusk Audio) — one engine, usage-based UI

**Decision:** The compression stage uses **one** compressor engine: **Multi-Comp** (Dusk Audio, GPL-3.0), from `vendors/dusk-audio-plugins`. What the user sees depends on strip mode:

- **Channel mode:** Three **usage-based** mode buttons — **Voice**, **Drums**, **Classic** — mapping to Multi-Comp’s Opto (LA-2A), FET (1176), and Classic VCA (dbx 160). We show these labels instead of technical names (FET, VCA, Opto). See [COMPRESSION_MODES.md](COMPRESSION_MODES.md).
- **Mix bus mode:** **No** mode selector. The compressor uses **only** the **SSL G-Series Bus** algorithm from Multi-Comp (bus glue).

Same Amount and Mix parameters in both modes; in Channel the user also picks the style (Voice / Drums / Classic). One codebase (Multi-Comp DSP), two presentations: channel = choice of character; mix bus = Bus only.

**Source:** Dusk Audio Multi-Comp, GPL-3.0. We use a subset of its modes and expose them with usage-based names. Implementation: port or link the relevant Multi-Comp DSP (Opto, FET, VCA, Studio VCA, Digital, Bus) into The Stripper; Multiband and other modes are not used.

### 3.4 Project license (GPL-3.0 with Dusk Audio)

The **compression** stage uses **Multi-Comp** (Dusk Audio, GPL-3.0). The **EQ** stage uses **4K-EQ** (Dusk Audio, GPL-3.0). Both are **GPL-3.0**, which is *copyleft*: derivative works must be licensed under GPL-3.0 and source must be provided on distribution. The **entire project** is licensed under **GPL-3.0**. See [LICENSE](../../../LICENSE).

- **Repository:** The root of the repo must contain a `LICENSE` file with the full GNU GPL v3 text.
- **Attribution:** Retain Dusk Audio (and other third-party) attribution and license notices as required by GPL-3.0.
- **No AGPL:** We no longer use ZLCompressor (AGPL-3.0); the project is GPL-3.0 only (no network-use clause like AGPL).

---

## 4. SSL 360 Link compatibility (requirement)

**Goal:** The Stripper must be **100% compatible** with mapping in **SSL 360 Link** (application that maps external plugins to SSL mixers). Some plugins map easily, others do not; we target full compatibility.

**What we need to establish:**

1. **How SSL 360 Link discovers and maps plugins**
   - Does it scan standard VST3 paths only?
   - Any extra registration or manifest (e.g. specific XML, plugin list file)?
   - Any SSL SDK or “certified” plugin program?

2. **What makes a plugin “mappable”**
   - VST3 parameter layout (e.g. parameter count, naming, order)?
   - Specific parameter IDs or host automation conventions?
   - Required VST3 interfaces or behaviors (e.g. IEditController, IComponentHandler)?

3. **Where to find the rules**
   - SSL developer portal or support (“SSL 360 Link plugin compatibility”, “SSL 360 Link developer”).
   - Official list of “supported” or “tested” plugins and any published requirements.
   - SSL 360 Link manual or FAQ for “adding” or “mapping” third-party plugins.

**Action:**

- Search for: **“SSL 360 Link” + “plugin compatibility” / “developer” / “SDK” / “supported plugins”**.
- If SSL provides a checklist or SDK, implement every required item (parameters, IDs, behavior).
- Once we have concrete requirements, add a subsection here (e.g. “4.1 SSL 360 Link requirements”) and track implementation in the codebase.

Until we have official or reliable sources, assume: **strict VST3 compliance** (correct parameter info, stable IDs, no optional behavior that could break host automation) and **clear, stable parameter names** to improve mapping in any host or controller app.

---

## 5. Reference

- **Conceptual / UI inspiration:** Channel-strip style and “load NAM from inside plugin” idea (e.g. Chiarello/PreBrowser). No code or brand copy.
- **Algorithm inspiration:** 4K-EQ + Pultec (EQ per mode; Pultec not implemented), Multi-Comp (Channel + Mix bus), Airwindows (TapeHack2, BlockParty, Denoise). NAM with GPU extension for Gain/Drive when enabled (Tone3000 or local, NAM-compatible). See `.cursorrules` and this doc for stage-by-stage choices.

---

## 6. GPU execution and mixed CPU/GPU pipeline

### 6.1 Drive (NAM) on GPU

The **Gain / Drive** stage when using NAM runs on **GPU** (modified NAM path with **gpuaudio-sdk**). Benefits: lower CPU load, room for larger or more complex models. **Fallback to CPU** when GPU is unavailable or unsupported. Total latency (NAM receptive field + GPU transfer/execution) must be reported via `getLatencySamples()`.

### 6.2 Which other stages can run on GPU?

| Stage | On GPU? | Reason |
|-------|---------|--------|
| **LP/HP (in 4K-EQ)** | Not planned | LP/HP are part of 4K-EQ in Channel; very light. Keep on CPU. |
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
