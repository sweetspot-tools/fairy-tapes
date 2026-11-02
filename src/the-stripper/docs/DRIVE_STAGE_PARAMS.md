# Drive block — NAM then TAPE

The Drive block holds two engines in series, each with its knob and its on/off switch: **NAM** (Neural Amp Modeler, knob DRIVE) and then **TAPE** (port of Airwindows Tape, MIT, knob TAPE). Both are auto-compensated via RMS tracking, so turning a knob changes timbre without changing perceived loudness. The module IN (`kBypassDrive`) bypasses both. The 4K console saturation (`kEqSaturation`) is **not** part of this block: it is the last step of the EQ (back there since 2026-10-24; from 2026-10-14 to 2026-10-24 it was the Drive's alternative engine).

---

## 0. Position in the chain (follows Strip Mode)

The block moves with Strip Mode, to mirror how real hardware is used (`driveBlockAtStart()` in `params.h`):

| Strip Mode | Chain | Real-world analogue |
|------------|-------|---------------------|
| **Channel** | Input → **[NAM → TAPE]** → Filters + EQ (+ 4K saturation) → Compression → Limiter → Gate → Output | Preamp or capture, then the multitrack machine, then the console channel |
| **Mix bus** | Input → Filters + EQ (+ 4K saturation) → Compression → **[NAM → TAPE]** → Limiter → Gate → Output | Bus processing and outboard, then the 2-track mixdown machine |

In Mix bus the block sits **before** the limiter, so the limiter stays the last stage to touch peaks. The panel box does not move; the ROUTING tab of the back shows the two positions. The DRV clip LED measures the block's output wherever it is.

NAM is a container, not a sound: it can hold a preamp, a pedal, an outboard unit or even a tape capture. TAPE is the built-in tape medium. They can run together (for example a preamp capture then tape in Channel), or either one can be switched off.

---

# Part A — NAM (knob DRIVE)

A single **Drive** knob controls how hard the signal is pushed into the NAM model. The NAM switch (`kDriveNamOn`) skips the engine. There is no classic alternative inside the NAM engine: the built-in saturation is TAPE (Part B), a port of Airwindows **Tape**; the earlier plan of Airwindows TapeHack2 as "NAM off" engine was dropped.

---

## 1. Design principles

- **One knob**: The user only interacts with **Drive Amount** (0–100%). No output trim, no dry/wet. Rock & roll, easy to use.
- **Always-on coloration**: When the stage is active (not bypassed) and a model is loaded, the NAM model is **always in the signal path** — even at Drive = 0%. At zero drive, input gain is unity (1×) and the model provides its natural "analog" coloration. As Drive increases, the signal is pushed harder into the model, adding saturation and harmonics.
- **Bypass = off**: The `kBypassDrive` toggle (module IN) removes the whole block, NAM and TAPE. The NAM switch (`kDriveNamOn`) skips only the NAM. With the NAM on and no model loaded, the NAM passes the signal through unmodified.
- **Auto-compensation**: Output level is matched to input level using a smoothed RMS tracker, so the user hears timbral changes without loudness jumps.

---

## 2. Parameter

| # | Name | VST3 ID | Normalized 0..1 | Meaning |
|---|------|---------|-----------------|---------|
| 1 | **Drive** | `kDriveAmount` | 0 = unity (coloration only), 1 = max (+20 dB into model) | Input gain into the NAM model. Exponential taper: `gain = 10^(norm)` → 1× at 0, ~3.16× at 0.5, 10× at 1.0. |

Plus `kDriveNamOn` (ID 35, NAM switch, default on) and `kBypassDrive` (module IN, bypasses NAM and TAPE).

**Removed parameters:**
- `kDriveOutput` — replaced by automatic RMS compensation.
- `kDriveMix` — always 100% wet when the stage is active.

---

## 3. Auto-compensation (RMS tracking)

Instead of a manual output trim or a "Match level" button, the Drive stage measures input and output RMS per block and applies a correction gain to keep the output at the same perceived level as the input.

### Algorithm

1. **Block RMS** of the mono input **before the drive gain** and of the model output (after sanitization) are computed each process block. Measuring the input before the gain is what keeps the loudness constant across the Drive range (fixed 2026-10-24: before, the reference was taken after the gain and the output stayed up to +20 dB louder at full Drive).
2. Both values are **smoothed** with a one-pole filter (time constant ≈ 100 ms) to avoid pumping.
3. **Compensation gain** = `smoothed_input_rms / smoothed_output_rms`, clamped to -40..+20 dB (linear 0.01 to 10.0): the drive gain alone needs up to -20 dB, and the model can add its own gain on top.
4. If either RMS is below a noise floor (1e-6), compensation is held at unity to avoid amplifying silence.

### Behavior

| Scenario | Drive gain | Model effect | Auto-comp | Net result |
|----------|-----------|-------------|-----------|------------|
| Drive = 0% | 1× (unity) | Natural coloration | ~1× (model changes timbre, not level much) | Subtle analog warmth |
| Drive = 50% | ~3.16× | Moderate saturation + level boost | Corrects level back down | Saturated tone at original loudness |
| Drive = 100% | 10× | Heavy saturation + significant level boost | Corrects level back down | Heavy distortion at original loudness |

---

## 4. Signal flow

```
Stereo in (L, R)
  │
  ├─ Convert to mono: mid = (L + R) / 2
  │
  ├─ Measure input RMS (of mid, before the drive gain)
  │
  ├─ Apply drive gain: mid × 10^(drive_norm)
  │
  ├─ Clamp to safe range (±10.0)
  │
  ├─ NAM model inference (mono in → mono out)
  │
  ├─ Sanitize output (NaN/Inf → 0, clamp ±10.0)
  │
  ├─ Measure output RMS
  │
  ├─ Smooth both RMS values (one-pole, τ ≈ 100ms)
  │
  ├─ Compute compensation = input_rms_smooth / output_rms_smooth
  │
  ├─ Apply: out = model_output × compensation
  │
  └─ Write to both L and R (100% wet, mono output)
```

**Note:** The current implementation outputs a mono signal to both channels: while the NAM is on, the stereo image of the input is lost (relevant in Mix bus, where the block processes the whole mix). TAPE stays stereo. Future versions may process L/R or mid/side separately if stereo preservation becomes a priority.

---

## 5. State version

- **v12** (current): adds `kDriveTapeOn` (36) and `kDriveTapeAmount` (37), `kNumParams` = 38. `kDriveNamOn` now means "NAM on or off" inside the Drive block. States v9…v11 hold 36 parameters (and v11 36 recall marks); they load with TAPE **off**, so a project keeps its sound (apart from the Channel-mode move of the Drive to the start of the chain).
- **v11**: screw angles, recall marks and recall mode (controller state).
- **v10**: adds the wear seed and the writing on the tape pieces after the model path (editor data, in the controller state); v9 loads without them.
- **v9**: adds `kDriveNamOn` (`kNumParams` = 36); v8 loads with the NAM on. Until 2026-10-24 the switch chose the Drive engine: the NAM (`kDriveAmount`) or the 4K saturation (`kEqSaturation`); the NAM without a model passes the signal through.
- **v8**: adds `kLimiterPound`, `kGateThreshold` (`kNumParams` = 35) and the model path (UTF-8 string with length, empty = no model); the model is restored on load. `state.h` reads v1…v8.
- **v7**: `kDriveAmount` is the only Drive parameter. `kNumParams` = 33.
- **v6** (previous): Had `kDriveAmount`, `kDriveOutput`, `kDriveMix` (35 params). Migration: positions 33 and 34 are skipped when loading v6 states.
- **v5** and earlier: No Drive params. `kDriveAmount` defaults to 0.0 on migration.

---

## 6. CPU and GPU inference

`NamDrive` lives in `shared/nam_drive` (NeuralAmpModelerCore v0.6.0) and loads models asynchronously (`requestLoad`); the audio thread swaps the model between blocks. On load it tries the GPU path first: `shared/gpu_nam`, **custom CUDA kernels** for WaveNet models (no gpuaudio-sdk, see `shared/gpu_nam/docs/GPU_NAM_ARCHITECTURE.md`); an unsupported model or a CUDA error falls back to the CPU (NeuralAmpModelerCore). Without the CUDA toolkit at build time, or with `-DGPU_NAM_DISABLE_CUDA=ON`, `gpu_nam` is a CPU-only stub and the NAM always runs on the CPU (the CI workflow does not install the CUDA toolkit). The CUDA build has not been verified on a machine with an NVIDIA driver; the NAM INFERENCE setting (Auto/GPU/CPU) on the back panel is dimmed until then. No latency is reported to the host.

---

# Part B — TAPE (knob TAPE)

Port of **Airwindows Tape** (`vendors/airwindows/plugins/LinuxVST/src/Tape`, MIT; reference submodule, not fetched by a normal clone): simplified, all-purpose tape that Airwindows tuned by stacking eight instances in series, so it holds up both per channel and on the mix bus. Source: `src/dsp/tape_saturation.{h,cpp}`. No Airwindows naming in the UI.

## 7. Parameters

| ID | Name | VST3 ID | Normalized 0..1 | Meaning |
|----|------|---------|-----------------|---------|
| 36 | **Drive Tape** | `kDriveTapeOn` | 1 = on, 0 = skipped | Engine switch (TAPE button left of the Drive IN). Default **on** for new instances, off for states saved before v12. |
| 37 | **Tape** | `kDriveTapeAmount` | 0 = tape colour only, 1 = max | One knob; drives the macro below. Default 0. |

## 8. One-knob macro and voicing

The original plugin has two controls, **Slam** (input gain, -12..+12 dB) and **Bump** (head bump amount). The Tape knob sets both, and the curve depends on Strip Mode:

| Voicing | Strip Mode | Slam | Bump | Character |
|---------|------------|------|------|-----------|
| **Tracking** | Channel | 0 → +12 dB | 0.30 → 0.50 | Multitrack machine hit hard, fatter low end |
| **Mixdown** | Mix bus | 0 → +8 dB | 0.20 → 0.30 | 2-track master machine, gentler slam and bump |

Measured at 48 kHz (40 Hz vs 1 kHz, two tones at -20 dBFS each): low-end lift goes from about +1.8 to +2.8 dB in Tracking and from about +1.2 to +1.7 dB in Mixdown across the knob range. THD on a 1 kHz sine at -3 dBFS goes from about 1.6% at 0 to about 33% (Tracking) or 24% (Mixdown) at full.

## 9. Differences from the original

- **Head bump follows Slam.** In the original, Slam boosts the signal but not the head bump, so the bump shrinks relative to the signal as Slam rises. Here the bump is scaled by the slam gain, so the low-end lift is a property of the machine and grows with the knob.
- **No sin() fold-over above 0 dBFS.** The sin() pre-shaper input is clamped to ±π/2, so hot input (e.g. after +20 dB input gain in Channel mode) saturates instead of folding.
- **Dither removed.** The denormal guard stays: tiny inputs are replaced by xorshift noise around 1e-8, seeded randomly per instance as in the other Airwindows ports. Because the ADClip stage is threshold-based, two instances can differ by up to about -60 dB on hard transients; this is inaudible and matches the original's behaviour.
- **Auto-compensation** (not in the original): stereo-linked RMS of the input (before Slam) and of the output, smoothed with τ ≈ 100 ms; gain = input / output, clamped to -24..+6 dB and ramped across each block. Held when either RMS is below 1e-6. Unlike Drive, the stage stays **stereo**.
