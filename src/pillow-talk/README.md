# Fairy Tapes · Pillow Talk

VST3 **tape echo that ducks its own return** while the voice sings and opens in the pauses, so the tails land after each phrase without send automation. Works as an insert on the vocal or on a return bus with an external sidechain.

**Design:** [spec](../../docs/superpowers/specs/2026-10-08-pillow-talk-design.md) · **Controls:** [docs/CONTROLS.md](docs/CONTROLS.md)

## Signal

Main stereo input, optional stereo sidechain input (aux bus, inactive by default), stereo output.

1. Detector: sidechain bus when **Sidechain** is on and the bus is active, otherwise the main input. Band-pass on the vocal range, envelope, threshold, hold (ms or note), exponential release.
2. The tape input stays fully open, so the phrase is recorded. The resulting gain scales the **wet output** before the mix: the repeats keep decaying in the line and are heard when the gate reopens after hold and release.
3. Tape echo: Hermite-interpolated tape with wow/flutter, feedback loop with tone low-pass, biased `tanh` saturation (2x oversampled) and 100 Hz high-pass. Time changes glide.
4. Parallel mix: `out = dry * (1 - mix) + wet * gain * mix`.

Note values (1/16 … 1/2) follow the host tempo; without one, 120 BPM.

## Build

From the suite root (SDK in `vendors/vst3sdk`):

```bash
./build.sh pillow-talk
# or
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target pillow_talk pillow_talk_tests
ctest --test-dir build -R pillow_talk --output-on-failure
```

Output: `dist/Pillow Talk.vst3`. Copy it into your host's VST3 folder.

## Status

- [x] Parameters, versioned state (v1)
- [x] Vocal-aware ducker, tape echo, sidechain bus
- [x] No custom UI: hosts show the standard parameter list
- [ ] Manual check in Reaper and Cubase
