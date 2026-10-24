# Fairy Tapes · Hotline

VST3 **device effect** for voice (and, for effected intros, instruments or the master): it makes the sound come out of a megaphone, a telephone, a walkie-talkie, an AM radio, a station PA or a cheap speaker. One **Morph** knob goes from the clean signal to the device, so an intro can be automated with a single curve. Zero latency.

**Design:** [spec](../../docs/superpowers/specs/2026-10-10-hotline-design.md) · **Plan:** [plan](../../docs/superpowers/plans/2026-10-10-hotline.md)

## Devices

Megaphone, Telephone, Walkie-talkie, AM radio, Station PA, Cheap speaker. Each one is a row of a table (`src/devices.cpp`) that sets the blocks of a single chain: band (high/low pass), asymmetric drive, cone resonance (and horn), crush (codec), compander with squelch, noise and a short room. The DEVICES tab on the back of the panel is generated from the same table.

## Controls

- **Device** (six buttons) and **Morph** (0 = transparent, 1 = the device; automatable without clicks).
- **CHARACTER** (one block with its own **IN**): Drive, Tone, Noise, Room, Mix. IN off ignores the five of them.
- **Gain**, **Mono**, **Bypass** (bit-exact copy of the input).
- LEVEL: stereo IN and OUT meters, clip LEDs for IN, DRV and OUT.
- Editor: 1000 x 470 rack panel with wear, paper tapes and screws; back panel with SETTINGS, DEVICES, ROUTING and CREDITS. Preferences (wear, tape font) in `FairyTapes/Hotline.prefs`.

## Build

From the suite root (SDK in `vendors/vst3sdk`), in a scratch folder outside the repo:

```bash
cmake -S . -B /tmp/hotline-build -DCMAKE_BUILD_TYPE=Release -DGPU_NAM_DISABLE_CUDA=ON
cmake --build /tmp/hotline-build -j2 --target hotline hotline_tests hotline_ui_tests hotline_decor_tests \
  hotline_back_tests hotline_factory_test hotline_state_module_test hotline_processor_test
ctest --test-dir /tmp/hotline-build -R hotline --output-on-failure
```

Output: `dist/Hotline.vst3`. Optional offscreen renders of the editor (no host needed): target `hotline_render`.

## License

The code is licensed under the GPL-3.0 (see the root `LICENSE`), like the rest of the suite. Hotline uses the VST 3 SDK, VSTGUI and the three tape fonts (see its CREDITS tab); it has none of The Stripper's third-party DSP components.
