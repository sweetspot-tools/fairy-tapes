# Fairy Tapes

A suite of VST3 audio plug-ins with an analog, hands-on feel. Each plug-in does one job with a few controls, behind a
rack-style panel with worn paint, paper tape you can write on and screws you can turn.

Published by **Sweetspot Tools** ([github.com/sweetspot-tools](https://github.com/sweetspot-tools)). VST3 vendor
"Sweetspot Tools", macOS bundle identifiers `com.sweetspot-tools.fairytapes.<plugin>`.

**License:** GNU General Public License v3.0 (GPL-3.0), full text in [LICENSE](LICENSE). Third-party code, fonts and
their licences: [THIRD_PARTY.md](THIRD_PARTY.md).

> **Work in progress.** The plug-ins are under active development and not released yet. Each README states what works
> and what is still to be checked in a host.

## The plug-ins

| Plug-in | Put it on | What it does | Status |
|---|---|---|---|
| [The Stripper](src/the-stripper/) | single tracks, groups, mix bus | Channel strip: filters, 4-band EQ, compression, drive through a NAM model or tape, limiter and gate. One switch picks Channel or Mix bus voicing. | DSP and editor done; host, Windows/macOS and CUDA checks pending |
| [Pillow Talk](src/pillow-talk/) | vocals, or a return bus | Tape echo that ducks its own repeats while the singer sings and lets them bloom in the pauses. No send automation. | Done; manual check in hosts pending |
| [Hotline](src/hotline/) | vocals, intros, any track | Makes the sound come out of a megaphone, phone, walkie-talkie, AM radio, station PA or cheap speaker. One Morph knob goes from clean to device. | Done; manual check in hosts pending |
| [Deep Throat](src/deep-throat/) | vocals: speech, singing, screams | Vocal channel strip with three modes: level rider, breath reduction, corrective EQ, de-esser, compressor and tone. | DSP and editor done; manual check in hosts pending |
| [Happy Ending](src/happy-ending/) | the master bus | Mastering chain: fine EQ, vibe EQ, glue compressor, clipper, limiter and final loudness, with LUFS metering and a fixed ceiling. | Audio engine done; editor not built yet; by-ear tuning pending |
| [Doggystyle](src/doggystyle/) | drum groups, bass and guitar groups | Aggressive parallel compression with two opposite characters. | Designed, not implemented |

Each README has the signal flow diagram of its plug-in (the ROUTING page of the panel), the controls and the build
commands.

## Download

Pre-built binaries are **not** in this repository. They will be published in the
**[GitHub Releases](https://github.com/sweetspot-tools/fairy-tapes/releases)**, each built from the tagged source.
Until then, build from source (below) or take the CI artifacts (see [CI](#ci-github-actions)).

To install, copy the `.vst3` bundle into your host's VST3 folder:

| System | Folder |
|---|---|
| Windows | `C:\Program Files\Common Files\VST3` |
| macOS | `~/Library/Audio/Plug-Ins/VST3` (or `/Library/Audio/Plug-Ins/VST3`) |
| Linux | `~/.vst3` (or `/usr/lib/vst3`) |

On Linux the editors embed only in X11 windows: the host must run on X11 or XWayland.

## Getting the sources

```bash
git clone --recurse-submodules https://github.com/sweetspot-tools/fairy-tapes.git
cd fairy-tapes
# if you cloned without --recurse-submodules:
git submodule update --init --recursive
```

This fetches what the build needs: `vendors/vst3sdk` (VST 3 SDK and VSTGUI) and `vendors/NeuralAmpModelerCore` (with
its own dependencies).

`vendors/airwindows` and `vendors/dusk-audio-plugins` are **reference sources**: their code is already ported into
`src/` and `shared/`, so they are marked `update = none` and skipped by the commands above. They stay as submodules to
record the exact upstream commit the ports came from. To fetch them (to compare with the original or port another
algorithm):

```bash
git submodule update --init --checkout vendors/airwindows vendors/dusk-audio-plugins
```

Do not move a submodule to another commit by accident (`git pull` inside it): change a pinned commit only on purpose,
then adjust the CMake source lists and run the tests.

## Build

Requirements: CMake 3.25 or newer and a C++20 compiler (Visual Studio 2022, Xcode, GCC or Clang). On Linux, the
system packages VSTGUI needs (list used by the CI, Ubuntu 24.04):

```bash
sudo apt-get install -y \
  libcairo2-dev libfreetype-dev libfontconfig1-dev libpango1.0-dev \
  libxcb1-dev libxcb-util-dev libxcb-cursor-dev libxcb-keysyms1-dev libxcb-xkb-dev \
  libxkbcommon-dev libxkbcommon-x11-dev \
  libx11-dev libx11-xcb-dev libwayland-dev wayland-protocols \
  libglib2.0-dev libgtkmm-3.0-dev libsqlite3-dev libexpat1-dev
```

Build one plug-in with CMake (the build folder can live anywhere; `GPU_NAM_DISABLE_CUDA=ON` skips the optional CUDA
path of The Stripper):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DGPU_NAM_DISABLE_CUDA=ON
cmake --build build --config Release -j2 --target hotline
```

Targets: `the_stripper`, `pillow_talk`, `hotline`, `deep_throat`, `happy_ending`. Each bundle is written to `dist/`
(`TheStripper.vst3`, `PillowTalk.vst3`, `Hotline.vst3`, `DeepThroat.vst3`, `HappyEnding.vst3`). Building every target
at once needs a lot of memory: build the ones you need.

The scripts in the root wrap the same steps and run the tests first:

```bash
./build.sh              # all plug-ins
./build.sh the-stripper # one plug-in
./build.sh --configure  # configure only
```

On Windows, `build.bat` does the same from a "x64 Native Tools Command Prompt for VS" (`build.bat the-stripper`).

## Tests

Each plug-in has its test targets (for example `the_stripper_tests`, `hotline_tests`); the plug-in READMEs list them.
Build the ones you need, then run `ctest --test-dir build --output-on-failure` (add `-R <plugin>` to filter). Tests stay
green before and after every change, and new code comes with a test.

## CI (GitHub Actions)

A push or pull request to `master` builds the suite on Linux, Windows and macOS
([.github/workflows/build.yml](.github/workflows/build.yml)). The workflow checks out the submodules, builds, and
uploads one artifact per plug-in and system: `the-stripper-<os>`, `pillow-talk-<os>`, `deep-throat-<os>`,
`hotline-<os>`, `happy-ending-<os>` with `<os>` = `linux`, `windows` or `macos`. The CI does not run `ctest`.

To use a build: open the run, **Summary**, **Artifacts**, download the one for your system, unzip it and copy the
`.vst3` folder into your host's VST3 folder.

## Repository layout

- **`src/`**: one folder per plug-in, with its README, `docs/` (notes, HTML preview of the panel, images) and `tests/`.
- **`shared/`**: code shared by the plug-ins: `gpu_nam` (NAM on GPU, CUDA), `nam_drive`, `inflator` and `ui`
  (`fairy_ui`, `fairy_ui_widgets`: the controls, wear, tape, screws and back panel drawn by code).
- **`vendors/`**: third-party repositories as git submodules, pinned to a commit. See [THIRD_PARTY.md](THIRD_PARTY.md).
- **`LICENSES/`**: licence texts of ported third-party code whose source is not in a fetched submodule.
- **`docs/`**: shared design rules ([DESIGN_SYSTEM.md](docs/DESIGN_SYSTEM.md)), the page with every preview
  ([docs/mockups/index.html](docs/mockups/index.html), open it from a local web server rooted in the repository) and
  the design specs and plans in `docs/superpowers/` (in Italian).
- **`build/`** and **`dist/`**: build output, not in the repository (gitignored).

The signal flow images in each README are rendered from the HTML previews by
[docs/mockups/render_routing.js](docs/mockups/render_routing.js).

## Branches

- **master** is the released line; it only receives merges of **pre-release**.
- **pre-release** collects the work; feature branches (`feat/<plugin>`, `fix/<topic>`, `docs/<topic>`) start from it
  and merge back into it when needed.
- CI runs on push and pull request to `master`.

## Development

- Conventions for contributors and coding agents: [CLAUDE.md](CLAUDE.md) and [.cursorrules](.cursorrules).
- Local settings go in `.env` (gitignored); [.env.example](.env.example) lists the keys.

## Trademarks

VST is a trademark of Steinberg Media Technologies GmbH, registered in Europe and other countries. Hardware and product
names that appear in the documentation describe a sound or a style; their owners do not endorse these plug-ins.
