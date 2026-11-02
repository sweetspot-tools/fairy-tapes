# Third-party components

Fairy Tapes is released under the **GNU General Public License v3.0** ([LICENSE](LICENSE)). It builds on the work of the
projects below. Every ported source file starts with a header that names its origin, author and licence.

## Build dependencies (submodules, fetched by a recursive clone)

| Path | Project | Author | Licence | Used for |
|---|---|---|---|---|
| `vendors/vst3sdk` | [VST 3 SDK](https://github.com/steinbergmedia/vst3sdk) | Steinberg Media Technologies | MIT (`LICENSE.txt`) | plug-in format, every plug-in |
| `vendors/vst3sdk/vstgui4` | VSTGUI | Steinberg Media Technologies | BSD-3-Clause (`LICENSE`) | editors |
| `vendors/NeuralAmpModelerCore` | [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) | Steven Atkinson | MIT | NAM models (`shared/nam_drive`, `shared/gpu_nam`, The Stripper) |
| `vendors/NeuralAmpModelerCore/Dependencies/AudioDSPTools` | AudioDSPTools | Steven Atkinson | MIT | NAM core dependency |
| `vendors/NeuralAmpModelerCore/Dependencies/eigen` | [Eigen](https://gitlab.com/libeigen/eigen) | Eigen contributors | MPL-2.0 | NAM core dependency |
| `vendors/NeuralAmpModelerCore/Dependencies/nlohmann` | [nlohmann/json](https://github.com/nlohmann/json) | Niels Lohmann | MIT | reading `.nam` files |

The commit of each submodule is pinned by this repository (`git submodule status --recursive`).

**Eigen's benchmarks are not part of this repository or of any build.** Eigen's `bench/` directory (the benchmark suite;
`bench/btl` carries its own GPL-2.0-only `COPYING`, which cannot be combined with GPL-3.0) exists only inside the pinned
Eigen checkout, which this repository references and does not copy. The build uses Eigen's headers only
(`shared/nam_drive` and `shared/gpu_nam` add `Dependencies/eigen` to the include path), so nothing from `bench/` is
compiled, linked or distributed with the plug-ins. `.license-compliance` records this for the license checker.

## Reference sources (submodules not fetched by default)

These two repositories are the origin of code that is already **ported into this repository** (`src/`, `shared/`). The
build does not need them, so `.gitmodules` marks them `update = none` and a recursive clone skips them. They stay as
submodules so the URL and the exact commit used for the ports are recorded; fetch them when you need to compare with or
port from the original:

```bash
git submodule update --init --checkout vendors/airwindows vendors/dusk-audio-plugins
```

| Path | Project | Author | Licence | Pinned commit |
|---|---|---|---|---|
| `vendors/airwindows` | [Airwindows](https://github.com/airwindows/airwindows) | Chris Johnson | MIT ([LICENSES/Airwindows-MIT.txt](LICENSES/Airwindows-MIT.txt)) | `e68f75154ed0520fc5757fec9e7792dd1747e5ee` |
| `vendors/dusk-audio-plugins` | [Dusk Audio plugins](https://github.com/dusk-audio/dusk-audio-plugins) (formerly Luna Co. Audio) | Dusk Audio | GPL-3.0 ([LICENSE](LICENSE)) | `9b2ced59c425c7e74920c740918cbc049f4e7d0c` |

## Ported code

| Original | Author | Licence | Ported to |
|---|---|---|---|
| 4K-EQ (filters, `SSLSaturation.h`) | Dusk Audio | GPL-3.0 | `src/the-stripper/src/dsp/fourk_eq.*`, `ssl_saturation.h`, `biquad.h` |
| Multi-Comp (engines, hardware emulation) | Dusk Audio | GPL-3.0 | `src/the-stripper/src/dsp/multicomp_engines.*`, `hardware_emulation/`, `compression_stub.*` |
| Multi-Comp `BusCompressor` | Dusk Audio | GPL-3.0 | `src/happy-ending/src/dsp/glue.*` |
| Multi-Q (`AnalogMatchedBiquad`, Tube EQ, `ADAASaturation`) | Dusk Audio | GPL-3.0 | `src/happy-ending/src/dsp/matched_biquad.h`, `vibe_eq.*` |
| BlockParty, DeNoise, ButterComp, Tape | Chris Johnson (Airwindows) | MIT | `src/the-stripper/src/dsp/blockparty.*`, `denoise.*`, `buttercomp.*`, `tape_saturation.*` |
| ClipSoftly, ClipOnly2 | Chris Johnson (Airwindows) | MIT | `src/happy-ending/src/dsp/clip_softly.h`, `clip_only2.h` |
| [ZLInflator](https://github.com/ZL-Audio/ZLInflator) (shaper curves, mixer) | zsliu98 (ZL Audio) | GPL-3.0 | `shared/inflator/inflator.h` |
| [libebur128](https://github.com/jiixyj/libebur128) (K-weighting prototype constants only) | Jan Kokemüller | MIT ([LICENSES/libebur128-MIT.txt](LICENSES/libebur128-MIT.txt)) | `src/happy-ending/src/dsp/loudness_meter.*` |
| "Matched Second Order Digital Filters" (published method, own implementation) | Martin Vicanek | — | `src/happy-ending/src/dsp/matched_biquad.h` |

## Fonts

Embedded as vector outlines by `shared/ui/tools/gen_tape_fonts.py`; sources, hashes and licence texts are in
[shared/ui/assets/fonts/](shared/ui/assets/fonts/README.md).

| Font | Author | Licence |
|---|---|---|
| Zeyada | Kimberly Geswein | SIL Open Font License 1.1 |
| Rock Salt | Font Diner (Sideshow) | Apache-2.0 |
| Homemade Apple | Font Diner | Apache-2.0 |

## Not in this repository

- **Brand** (logo, avatar, favicon): [sweetspot-tools/brand](https://github.com/sweetspot-tools/brand), folder
  `fairy-tapes/`, licensed CC BY-ND 4.0 separately from the code. READMEs link it with raw URLs; it is not a submodule.

## Trademarks

VST is a trademark of Steinberg Media Technologies GmbH, registered in Europe and other countries.
