# The Stripper — Project overview

One-page summary of the plugin and **where each decision is documented**. Current state (2026-11-02): version 1 is complete in code; host tests (Reaper, Cubase), Windows/macOS tests and the CUDA build are still to do (see the [plan](../../../docs/superpowers/plans/2026-10-09-the-stripper-completion.md)). Decisions taken after the first design live in the [spec](../../../docs/superpowers/specs/2026-10-09-the-stripper-completion-design.md) (§11, including the open ones).

---

## What it is

- **The Stripper** = VST3 **bus-oriented** channel strip (single tracks and buses: subgroup, stems, mix bus).
- **Two strip modes**, one switch (**CHAN / BUS**, parameter `kStripMode`). **Channel** = 4K-EQ Black (G series) + Multi-Comp with three characters (Voice / Drums / Classic) + Drive block right after the input. **Mix bus** = 4K-EQ Brown (E series) + Multi-Comp Bus + Drive block before the limiter. The EQ type is derived from the strip mode (no selector). See [FEATURES §1.2c.1](FEATURES.md#12c1-strip-modes-channel-vs-mix-bus-two-buttons), §3.3.1.
- All stages are **bypassable** (one IN button per module). Any combination works (e.g. only EQ, only NAM, EQ + NAM).
- **Order of the chain (engine, `processor.cpp`):** Input → **[Drive, Channel]** → Filters + EQ (with the 4K saturation as last step) → Compression → **[Drive, Mix bus]** → Limiter → Gate → Output. The filters (HPF/LPF) are part of the 4K-EQ engine: HPF before the bands, LPF after them; they follow only their own IN (with the EQ IN off they still run, filters only; 2026-11-02). See [FEATURES §1](FEATURES.md#1-channel-strip-layout) and [DRIVE_STAGE_PARAMS.md §0](DRIVE_STAGE_PARAMS.md).
- **We can take pieces and simplify:** we are not bound to 1:1 copies of source plugins; we expose fewer, more musical parameters for a "rock n roll", easy-to-use strip. See [FEATURES §1.2a](FEATURES.md#12a-taking-pieces-and-simplifying-rock-n-roll-easy-to-use).
- **License:** **GPL-3.0** (Multi-Comp and 4K-EQ from Dusk Audio are GPL-3.0). See [FEATURES §3.3.1](FEATURES.md#331-compression-multi-comp-dusk-audio--one-engine-usage-based-ui-sidechain), [§3.4](FEATURES.md#34-project-license-gpl-30-with-dusk-audio), [THIRD_PARTY.md](../../../THIRD_PARTY.md).

---

## Stage decisions (all linked to FEATURES.md)

| Stage | Choice (in the code) | Status | Where it's written |
|-------|----------------------|--------|--------------------|
| **LP/HP** | HPF/LPF of the 4K-EQ, both modes; one IN for both (`kBypassLPHP`) | ✓ done | [FEATURES §1](FEATURES.md#1-channel-strip-layout), §3.2 |
| **EQ** | 4K-EQ (Dusk Audio) in both modes. **Channel** = Black (G series). **Mix bus** = Brown (E series). Last step: the 4K console saturation (SATURATE, `kEqSaturation`) | ✓ done | [FEATURES §1.2c](FEATURES.md#12c-eq-analog-style-2-shelves--2-mid-bands-one-character-per-strip-mode), §1.2c.1, §1.1, §3.2 |
| **Compression** | Multi-Comp (Dusk Audio): **Channel** = Opto / FET / VCA (Voice / Drums / Classic); **Mix bus** = Bus. External stereo sidechain | ✓ done | [FEATURES §3.2](FEATURES.md#32-per-stage-candidates-if-no-single-channel-strip), §3.3.1, [COMPRESSION_MODES.md](COMPRESSION_MODES.md) |
| **Gain / Drive** | **Core stage.** Two engines in series, each with its switch: **NAM** (NeuralAmpModelerCore via `shared/nam_drive`; optional CUDA path in `shared/gpu_nam`) then **TAPE** (port of Airwindows Tape). Position follows Strip Mode | ✓ done (CUDA not verified) | [FEATURES §2](FEATURES.md#2-gain--drive-stage-core-of-the-system-nam-with-gpu-extension), [DRIVE_STAGE_PARAMS.md](DRIVE_STAGE_PARAMS.md) |
| **Limiter** | BlockParty (Airwindows), knob POUND | ✓ done | [FEATURES §3.2](FEATURES.md#32-per-stage-candidates-if-no-single-channel-strip), §3.3 |
| **Gate / Expander** | DeNoise (Airwindows), knob THRESH | ✓ done | [FEATURES §3.2](FEATURES.md#32-per-stage-candidates-if-no-single-channel-strip), §3.3 |
| **Gain staging** | In-house (RMS + trim), Channel only | **Out of scope for version 1**; its two parameters stay, inert | [FEATURES §1.2](FEATURES.md#12-gain-staging-stage-channel-only-not-implemented) |

---

## Gain / Drive (core) — quick ref

- **NAM** (switch NAM, knob DRIVE): `.nam` files loaded by hand from local folders with the model browser (click on the paper strip). No online catalogue: the Tone3000 integration is **not in version 1** (removed 2026-10-23/24, spec §10). No model = passthrough. Dual mono (one model instance per channel; a mono source runs one instance, since 2026-11-02). Runs on the CPU; the optional GPU path uses **custom CUDA kernels** in `shared/gpu_nam` (WaveNet only; no gpuaudio-sdk), built only with the CUDA toolkit, not verified yet. → [FEATURES §2](FEATURES.md#2-gain--drive-stage-core-of-the-system-nam-with-gpu-extension), [§6](FEATURES.md#6-gpu-execution-and-mixed-cpugpu-pipeline)
- **TAPE** (switch TAPE, knob TAPE): port of **Airwindows Tape** (`src/dsp/tape_saturation.*`), one knob over slam and head bump; voicing **Tracking** in Channel, **Mixdown** in Mix bus; stereo, level-matched. TapeHack2 (the earlier candidate) is not used. → [DRIVE_STAGE_PARAMS.md](DRIVE_STAGE_PARAMS.md) Part B
- **Position:** Channel = right after the Input; Mix bus = between Compression and Limiter.

---

## Still to decide

- Open decisions are in the spec, §11: oversampling of the non-linear stages, guided release setting for the compressor, tuning of the TAPE macro by ear, and others.
- **Gain staging:** out of scope for version 1 (RMS window, target-RMS vs trim-only and UI would be decided if it comes back).

---

## Where things live

| Topic | Document |
|-------|----------|
| User-facing summary, build, licences | [README.md](../README.md) (the-stripper) |
| Full requirements, research, algorithms, SSL 360 Link | [FEATURES.md](FEATURES.md) |
| This overview | [OVERVIEW.md](OVERVIEW.md) |
| Every parameter (IDs, ranges, defaults) | [CONTROLS.md](CONTROLS.md), `src/params.h` |
| Compressor modes and sidechain | [COMPRESSION_MODES.md](COMPRESSION_MODES.md) |
| Drive block (NAM, TAPE, state versions) | [DRIVE_STAGE_PARAMS.md](DRIVE_STAGE_PARAMS.md) |
| Build, install, developer tools | [BUILD_AND_USE.md](BUILD_AND_USE.md) |
| Editor design (layout, controls, back panel) | spec §4 and §11, preview [mockups/stripper_rack.html](mockups/stripper_rack.html), [DESIGN_SYSTEM.md](../../../docs/DESIGN_SYSTEM.md) |
| Signal-flow diagram | [images/routing.png](images/routing.png) (ROUTING tab of the back panel) |
| Third-party code | [THIRD_PARTY.md](../../../THIRD_PARTY.md) |
| Project license (GPL-3.0) | [LICENSE](../../../LICENSE) |
| Suite README | [README.md](../../../README.md) (root) |

---

## Checklist "tutto collegato"

- [x] Bus-oriented → FEATURES intro + this overview.
- [x] Every stage choice → FEATURES §3.2 table + §3.3 Result.
- [x] Gain/Drive = core, NAM then TAPE (Airwindows Tape), position from Strip Mode → FEATURES §2, DRIVE_STAGE_PARAMS.md, this overview.
- [x] Compression = Multi-Comp (Dusk Audio): Channel usage modes + Bus in Mix bus. License GPL-3.0 (Multi-Comp + 4K-EQ, both Dusk Audio). FEATURES §3.4, LICENSE.
- [x] EQ without per-band solo/mute → FEATURES §1.1.
- [x] Limiter → **BlockParty** (Airwindows); Gate → **DeNoise** (Airwindows).
- [x] Gain staging → out of scope for version 1 (FEATURES §1.2).
- [x] Tone3000 → not in version 1 (spec §10).
