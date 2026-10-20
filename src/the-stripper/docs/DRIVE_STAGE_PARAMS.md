# Drive stage — single-knob NAM with auto-compensation

The Drive stage uses **NAM (Neural Amp Modeler)** exclusively — no classic fallback (TapeHack2 removed from design). A single **Drive** knob controls the amount of saturation; output level is **auto-compensated** via RMS tracking so that increasing drive changes timbre without changing perceived loudness.

---

## 1. Design principles

- **One knob**: The user only interacts with **Drive Amount** (0–100%). No output trim, no dry/wet. Rock & roll, easy to use.
- **Always-on coloration**: When the stage is active (not bypassed) and a model is loaded, the NAM model is **always in the signal path** — even at Drive = 0%. At zero drive, input gain is unity (1×) and the model provides its natural "analog" coloration. As Drive increases, the signal is pushed harder into the model, adding saturation and harmonics.
- **Bypass = off**: The `kBypassDrive` toggle removes the stage entirely (true bypass for CPU savings or clean signal). When not bypassed and no model is loaded, the signal passes through unmodified.
- **Auto-compensation**: Output level is matched to input level using a smoothed RMS tracker, so the user hears timbral changes without loudness jumps.

---

## 2. Parameter

| # | Name | VST3 ID | Normalized 0..1 | Meaning |
|---|------|---------|-----------------|---------|
| 1 | **Drive** | `kDriveAmount` | 0 = unity (coloration only), 1 = max (+20 dB into model) | Input gain into the NAM model. Exponential taper: `gain = 10^(norm)` → 1× at 0, ~3.16× at 0.5, 10× at 1.0. |

Plus the existing `kBypassDrive` (stage on/off toggle).

**Removed parameters:**
- `kDriveOutput` — replaced by automatic RMS compensation.
- `kDriveMix` — always 100% wet when the stage is active.

---

## 3. Auto-compensation (RMS tracking)

Instead of a manual output trim or a "Match level" button, the Drive stage measures input and output RMS per block and applies a correction gain to keep the output at the same perceived level as the input.

### Algorithm

1. **Block RMS** of the mono input (after drive gain, before model) and model output (after sanitization) are computed each process block.
2. Both values are **smoothed** with a one-pole filter (time constant ≈ 100 ms) to avoid pumping.
3. **Compensation gain** = `smoothed_input_rms / smoothed_output_rms`, clamped to ±20 dB (linear 0.1 to 10.0).
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
  ├─ Apply drive gain: mid × 10^(drive_norm)
  │
  ├─ Clamp to safe range (±10.0)
  │
  ├─ Measure input RMS
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

**Note:** The current implementation outputs a mono signal to both channels. Stereo imaging from upstream stages is preserved because the Drive stage is typically used for tonal coloring, not spatial processing. Future versions may implement mid/side processing if stereo preservation becomes a priority.

---

## 5. State version

- **v10** (current): adds the wear seed and the writing on the tape pieces after the model path (editor data, in the controller state); v9 loads without them.
- **v9**: adds `kDriveNamOn` (`kNumParams` = 36); v8 loads with the NAM on. The Drive is either the NAM (`kDriveAmount`) or the 4K saturation moved here from the EQ (`kEqSaturation`), chosen by this switch; the NAM without a model passes the signal through.
- **v8**: adds `kLimiterPound`, `kGateThreshold` (`kNumParams` = 35) and the model path (UTF-8 string with length, empty = no model); the model is restored on load. `state.h` reads v1…v8.
- **v7**: `kDriveAmount` is the only Drive parameter. `kNumParams` = 33.
- **v6** (previous): Had `kDriveAmount`, `kDriveOutput`, `kDriveMix` (35 params). Migration: positions 33 and 34 are skipped when loading v6 states.
- **v5** and earlier: No Drive params. `kDriveAmount` defaults to 0.0 on migration.

---

## 6. GPU acceleration (planned)

`NamDrive` lives in `shared/nam_drive` (NeuralAmpModelerCore v0.6.0) and loads models asynchronously (`requestLoad`); the audio thread swaps the model between blocks. The CUDA path of `gpu_nam` falls back to a CPU stub; it has not been verified on a machine with an NVIDIA driver.
