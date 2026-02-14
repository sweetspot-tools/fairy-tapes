# The Stripper — Project overview

One-page summary of the plugin and **where each decision is documented**. Use this to check that everything is connected.

---

## What it is

- **The Stripper** = VST3 **bus-oriented** channel strip (mix bus, subgroup, master), not single-track only.
- **Two strip modes:** **Channel** and **Mix bus** — one button each. **EQ stage:** **Channel** = 4K-EQ controls **active**, Pultec **disabled**; **Mix bus** = Pultec controls **active**, 4K-EQ **disabled**. **LP/HP** stay active in both modes when feasible. **Channel** = 4K-EQ + ZLCompressor + gain staging (not implemented). **Mix bus** = Pultec (not implemented) + Logical4 (SSL bus compressor clone). See [FEATURES §1.2c.1](FEATURES.md#12c1-strip-modes-channel-vs-mix-bus-two-buttons), §3.3.1.
- All stages are **bypassable**. User can run any combination (e.g. only EQ, only NAM, EQ + NAM).
- **Order of the chain:** LP/HP → EQ → Compression → Drive → Limiter → Gate. Gain staging **Channel mode only**, at one end (unobtrusive). See [FEATURES §1](FEATURES.md#1-channel-strip-layout).
- **We can take pieces and simplify:** We are not bound to 1:1 copies of source plugins; we can expose fewer, more musical parameters for a “rock n roll”, easy-to-use strip. See [FEATURES §1.2a](FEATURES.md#12a-taking-pieces-and-simplifying-rock-n-roll-easy-to-use).
- **License:** **AGPL-3.0** (ZLCompressor in Channel). Mix bus compression = Logical4 (MIT). See [FEATURES §3.3.1](FEATURES.md#331-compression-zlcompressor-channel--logical4-mix-bus), [§3.4](FEATURES.md#34-project-license-agpl-30-with-zlcompressor).

---

## Stage decisions (all linked to FEATURES.md)

| Stage | Choice | Status | Where it’s written |
|-------|--------|--------|--------------------|
| **LP/HP** | Mapped on 4K-EQ (Channel only) | ✓ | [FEATURES §1](FEATURES.md#1-channel-strip-layout), §3.2 |
| **EQ** | **Channel:** 4K-EQ controls active, Pultec disabled. **Mix bus:** Pultec controls active, 4K-EQ disabled. LP/HP active in both when feasible. Pultec not implemented. | 4K-EQ ✓; Pultec stub | [FEATURES §1.2c](FEATURES.md#12c-eq-analog-style-2-shelves--2-mid-bands-one-character-per-strip-mode), §1.2c.1, §1.1, §3.2, §3.3 |
| **Compression** | **Channel** = ZLCompressor; **Mix bus** = Logical4 (SSL bus compressor clone). | ✓ chosen | [FEATURES §3.2](FEATURES.md#32-per-stage-candidates-if-no-single-channel-strip), §3.3, §3.3.1, §3.4 |
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
| Project license (AGPL-3.0) | [LICENSE](../../../LICENSE) |
| Suite README | [README.md](../../../README.md) (root) |
| Plugin build & status | [README.md](../README.md) (the-stripper) |

---

## Checklist “tutto collegato”

- [x] Bus-oriented → FEATURES intro + this overview.
- [x] Every stage choice → FEATURES §3.2 table + §3.3 Result.
- [x] Gain = core, NAM vs TapeHack2 → FEATURES §2 + overview table.
- [x] Compression = ZLCompressor (Channel) + Logical4 (Mix bus, SSL bus clone). License AGPL-3.0 (ZLCompressor). FEATURES §3.4, LICENSE.
- [x] EQ solo/mute → FEATURES §1.1.
- [x] Limiter → **BlockParty** (Airwindows) chosen; analog character (FEATURES §3.2, §3.3).
- [x] Gain staging source → in-house (RMS + trim); ReaPlugs excluded (FEATURES §1.2).
