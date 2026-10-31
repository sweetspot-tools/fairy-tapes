# Fairy Tapes · Happy Ending

VST3 **mastering chain** for the master bus, for the final stage of a project: a fixed all-in-one chain with stepped controls, narrow ranges and starting values that already suit a master. Stereo in, stereo out (mono → mono is accepted and processed as dual mono). No sidechain.

**Status:** audio engine and VST3 plug-in done (plan Tasks 1-13). The by-ear tuning of `src/tuning.h` is waiting for the user. The graphic editor has its own plan, so the host shows its generic parameter list for now.

**Design:** [spec](../../docs/superpowers/specs/2026-10-21-happy-ending-design.md) · [plan](../../docs/superpowers/plans/2026-10-24-happy-ending.md) · **Preview:** `docs/mockups/happy_ending_rack.html`

## Signal chain

INPUT → FINE EQ → VIBE EQ → GLUE → CLIP → LIMIT → FINAL → METALHEAD → OUTPUT, with a global BYPASS and a LUFS meter at the output.

- **INPUT / OUTPUT:** ±12 dB in 0.5 dB steps. OUTPUT is followed by a transparent base-rate safety limiter (threshold = CEIL, no gain recovery so it never raises the level, 1 ms look-ahead, 50 ms release) and, behind it, a hard ceiling guard as the last net: whenever LIMIT, FINAL or METALHEAD is on, no output sample exceeds CEIL (−0.1 / −0.3 / −0.5 / −1.0 dB), OUTPUT gain included.
- **FINE EQ:** six stepped bands (HI PASS, LOW SHELF, LO MID, MID, HI MID, HI SHELF), each with IN and M/S (ST, MID, SIDE), LISTEN on the three bells. Analog-matched biquads (Vicanek designs for bells and high shelf): zero latency, correct up to Nyquist, minimum phase.
- **VIBE EQ:** Pultec-style passive network: LOW (boost plus the cut above it), AIR, TILT, with DYN that pulls the boost back on loud program, and a TUBE stage.
- **GLUE:** SSL-style bus compressor in parallel (MIX), stereo linked, SC HPF, WARM.
- **CLIP:** level-matched soft clipper (ClipSoftly).
- **LIMIT:** look-ahead sample-peak limiter with styles MODERN / PUNCH / DYN, a RELEASE trim and a guided SET.
- **FINAL:** INFLATE or CLIP (ClipOnly2), under the ceiling.
- **METALHEAD:** optional brickwall after FINAL (off by default), PUSH 0…+6 dB.
- **LOUDNESS** (0-10) pushes CLIP, then LIMIT, then FINAL on top of the stage settings; **TARGET** (−14 / −11 / −9 / −7 LUFS) and **MATCH** set LOUDNESS once to hit the target.
- **Oversampling** (Off / 2× / 4×, default 2×, a setting stored in the project): linear-phase half-band stages for the non-linear parts (TUBE, then CLIP → LIMIT → FINAL → METALHEAD). Flat to 20 kHz at 44.1 kHz. The oversampler latency is 69 samples at 2× and 76 at 4× (base rate) and adds to the look-aheads (including the 1 ms look-ahead of the safety limiter, constant whether it is active or not); the total is reported to the host and changes only when the setting changes.

There is no true-peak detection (design choice): the limiters work on the sample peak, and CEIL −1.0 dB is the margin for streaming.

BYPASS crossfades to the input delayed by the reported latency, sample-exact once settled. LISTEN and SET are never saved in the state and always start off. The state is versioned (v2 adds the decor; v1 projects load with a fresh decor; a v2 whose decor part is damaged still loads its parameters).

## Build

Outside the repo (the `/srv/http` disk is small):

```
cmake -S . -B /tmp/he-build -DCMAKE_BUILD_TYPE=Release -DGPU_NAM_DISABLE_CUDA=ON
cmake --build /tmp/he-build --target happy_ending -j2
```

The bundle is `HappyEnding.vst3`, built locally in `dist/`. CI (on merge to `master`) builds all targets and uploads it as `happy-ending-linux`, `happy-ending-windows` and `happy-ending-macos`; it does not run `ctest`. Do not build all targets together; for the tests build only the test targets you need (`happy_ending_<block>_tests`, listed below).

## Tests

```
cmake --build /tmp/he-build --target happy_ending_params_tests happy_ending_fine_eq_tests \
  happy_ending_vibe_eq_tests happy_ending_glue_tests happy_ending_oversampler_tests \
  happy_ending_clip_tests happy_ending_limiter_tests happy_ending_final_tests \
  happy_ending_loudness_tests happy_ending_loudness_macro_tests happy_ending_engine_tests \
  happy_ending_state_tests happy_ending_display_tests happy_ending_controller_tests \
  happy_ending_factory_test happy_ending_processor_level_test happy_ending_state_module_test -j2
ctest --test-dir /tmp/he-build -R happy_ending --output-on-failure
```

One executable per block, with the `RUN_TEST` / `ASSERT_TRUE` harness and synthetic signals; the three module tests (`factory`, `processor_level`, `state_module`) load `HappyEnding.vst3` and are skipped (exit 77) when it is missing; the others link the code directly. They cover the parameter table, every DSP block (curves against analog prototypes, M/S, LISTEN, DYN, GLUE reduction and MIX, level-matched CLIP, ceiling guarantees for every LIMIT style, SET, FINAL and METALHEAD, oversampler latency and flatness, BS.1770 reference loudness, LOUDNESS and MATCH), the engine (BYPASS, latency per oversampling, all-max never above the ceiling, silence after the tail, no allocation in `process`), the saved state (v2: parameters plus the instance decor: wear seed, screw angles, tape texts) and the processor. All tuning values are read from `src/tuning.h`, never copied.

## Tuning

Every value to be voiced by ear lives in `src/tuning.h` and only there: VIBE DYN threshold and times, LIMIT release bases, SET threshold, LOUDNESS curve, MATCH step. They are still the starting values of the preview; the by-ear pass in a host (REAPER or Bitwig) is waiting for the user.

## Licences

The plug-in is **GPL-3.0**. Third-party code is in the submodules under `vendors/` (pinned by the repo); every ported file starts with a header naming origin, author, licence and path.

| Component | Author | Licence | Used for | Path |
|---|---|---|---|---|
| Multi-Q, `AnalogMatchedBiquad` | Dusk Audio | GPL-3.0 | matched biquad port (`dsp/matched_biquad.h`) for FINE EQ and VIBE EQ | `vendors/dusk-audio-plugins/plugins/multi-q/AnalogMatchedBiquad.h` |
| Multi-Q, Tube EQ (`InductorModel`, `TubeEqLFSection`, `TubeEQTubeStage`, `ADAASaturation`) | Dusk Audio | GPL-3.0 | VIBE EQ (`dsp/vibe_eq.*`) | `vendors/dusk-audio-plugins/plugins/multi-q/TubeEQProcessor.h`, `.../ADAASaturation.h` |
| Multi-Comp, `BusCompressor` | Dusk Audio | GPL-3.0 | GLUE (`dsp/glue.*`) | `vendors/dusk-audio-plugins/plugins/multi-comp/multicomp.cpp` |
| ClipSoftly | Chris Johnson (Airwindows) | MIT | CLIP (`dsp/clip_softly.h`) | `vendors/airwindows/plugins/LinuxVST/src/ClipSoftly/ClipSoftlyProc.cpp` |
| ClipOnly2 | Chris Johnson (Airwindows) | MIT | FINAL clip mode (`dsp/clip_only2.h`) | `vendors/airwindows/plugins/LinuxVST/src/ClipOnly2/ClipOnly2Proc.cpp` |
| ZLInflator | zsliu98 (ZL Audio) | GPL-3.0 | FINAL inflate, through `shared/inflator` | `shared/inflator/inflator.h` (port of github.com/ZL-Audio/ZLInflator) |
| libebur128 | Jan Kokemüller | MIT | prototype constants of the K-weighting filters (`dsp/loudness_meter.cpp`) | not vendored: constants only, no code copied |
| "Matched Second Order Digital Filters" | Martin Vicanek (2016) | published method | matched peaking and high shelf (`dsp/matched_biquad.h`) | not vendored: credit for the method, own implementation |

Submodule commits at the time of writing: `vendors/dusk-audio-plugins` 9b2ced59c425c7e74920c740918cbc049f4e7d0c, `vendors/airwindows` e68f75154ed0520fc5757fec9e7792dd1747e5ee, `vendors/vst3sdk` 3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96 (VST 3 SDK and VSTGUI, Steinberg: MIT and BSD-3-Clause). ZL Equalizer was excluded (AGPL-3.0). The LIMIT, METALHEAD, oversampler and BS.1770 meter code is our own. Licences are to be rechecked against the `LICENSE` files before release.
