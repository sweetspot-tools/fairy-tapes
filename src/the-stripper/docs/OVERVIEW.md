# The Stripper — Project overview

One-page summary of the plugin and **where each decision is documented**. Use this to check that everything is connected.

---

## What it is

- **The Stripper** = VST3 **bus-oriented** channel strip (mix bus, subgroup, master), not single-track only.
- **Two strip modes:** **Channel** and **Mix bus** — one button each. **EQ stage:** 4K-EQ in both modes. **Channel** = Black (G-series, tight). **Mix bus** = Brown (E-series, wide/glue). EQ Type derived automatically from Strip Mode. **LP/HP** stay active in both modes when feasible. **Channel** = 4K-EQ + Multi-Comp (Dusk Audio) + gain staging (not implemented). **Mix bus** = Multi-Comp Bus (Dusk Audio). See [FEATURES §1.2c.1](FEATURES.md#12c1-strip-modes-channel-vs-mix-bus-two-buttons), §3.3.1.
- All stages are **bypassable**. User can run any combination (e.g. only EQ, only NAM, EQ + NAM).
- **Order of the chain:** LP/HP → EQ → Compression → Drive → Limiter → Gate. Gain staging **Channel mode only**, at one end (unobtrusive). See [FEATURES §1](FEATURES.md#1-channel-strip-layout).
- **We can take pieces and simplify:** We are not bound to 1:1 copies of source plugins; we can expose fewer, more musical parameters for a “rock n roll”, easy-to-use strip. See [FEATURES §1.2a](FEATURES.md#12a-taking-pieces-and-simplifying-rock-n-roll-easy-to-use).
- **License:** **GPL-3.0** (Multi-Comp + 4K-EQ, both Dusk Audio). See [FEATURES §3.3.1](FEATURES.md#331-compression), [§3.4](FEATURES.md#34-project-license-gpl-30).

---

## Stage decisions (all linked to FEATURES.md)

| Stage | Choice | Status | Where it’s written |
|-------|--------|--------|--------------------|
| **LP/HP** | Mapped on 4K-EQ (Channel only) | ✓ | [FEATURES §1](FEATURES.md#1-channel-strip-layout), §3.2 |
| **EQ** | 4K-EQ in both modes. **Channel** = Black (G-series, tight). **Mix bus** = Brown (E-series, wide/glue). EQ Type derived automatically from Strip Mode. LP/HP active in both. | ✓ | [FEATURES §1.2c](FEATURES.md#12c-eq-analog-style-2-shelves--2-mid-bands-one-character-per-strip-mode), §1.2c.1, §1.1, §3.2, §3.3 |
| **Compression** | **Channel** = Multi-Comp (Dusk Audio); **Mix bus** = Multi-Comp Bus (Dusk Audio). | ✓ chosen | [FEATURES §3.2](FEATURES.md#32-per-stage-candidates-if-no-single-channel-strip), §3.3, §3.3.1, §3.4 |
| **Gain / Drive** | **Core stage.** NAM with GPU extension (primary); TapeHack2 when NAM off | ✓ chosen | [FEATURES §2](FEATURES.md#2-gain--drive-stage-core-of-the-system-nam-with-gpu-extension), §3.2, §3.3 |
| **Limiter** | BlockParty (Airwindows) | ✓ chosen | [FEATURES §3.2](FEATURES.md#32-per-stage-candidates-if-no-single-channel-strip), §3.3 — analog character |
| **Gate / Expander** | Denoise (Airwindows) | ✓ chosen | [FEATURES §3.2](FEATURES.md#32-per-stage-candidates-if-no-single-channel-strip), §3.3 |
| **Gain staging** | In-house (RMS + trim) | **Channel mode only**; **not implemented**. Not present in Mix bus. | [FEATURES §1.2](FEATURES.md#12-gain-staging-stage-channel-only-not-implemented) |

---

## Gain / Drive (core) — quick ref

- **NAM on:** models from **Tone3000** or **local file**; must be **NAM-compatible**. NAM **must run on GPU** (gpuaudio-sdk extension), with CPU fallback. → [FEATURES §2.1](FEATURES.md#21-mode-1-nam-primary--with-gpu-extension), [§6](FEATURES.md#6-gpu-execution-and-mixed-cpugpu-pipeline)
- **NAM off:** **Airwindows TapeHack2** (CPU). → [FEATURES §2.2](FEATURES.md#22-mode-2-classic-when-nam-is-not-used)

---

## Still to decide

- **Gain staging** — implementation details (exact RMS window, target-RMS vs trim-only, UI layout). Placement: unobtrusive (e.g. end of strip or compact block).

---

## Where things live

| Topic | Document |
|-------|----------|
| Full requirements, NAM, algorithms, SSL 360 Link | [FEATURES.md](FEATURES.md) |
| This overview | [OVERVIEW.md](OVERVIEW.md) |
| **Parameters per stage** (source list + proposed UI) | [UI_PARAMS.md](UI_PARAMS.md) |
| UI / assets | [UI_ASSETS_SPEC.md](UI_ASSETS_SPEC.md) |
| Project license (GPL-3.0) | [LICENSE](../../../LICENSE) |
| Suite README | [README.md](../../../README.md) (root) |
| Plugin build & status | [README.md](../README.md) (the-stripper) |

---

## Checklist “tutto collegato”

- [x] Bus-oriented → FEATURES intro + this overview.
- [x] Every stage choice → FEATURES §3.2 table + §3.3 Result.
- [x] Gain = core, NAM vs TapeHack2 → FEATURES §2 + overview table.
- [x] Compression = Multi-Comp (Dusk Audio, Channel) + Multi-Comp Bus (Dusk Audio, Mix bus). License GPL-3.0 (Multi-Comp + 4K-EQ, both Dusk Audio). FEATURES §3.4, LICENSE.
- [x] EQ solo/mute → FEATURES §1.1.
- [x] Limiter → **BlockParty** (Airwindows) chosen; analog character (FEATURES §3.2, §3.3).
- [x] Gain staging source → in-house (RMS + trim); ReaPlugs excluded (FEATURES §1.2).
