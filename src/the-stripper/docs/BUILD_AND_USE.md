# The Stripper — Build, install, and use (no UI)

How to build the plugin, install it in Reaper, and (later) add the custom UI.

---

## 1. Build (from repo root)

```bash
cd /path/to/fairy-tapes
git submodule update --init --recursive   # first time: fetch VST3 SDK, Airwindows, 4K-EQ (Dusk Audio), Pultec, etc.
bash build.sh the-stripper   # from repo root (fairy-tapes)
```

Or manually:

```bash
mkdir -p build && cd build
cmake ..
cmake --build .   # runs tests first, then builds the_stripper
```

**Output:** `dist/The Stripper.vst3/` (from repo root; folder containing the plugin). The build writes the bundle directly to `dist/`.

---

## 2. Install in Reaper (Linux example)

1. Find your VST3 path. Common locations:
   - Linux: `~/.vst3` or `/usr/lib/vst3`
   - Reaper can also scan a custom folder (Options → Preferences → VST).

2. Copy the **entire** bundle:
   ```bash
   cp -r dist/The\ Stripper.vst3 ~/.vst3/
   ```

3. In Reaper: **Options → Preferences → Plug-ins → VST** → (re)scan or add path if needed.

4. Insert the plugin on a track: **Insert → FX** → search "The Stripper" or "Fairy Tapes".

5. You will see the **generic parameter list** (no custom graphics): Bypass, Strip Mode, Input/Output Gain, bypass toggles per stage, Gain Staging Trim, Compress Amount, Compress Mix. This is normal and works in Reaper.

---

## 3. Current behaviour (no-UI version)

- **Strip Mode:** Channel = 4K-EQ + ButterComp + gain staging; Mix bus = Pultec + Logical4. Right now only **Channel** uses real DSP: **ButterComp** (compression) and **gain staging** (trim). EQ, Pultec, Logical4, Drive, Limiter, Gate are **stubs** (passthrough).
- **Gain staging** is only active in **Channel** mode; in Mix bus that stage is not in the chain.
- **Compression** in Channel mode uses the ported **ButterComp** (Amount + Mix). In Mix bus, compression is still passthrough (Logical4 to be added later).

---

## 4. Adding the custom UI (later)

When we add the strip graphics:

1. **VSTGUI** (or similar) will be added as dependency; the editor will be a separate view created by the controller (`createView`).
2. **Assets** (backgrounds, knobs) go in the plugin bundle or resources; paths are in [UI_ASSETS_SPEC.md](UI_ASSETS_SPEC.md).
3. **Build:** Same `build.sh the-stripper` (from repo root); the UI code will live in the same target (or an optional one).
4. **Reaper:** After copying the new `.vst3` bundle, Reaper will show the custom UI when you open the plugin window.

At the end we can add a short step-by-step (“dove mettere i file”, “come compilare con la UI”, “come testare in Reaper”) in this doc or in the README.
