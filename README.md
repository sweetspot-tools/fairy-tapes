# Fairy Tapes

Suite of VST3 audio plugins: real, analog-style tone via NAM (Neural Amp Modeler) and GPU where applicable.

**License:** This project is licensed under the **GNU General Public License v3.0 (GPL-3.0)**. Full text: [LICENSE](LICENSE). Components: **4K-EQ** and **Multi-Comp** (Dusk Audio, GPL-3.0), **Airwindows** (MIT), VST3 SDK (Steinberg). See [src/the-stripper/docs/FEATURES.md](src/the-stripper/docs/FEATURES.md) §3.3.1 and §3.4.

- **Suite**: Fairy Tapes, published by **Sweetspot Tools** ([github.com/sweetspot-tools](https://github.com/sweetspot-tools)): VST3 vendor "Sweetspot Tools", macOS bundle identifiers `com.sweetspot-tools.fairytapes.<plugin>`  
- **First plugin**: [The Stripper](src/the-stripper/) — **bus-oriented** channel strip (LP/HP, EQ, compression, Gain/Drive with NAM or TapeHack2, limiter, gate/expander). See [src/the-stripper/docs/OVERVIEW.md](src/the-stripper/docs/OVERVIEW.md) and [FEATURES.md](src/the-stripper/docs/FEATURES.md) for design and stage choices.
- **Second plugin (designed)**: [Doggystyle](src/doggystyle/) — aggressive parallel compression for drums / heavy rock–metal: a fast FET-style "Slap" branch and a heavy VCA "Thump" branch, blend, dry mix and an optional NAM stage. See the [design](docs/superpowers/specs/2026-10-09-doggystyle-design.md) and [plan](docs/superpowers/plans/2026-10-09-doggystyle.md) (not implemented yet).
- **Third plugin**: [Pillow Talk](src/pillow-talk/) — tape echo that ducks its own return while the voice sings and opens in the pauses, so the tails land after each phrase without automation. Works as an insert or on a return bus with sidechain. Has a code-drawn VSTGUI rack editor (1000 x 628) with a live ducking display, LEVEL meters and clip LEDs, wear, handwritten tape, rotatable screws and a back panel with Settings and Credits. See [src/pillow-talk/docs/CONTROLS.md](src/pillow-talk/docs/CONTROLS.md) and the [design](docs/superpowers/specs/2026-10-08-pillow-talk-design.md).
- **Fourth plugin**: [Hotline](src/hotline/) — device effect for voice: megaphone, telephone, walkie-talkie, AM radio, station PA and cheap speaker from one chain (band, drive, cone, crush, compander, noise, room) with a single automatable Morph from clean to device and zero latency. Code-drawn VSTGUI rack editor (1000 x 470) with LEVEL meters, wear, tape, screws and a back panel with Settings, Devices, Routing and Credits. See the [design](docs/superpowers/specs/2026-10-10-hotline-design.md) and [plan](docs/superpowers/plans/2026-10-10-hotline.md).

## Structure

- **`vendors/`** — Sources of third-party repos (VST3 SDK, Airwindows, Dusk Audio, EQP-WDF-1A, etc.). Submodules.
- **`shared/`** — Libraries shared by the plugins: `gpu_nam` (NAM on GPU) and `ui` (`fairy_ui`, `fairy_ui_widgets`: code-drawn VSTGUI controls and optional bitmap skin).
- **`src/`** — Source code of Fairy Tapes plugins (e.g. `src/the-stripper/`, `src/pillow-talk/`, `src/hotline/`; `src/doggystyle/` is still to be implemented).
- **`build/`** — CMake output (cache, object files, test binaries). **Not in the repo** (gitignored). Regenerated when you run `./build.sh` or `cmake --build build`.
- **`dist/`** — **Only** the built plugin bundles (e.g. `TheStripper.vst3`, `PillowTalk.vst3`). CMake is configured so that each plugin target writes its `.vst3` directly here when you build; no copy step. **Not in the repo** (gitignored). Use this folder to upload to your store or pay-what-you-want page.

Shared conventions and build setup; no cross-plugin code coupling. **Default branch:** `master` (CI and merges target this branch).

### Download pre-built binaries

Pre-built binaries are **not** included in this repository. You can download them (**pay what you want**, including €0) from:

**→ [Download Fairy Tapes plugins](https://example.com/fairy-tapes-download)** *(replace with your Ko-fi / itch.io / Gumroad / personal page)*

Source code here is free and open source (GPL-3.0); the optional payment is for the convenience of ready-to-use builds and to support development.

**Why GPL-3.0:** The Stripper uses **4K-EQ** and **Multi-Comp** (Dusk Audio, GPL-3.0). The combined work is distributed under GPL-3.0. Full obligations: see [LICENSE](LICENSE).

## Repository setup

1. **Create the repo (if starting from scratch)**  
   From the `fairy-tapes` folder:

   ```bash
   cd /path/to/fairy-tapes
   git init
   git remote add origin <your-repo-url>
   ```

2. **VST3 SDK as submodule (recommended)**  
   The SDK lives **outside** the project tree as a git submodule; the repo stays small and you pin a specific SDK version:

   ```bash
   cd /path/to/fairy-tapes
   mkdir -p vendors
   # If you already have vendors/vst3sdk as a plain clone, remove it first:
   # rm -rf vendors/vst3sdk
   git submodule add https://github.com/steinbergmedia/vst3sdk vendors/vst3sdk
   git add .gitmodules vendors/vst3sdk
   git commit -m "chore: add VST3 SDK as submodule"
   ```

   New clones must fetch the submodules:

   ```bash
   git clone --recurse-submodules <repo-url> fairy-tapes
   # or, if already cloned without submodules:
   git submodule update --init --recursive
   ```

   **Submodules:** `vendors/vst3sdk` (VST3 SDK), `vendors/airwindows` (Airwindows), `vendors/dusk-audio-plugins` (4K-EQ, Multi-Comp, Dusk Audio), `vendors/eqp-wdf-1a` (Pultec EQP-WDF-1A). After clone run `git submodule update --init --recursive`.

   **Note:** The submodule is the current approach; the plan is to switch later to CMake-based dependency management (e.g. `FetchContent`) so the SDK is declared and fetched like other dependencies, without submodules.

## Build (The Stripper)

1. **SDK**  
   Ensure `vendors/vst3sdk` is present (submodule: `git submodule update --init --recursive` after clone, or add it as in Repository setup).

2. **Configure and build**  
   Dalla root **fairy-tapes**:

   **Linux / macOS:**
   ```bash
   ./build.sh              # build di tutti i plugin
   ./build.sh the-stripper # build solo The Stripper
   ./build.sh --configure  # solo configurazione CMake, senza build
   ```

   **Windows:**  
   Si usano ancora i file **.bat** (batch) per automatizzare build da CMD. Dalla root del repo:

   ```cmd
   build.bat              REM build di tutti i plugin (test poi build)
   build.bat the-stripper REM build solo The Stripper
   build.bat --configure  REM solo cmake configure, senza build
   ```

   Richiede CMake in PATH; su Windows è consigliato aprire il **“x64 Native Tools Command Prompt for VS”** (se usi Visual Studio) e lanciare da lì `build.bat the-stripper`.

   Oppure a mano:

   ```bash
   cd /path/to/fairy-tapes
   mkdir build && cd build
   cmake ..
   cmake --build .
   ```

   Non serve passare `VST3_SDK_PATH` se l’SDK è in `vendors/vst3sdk`. Per compilare solo The Stripper dalla sua cartella:

   ```bash
   cd src/the-stripper
   mkdir build && cd build
   cmake .. -DVST3_SDK_PATH=/path/to/fairy-tapes/vendors/vst3sdk
   cmake --build .
   ```

3. **Tests**  
   `./build.sh` runs the test suite **before** building the plugin(s); if tests fail, the build stops. Tests are in `src/the-stripper/tests/` (parameter helpers). From `build/` you can run only tests with `ctest --output-on-failure`.

4. **Install (local use)**  
   The built bundle is written directly to **`dist/TheStripper.vst3`**. Copy it into your host’s VST3 folder (see your DAW/host documentation).

## CI (GitHub Actions)

Push (or PR) to **master** triggers a build on **Linux**, **Windows**, and **macOS**. The workflow:

- Checks out the repo **with submodules** (so `vendors/vst3sdk` is available).
- Builds the plugins and uploads The Stripper, Pillow Talk and Deep Throat `.vst3` bundles as **artifacts** per platform.

**Rough duration (per run):**  
With submodules, the SDK is fetched at checkout. Build is typically **2–6 min per OS**; total for all three platforms is usually **under 15 minutes**.

**GitHub free tier:** 2000 minutes/month. Linux counts 1×, Windows 2×, macOS 10× per minute. A full three-OS run is well within the free tier.

**Using the builds:** In the Actions run, open the **Summary** → **Artifacts** and download e.g. `the-stripper-windows` or `pillow-talk-windows` for your Windows DAW, or the `-linux` / `-macos` artifacts for the other systems. Unzip if needed and place the `TheStripper.vst3` folder in your host’s VST3 path.

## Branch strategy

- **master** is the main branch: only merge when a plugin (or feature) is ready for release.
- **One branch per plugin** while developing, e.g. `dev/the-stripper`, `dev/next-plugin`. Work and commit there; in the meantime you can run as many local builds as you like to test compilation and behaviour.
- Merge into `master` (e.g. via PR) when the plugin is stable. CI runs on every push/PR to `master`.

## Tests

For a VST3 plugin (C++ / DSP), **automated tests are not mandatory** to ship: many commercial plugins have no test suite. They are still **recommended** for:

- **Parameter and state logic**: conversion helpers (`normalizedToDb`, `dbToNormalized`), getState/setState roundtrip, so regressions are caught without loading the plugin in a host.
- **CI**: you can add a small test target (e.g. CTest or a minimal host) that loads the plugin and checks parameters; that’s optional and can be added later.

So: **no obligation** to add tests now; consider them when you want to lock down param/state behaviour or add a “test” step in CI.

## Development

- API keys (e.g. Tone3000): in development use `.env`; in the released plugin the user enters them in the plugin settings.
- See [.cursorrules](.cursorrules) for project conventions, naming, and UI direction.
