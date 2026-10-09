# Fairy Tapes · Pillow Talk

VST3 **tape echo that ducks its own return** while the voice sings and opens in the pauses, so the tails land after each phrase without send automation. Works as an insert on the vocal or on a return bus with an external sidechain.

**Design:** [spec](../../docs/superpowers/specs/2026-10-08-pillow-talk-design.md) · **Controls:** [docs/CONTROLS.md](docs/CONTROLS.md) · **Skin:** [docs/UI_ASSETS.md](docs/UI_ASSETS.md)

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

## Editor

Finestra fissa 480 x 560 disegnata da codice (VSTGUI4, libreria condivisa `shared/ui`). Dall'alto in basso:

- intestazione con titolo e pulsanti **Sidechain** e **Bypass**;
- **display del ducking** (circa 6 s di storia): area grigia = voce, area ambra = eco, linea verde = livello di mandata (gain applicato al wet), linea tratteggiata = Threshold;
- sezione **Delay**: Time, Feedback, Tone, Saturation, Wow/Flutter, Mix; sotto, Sync e le note 1/16, 1/8, 1/4, 1/2 per il Time;
- sezione **Auto Duck**: Threshold, Depth, Hold, Release; sotto, Sync e le note per l'Hold.

Gesti: trascinamento verticale sulle manopole, **Maiusc** = regolazione fine, **doppio clic** = valore di default. I gesti arrivano all'host come begin/perform/end edit, quindi automazione e undo funzionano.

Sync: con Sync **On** la manopola in ms è attenuata, con Sync **Off** sono attenuati i pulsanti nota. I controlli attenuati **rispondono comunque** (è voluto).

Il display legge tre parametri nascosti e di sola lettura (ID 1000-1002) scritti dal processore circa ogni 16 ms. Se l'host non li inoltra al controller, il display resta fermo (voce 0, mandata 1, eco 0) ma il plugin funziona normalmente.

Linux: solo embedding X11. Le finestre native Wayland vengono rifiutate, quindi Reaper usa X11/XWayland.

Grafiche opzionali: [docs/UI_ASSETS.md](docs/UI_ASSETS.md).

## Verifica manuale

- [ ] Aprire e chiudere l'editor 50 volte (nessun crash, nessun blocco).
- [ ] Muovere ogni controllo e controllare che l'automazione lo registri e che l'undo lo annulli.
- [ ] Sync On/Off mentre si automatizzano Time e Hold: la manopola ms e le note si attenuano di conseguenza.
- [ ] Display con voce reale e con sidechain: voce, eco e mandata si muovono, la linea tratteggiata segue Threshold.
- [ ] Finestra su schermo HiDPI: nitida, controlli allineati.
- [ ] I pulsanti nota attenuati si possono cliccare (è voluto).
- [ ] Linux: embedding solo X11; con Wayland nativo l'editor viene rifiutato e Reaper deve usare X11/XWayland.

## Status

- [x] Parameters, versioned state (v1)
- [x] Vocal-aware ducker, tape echo, sidechain bus
- [x] VSTGUI editor (480 x 560) with the ducking display
- [ ] Manual check in Reaper and Cubase (see Verifica manuale)
