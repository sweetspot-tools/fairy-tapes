# Fairy Tapes

Suite of VST3 audio plugins: real, analog-style tone via NAM (Neural Amp Modeler) and GPU where applicable.

**License:** GNU General Public License v3.0 (GPL-3.0), full text in [LICENSE](LICENSE). Third-party code, fonts and their licences: [THIRD_PARTY.md](THIRD_PARTY.md).

- **Suite**: Fairy Tapes, published by **Sweetspot Tools** ([github.com/sweetspot-tools](https://github.com/sweetspot-tools)): VST3 vendor "Sweetspot Tools", macOS bundle identifiers `com.sweetspot-tools.fairytapes.<plugin>`
- [The Stripper](src/the-stripper/) — **bus-oriented** channel strip (LP/HP, EQ, compression, Gain/Drive with NAM or tape, limiter, gate/expander). See [src/the-stripper/docs/OVERVIEW.md](src/the-stripper/docs/OVERVIEW.md) and [FEATURES.md](src/the-stripper/docs/FEATURES.md) for design and stage choices.
- [Pillow Talk](src/pillow-talk/) — tape echo that ducks its own return while the voice sings and opens in the pauses, so the tails land after each phrase without automation. Works as an insert or on a return bus with sidechain. See [src/pillow-talk/docs/CONTROLS.md](src/pillow-talk/docs/CONTROLS.md) and the [design](docs/superpowers/specs/2026-10-08-pillow-talk-design.md).
- [Hotline](src/hotline/) — device effect for voice: megaphone, telephone, walkie-talkie, AM radio, station PA and cheap speaker from one chain with a single automatable Morph from clean to device and zero latency. See the [design](docs/superpowers/specs/2026-10-10-hotline-design.md) and [plan](docs/superpowers/plans/2026-10-10-hotline.md).
- [Deep Throat](src/deep-throat/) — channel strip for vocals: fixed chain, a mode selector and a few knobs. See the [design](docs/superpowers/specs/2026-10-10-deep-throat-design.md).
- [Happy Ending](src/happy-ending/) — mastering chain for the master bus, with stepped controls and narrow ranges. See the [design](docs/superpowers/specs/2026-10-21-happy-ending-design.md).
- [Doggystyle](src/doggystyle/) (designed, not implemented yet) — aggressive parallel compression for drums. See the [design](docs/superpowers/specs/2026-10-09-doggystyle-design.md) and [plan](docs/superpowers/plans/2026-10-09-doggystyle.md).

## Structure

- **`vendors/`** — Third-party repositories as git submodules, pinned to a commit. See [THIRD_PARTY.md](THIRD_PARTY.md).
- **`shared/`** — Libraries shared by the plugins: `gpu_nam` (NAM on GPU), `nam_drive`, `inflator` and `ui` (`fairy_ui`, `fairy_ui_widgets`: code-drawn VSTGUI controls).
- **`src/`** — One folder per plugin.
- **`LICENSES/`** — Licence texts of ported third-party code that is not in a fetched submodule.
- **`build/`** — CMake output. **Not in the repo** (gitignored).
- **`dist/`** — **Only** the built plugin bundles (e.g. `TheStripper.vst3`, `PillowTalk.vst3`); each plugin target writes its `.vst3` here. **Not in the repo** (gitignored).

Shared conventions and build setup; no cross-plugin code coupling. **Default branch:** `master` (CI and merges target this branch).

### Download pre-built binaries

Pre-built binaries are **not** included in this repository. Download them from the **[GitHub Releases](https://github.com/sweetspot-tools/fairy-tapes/releases)** of this repository; each release is built from the tagged source.

## Getting the sources

```bash
git clone --recurse-submodules https://github.com/sweetspot-tools/fairy-tapes.git
cd fairy-tapes
# if you cloned without --recurse-submodules:
git submodule update --init --recursive
```

This fetches what the build needs: `vendors/vst3sdk` (VST 3 SDK and VSTGUI) and `vendors/NeuralAmpModelerCore` (with its own dependencies).

`vendors/airwindows` and `vendors/dusk-audio-plugins` are **reference sources**: their code is already ported into `src/` and `shared/`, so they are marked `update = none` and skipped by the commands above. They stay as submodules to keep the exact upstream commit the ports came from. To fetch them (for example to port another algorithm or compare with the original):

```bash
git submodule update --init --checkout vendors/airwindows vendors/dusk-audio-plugins
```

Do not move a submodule to another commit by accident (`git pull` inside it): update a pinned commit only on purpose, adjusting the CMake source lists and running the tests.

## Build (The Stripper)

1. **SDK**  
   Ensure `vendors/vst3sdk` is present (see [Getting the sources](#getting-the-sources)).

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
- Builds the plugins and uploads The Stripper, Pillow Talk, Deep Throat and Hotline `.vst3` bundles as **artifacts** per platform.

**Rough duration (per run):**  
With submodules, the SDK is fetched at checkout. Build is typically **2–6 min per OS**; total for all three platforms is usually **under 15 minutes**.

**GitHub free tier:** 2000 minutes/month. Linux counts 1×, Windows 2×, macOS 10× per minute. A full three-OS run is well within the free tier.

**Using the builds:** In the Actions run, open the **Summary** → **Artifacts** and download e.g. `the-stripper-windows`, `pillow-talk-windows` or `hotline-windows` for your Windows DAW, or the `-linux` / `-macos` artifacts for the other systems. Unzip if needed and place the `TheStripper.vst3` folder in your host’s VST3 path.

## Branches

- **master** is the released line; it only receives merges of **pre-release**.
- **pre-release** collects the work; feature branches (`feat/<plugin>`, `fix/<topic>`, `docs/<topic>`) start from it and merge back into it when needed.
- CI runs on push and PR to `master`.

## Tests

Each plugin has a test target (for example `the_stripper_tests`); `ctest --output-on-failure` from the build folder runs them. Tests stay green before and after every change, and new code comes with a test.

## Development

- Conventions for contributors and coding agents: [CLAUDE.md](CLAUDE.md) and [.cursorrules](.cursorrules). Design decisions, specs and plans are in `docs/superpowers/` (in Italian).
- Shared visual rules of the editors: [docs/DESIGN_SYSTEM.md](docs/DESIGN_SYSTEM.md). HTML previews of every panel: [docs/mockups/index.html](docs/mockups/index.html) (open it from a local web server rooted in the repository).
- Local settings go in `.env` (gitignored); [.env.example](.env.example) lists the keys.

## Trademarks

VST is a trademark of Steinberg Media Technologies GmbH, registered in Europe and other countries.
