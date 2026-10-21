# Pillow Talk — Grafica dell'editor

L'editor (**1000 x 628** logici, host-scalato) è **interamente disegnato da codice**: non c'è skin PNG, né sfondo né immagini dei controlli. Il vecchio meccanismo di skin opzionale (`panel.png`, `knob_body.png`, `button_on/off.png`) e la finestra 480 x 560 non esistono più nell'editor di Pillow Talk.

Riferimento visivo: `docs/mockups/pillow_rack.html`. Linguaggio comune della suite: `docs/DESIGN_SYSTEM.md`. Tutto il testo visibile è in inglese.

## Livelli, dal fondo

1. `PanelView` (`src/ui/panel_view.{h,cpp}`): tinta `#161617`, grana, riquadri con bordo e titolo sul bordo inferiore (`drawTitledBox`), testi fissi e righe, da `src/ui/layout.h`.
2. `SilkscreenView` (`fairy_ui/silkscreen_view.h`): serigrafia dei controlli (sigle, scale, valori), guidata dalla tabella di `layout.h`.
3. `WearView` (`fairy_ui/wear_view.h`): usura vettoriale; lascia passare i clic.
4. Nastri (`src/ui/tape_view.{h,cpp}`).
5. Viti (`ScrewView`, `fairy_ui/screw_view.h`).
6. Controlli (`RackKnob`, `RackToggle`, `RackChoice`), display del ducking (`DuckScope`), meter (`MeterBar`) e LED di clip (`ClipLed`).
7. Retro e animazione di flip (`BackView`, `FlipView`/`PanelFlip`), sopra al resto.

Posizioni e dimensioni (riquadri, controlli, meter, LED, testi, nastri e viti) stanno in `src/ui/layout.h`, con test (`pillow_talk_layout_tests`): un controllo per parametro, tutto dentro il pannello, niente sovrapposizioni.

## Asset

Un solo file: `shared/ui/assets/panel_grain.png`, la tile di grana del pannello (la stessa di The Stripper), copiata da un passo `POST_BUILD` in `PillowTalk.vst3/Contents/Resources/`. Se manca o non si carica, lo sfondo è a tinta unita `#161617` e l'editor si apre comunque. Il bundle viene riscritto a ogni build.

I font del nastro non sono file: i tracciati vettoriali dei glifi sono nell'header generato `shared/ui/include/fairy_ui/tape_glyphs.h`. Sorgenti, SHA-256 e licenze: `shared/ui/assets/fonts/README.md`. Rigenerare (serve `fontTools`):

```bash
python3 shared/ui/tools/gen_tape_fonts.py --zeyada Zeyada.ttf --rocksalt RockSalt-Regular.ttf \
  --homemade HomemadeApple-Regular.ttf --out shared/ui/include/fairy_ui/tape_glyphs.h
python3 -m pytest shared/ui/tools/test_gen_tape_fonts.py
```

## Usura generata a runtime

Nessuna immagine di usura. Graffi, macchie e sporco sono un elenco di segni vettoriali prodotto da `generateWear(seme, livello)` (`fairy_ui/wear.h`, lo stesso generatore di The Stripper: stesso seme, stessi segni), una volta all'apertura dell'editor o al cambio di Wear, mai sul thread audio. Il seme (32 bit) è salvato nello stato; Wear (Off / Light / Heavy) è una preferenza dell'utente. Posizione dei nastri (0 con Off, 1-2 Light, 2-3 Heavy) e viti (quattro agli angoli, una può mancare) vengono dallo stesso seme (`src/ui/decor.{h,cpp}`, `layoutFromSeed`; viti da `fairy_ui/screws.h`).

## HiDPI e vincoli

- Dimensione logica fissa 1000 x 628: `checkSizeConstraint` forza questa dimensione per il fattore di scala. Su Windows e Linux l'host passa il fattore (`setContentScaleFactor`) e il frame VSTGUI si ingrandisce; su macOS la scala Retina è del sistema. Il disegno da codice resta nitido a ogni scala.
- I controlli attenuati sono disegnati con opacità ridotta e restano operativi.
- Testi e posizioni sono nel codice (`layout.h`, `editor.cpp`, `panel_view.cpp`).

## Verifica senza host

`pillow_talk_render` (target `EXCLUDE_FROM_ALL`, `tools/render_editor.cpp`) disegna l'editor in un PNG con le stesse viste dell'host; opzioni in testa al sorgente e nel README del plugin.
