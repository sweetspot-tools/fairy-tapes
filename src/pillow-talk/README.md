# Fairy Tapes · Pillow Talk

VST3 **tape echo that ducks its own return** while the voice sings and opens in the pauses, so the tails land after each phrase without send automation. Works as an insert on the vocal or on a return bus with an external sidechain.

**Design:** [spec](../../docs/superpowers/specs/2026-10-08-pillow-talk-design.md) · **Controls:** [docs/CONTROLS.md](docs/CONTROLS.md) · **Grafica:** [docs/UI_ASSETS.md](docs/UI_ASSETS.md)

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

Output: `dist/PillowTalk.vst3`. Copy it into your host's VST3 folder.

## Linux: dipendenze di sistema

Per compilare (il VSTGUI4 dell'SDK le richiede; elenco della CI, Ubuntu 24.04):

```bash
sudo apt-get install -y \
  libcairo2-dev libfreetype-dev libfontconfig1-dev libpango1.0-dev \
  libxcb1-dev libxcb-util-dev libxcb-cursor-dev libxcb-keysyms1-dev libxcb-xkb-dev \
  libxkbcommon-dev libxkbcommon-x11-dev \
  libx11-dev libx11-xcb-dev libwayland-dev wayland-protocols \
  libglib2.0-dev libgtkmm-3.0-dev libsqlite3-dev libexpat1-dev
```

Per eseguire l'editor servono le corrispondenti librerie a runtime (Cairo, Pango, FreeType, Fontconfig, xcb, xkbcommon). Solo embedding X11: le finestre native Wayland vengono rifiutate, quindi l'host deve usare X11/XWayland (Reaper).

## Editor

Finestra fissa **1000 x 628** (dimensione logica), rack orizzontale disegnato interamente da codice (VSTGUI4, libreria condivisa `shared/ui`); riferimento visivo: [docs/mockups/pillow_rack.html](docs/mockups/pillow_rack.html), regole comuni in [docs/DESIGN_SYSTEM.md](../../docs/DESIGN_SYSTEM.md). Su Windows e Linux l'host comunica il fattore di scala (`IPlugViewContentScaleSupport`) e il frame si ingrandisce: a 200 % la finestra è 2000 x 1256. Su macOS la scala Retina è del sistema. L'utente non può ridimensionare l'editor.

Cinque riquadri, con il titolo sul bordo inferiore (tabella dati in `src/ui/layout.h`):

- **DUCKING**: display del ducking (finestra di 6 s per default) e pulsante **Sidechain**. Area grigia = voce (inviluppo del segnale filtrato che pilota il rilevatore, lo stesso confrontato con Threshold; un seno stazionario si legge circa 4 dB sotto il suo picco), area ambra = eco in uscita (dopo ducking e Mix), linea verde = livello di mandata (gain applicato al wet), linea tratteggiata = Threshold.
- **LEVEL**: meter a LED di ingresso (IN L/R, prima di tutto) e uscita (OUT L/R, dopo Mix e Bypass), scala -60 ... 0 dB; riga **DUCK** = riduzione della mandata (1 - gain, ambra); due **LED di clip** (IN, OUT) che si accendono con un picco a 0 dBFS o oltre e restano accesi 2 s; un **clic su un LED lo azzera**. Il picco massimo si mantiene sul meter per il tempo scelto in Settings (DISPLAY PEAKS: Off / 1.5 s / 3 s; caduta 20 dB/s).
- **OUTPUT**: manopola **Mix** al centro, **Bypass** in alto a destra.
- **DELAY**: Time, Feedback, Tone, Saturation, Wow/Flutter; gruppo **Time Sync** con le note 1/16, 1/8, 1/4, 1/2.
- **AUTO DUCK**: Threshold, Depth, Hold, Release; gruppo **Hold Sync** con le note.

Controlli in alluminio nero (componenti comuni `RackKnob`, `RackToggle`, `RackChoice`). Pulsanti on/off (Sync, Sidechain, Bypass) con LED rosso, selettori (note) con LED giallo. Gesti: trascinamento verticale sulle manopole, **Maiusc** = regolazione fine, **doppio clic** = valore di default. I gesti arrivano all'host come begin/perform/end edit: automazione e undo funzionano.

Attenuazione: Sync **On** attenua la manopola in ms, Sync **Off** attenua i pulsanti nota; il **Bypass** attenua tutto tranne sé stesso (display compreso; i meter IN/OUT e i LED di clip restano accesi, la riga DUCK si attenua). I controlli attenuati **rispondono comunque** (è voluto).

Il display e il LEVEL leggono parametri nascosti di sola lettura (ID da 1000, scritti dal processore circa ogni 16 ms; vedi [docs/CONTROLS.md](docs/CONTROLS.md)). Se l'host non li inoltra al controller, display e meter restano fermi ma il plugin funziona normalmente.

### Wear, nastro, viti

- **Wear** (Off / Light / Heavy): graffi, macchie e sporco sono **generati a runtime** da un seme (intero a 32 bit salvato nello stato, uno per istanza), come elenco di segni vettoriali (`fairy_ui/wear.h`, stessi segni di The Stripper per lo stesso seme). Una sola volta all'apertura o al cambio di Wear, mai sul thread audio. Cambiare Wear cambia il livello, non il seme. Off = nessun segno e nessun nastro.
- **Nastro** (carta con bordo strappato): 0 nastri con Off, 1-2 con Light, 2-3 con Heavy, in sei posizioni libere che non coprono mai controlli, display o meter. **Un clic sul nastro apre un campo di testo** (al più **14 caratteri** ASCII stampabili; Invio o clic altrove confermano, Esc annulla). Il testo è scritto a mano con uno di **3 font** (Zeyada, Rock Salt, Homemade Apple), disegnati come tracciati vettoriali: nessun font da installare. Un nastro mai scritto resta bianco. I testi sono per posizione e vanno nello stato.
- **Viti** ai quattro angoli (taglio o croce, una può mancare con probabilità 6 %): si **ruotano trascinando** (a destra o in alto = senso orario). L'angolo si salva alla fine del gesto. Ci sono anche con Wear Off.

### Retro: Settings e Credits

La voce **BACK →** in alto a destra gira il pannello (animazione di circa 0,8 s); **← BACK** torna al fronte.

- **SETTINGS, Appearance** (nel file delle preferenze, valide per ogni progetto): WEAR Off/Light/Heavy; NOTES LAYER Hide/Show (per ora solo salvato, nessuno strato di note è disegnato); TAPE FONT Zeyada/Rock Salt/Homemade Apple; VALUES FONT Standard/Tape font (valori gialli sotto le manopole); LOOK **Re-roll** (nuovo seme: usura, nastri e viti nuovi, rotazioni delle viti a 0, nastri bianchi).
- **SETTINGS, Behaviour** (nel progetto): DISPLAY WINDOW 3/6/12 s; DISPLAY PEAKS Off/1.5/3 s; TEMPO FALLBACK 120 BPM (unico valore: tempo usato per le note Sync se l'host non manda il tempo).
- **CREDITS**: versione, GPL-3.0, componenti con autore e licenza (VST 3 SDK, VSTGUI, i tre font) e avvisi di legge.

### Dove vivono le preferenze

File `FairyTapes/PillowTalk.prefs` (righe `chiave=valore`; chiavi `wear`, `notes`, `tapeFont`, `valuesFont`; file proprio, separato da quello di The Stripper), nella cartella delle preferenze dell'utente:

| Sistema | Percorso |
|---------|----------|
| Linux | `$XDG_CONFIG_HOME/FairyTapes/PillowTalk.prefs` (se assoluto), altrimenti `~/.config/FairyTapes/PillowTalk.prefs` |
| macOS | `~/Library/Application Support/FairyTapes/PillowTalk.prefs` |
| Windows | `%APPDATA%\FairyTapes\PillowTalk.prefs` |

File mancante, vuoto, binario, oltre 64 KB o con valori non validi: si usano i default, campo per campo. La scrittura è atomica; se fallisce viene ignorata. Il file si può cancellare senza danni.

### Stato (v3)

Il progetto salva `kStateVersion = 3`: i 16 valori normalizzati dei parametri; il **decor** dell'istanza (seme, angoli delle quattro viti = rotazione dell'utente da sommare all'angolo iniziale, testi dei nastri); le impostazioni **Behaviour** (finestra del display, tenuta dei picchi, tempo di riserva). Gli stati v1 e v2 si caricano con i default per ciò che manca; un seme assente o 0 ne genera uno nuovo, salvato con il prossimo `getState`. Valori non finiti o fuori campo sono sanificati; i parametri di display non si salvano. Le modifiche al decor (viti, testi, Re-roll, Behaviour) segnano il progetto come modificato.

### Grafica: cosa c'è e come si rigenera

Nessuna immagine di sfondo: pannello, riquadri, serigrafia, controlli, usura, nastri e viti sono disegnati dal codice. L'unico asset è la tile di grana `shared/ui/assets/panel_grain.png`, copiata nel bundle (`Contents/Resources`) dopo la build; se manca, lo sfondo è a tinta unita `#161617`. Dettagli in [docs/UI_ASSETS.md](docs/UI_ASSETS.md).

I tracciati dei font del nastro stanno in `shared/ui/include/fairy_ui/tape_glyphs.h`, generato (richiede `fontTools`) da `shared/ui/tools/gen_tape_fonts.py` a partire dai TTF in `shared/ui/assets/fonts/` (sorgenti, SHA-256 e licenze nel README di quella cartella):

```bash
python3 shared/ui/tools/gen_tape_fonts.py --zeyada Zeyada.ttf --rocksalt RockSalt-Regular.ttf \
  --homemade HomemadeApple-Regular.ttf --out shared/ui/include/fairy_ui/tape_glyphs.h
```

### Strumento di render

`pillow_talk_render` disegna l'editor fuori schermo in un PNG, senza host (non è nei target di default: `cmake --build <build> --target pillow_talk_render`). Opzioni principali: `bypass`, `sync`, `meters`, `scope`, `wear=off|light|heavy`, `seed=<n>`, `text=`/`text1..6=`, `font=zeyada|rocksalt|homemade`, `screw1..4=<gradi>`, `values=standard|tape`, `window=3|6|12`, `peaks=off|1.5|3`, `back`, `tab=settings|credits`, `flip=<0..1>`, `cycles=<n>`; elenco completo in testa a `tools/render_editor.cpp`.

## Verifica manuale

- [ ] Aspetto confrontato con la preview (`docs/mockups/pillow_rack.html`).
- [ ] Aprire e chiudere l'editor 50 volte (nessun crash, nessun blocco).
- [ ] Muovere ogni controllo: l'automazione lo registra e l'undo lo annulla.
- [ ] Sync On/Off mentre si automatizzano Time e Hold: la manopola ms e le note si attenuano di conseguenza.
- [ ] Display, meter IN/OUT e LED di clip con segnale vero e con sidechain; un clic su un LED di clip lo azzera.
- [ ] Wear Off / Light / Heavy; scrivere sul nastro; ruotare le viti; flip del retro e Settings che persistono.
- [ ] HiDPI (Windows/Linux a 150-200 %): dimensione = 1000 x 628 per il fattore, nitida; spostando la finestra su uno schermo con scala diversa si ridimensiona.
- [ ] I pulsanti nota attenuati si possono cliccare (è voluto).
- [ ] Linux: embedding solo X11; con Wayland nativo l'editor viene rifiutato.

## Status

- [x] Parameters, versioned state (v3)
- [x] Vocal-aware ducker, tape echo, sidechain bus
- [x] VSTGUI rack editor (1000 x 628): ducking display, LEVEL meters and clip LEDs, Wear, tape handwriting, screws, back panel (Settings, Credits)
- [ ] Manual check in Reaper and Cubase (see Verifica manuale)
