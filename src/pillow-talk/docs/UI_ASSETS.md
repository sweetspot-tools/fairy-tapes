# Pillow Talk — Asset grafici dell'editor (skin)

L'editor (480 x 560) disegna tutto da codice. Le immagini sono **opzionali**: se un file manca, l'elemento si disegna con il vettoriale di fallback. Il plugin funziona identico con o senza asset.

## Cartella

`Contents/Resources` del bundle (`PillowTalk.vst3/Contents/Resources/`). VSTGUI cerca i file lì, per nome, tramite `Skin` (`shared/ui`).

**Stato della build:** il CMake di Pillow Talk **non copia ancora nessun asset** nel bundle e non crea la cartella `Resources`. Per provare un'immagine oggi si copia a mano il file in `dist/PillowTalk.vst3/Contents/Resources/`. Il passo di copia dalla sorgente degli asset verso il bundle va aggiunto al CMake quando arrivano le grafiche definitive (posto previsto: una cartella di sorgenti degli asset in `src/pillow-talk/`, copiata in `Contents/Resources` dopo la build). Il bundle viene riscritto a ogni build: i file copiati a mano possono andare persi.

## File riconosciuti oggi

Nomi in `shared/ui/include/fairy_ui/skin.h` (`SkinAsset`). Solo PNG.

| File | Dove si usa | Dimensione |
|------|-------------|------------|
| `panel.png` | Sfondo dell'intero editor, al posto del corpo e del bordo disegnati | 480 x 560 px (1x) |
| `knob_body.png` | Corpo di ogni manopola, al posto del cerchio vettoriale | cerchio di 34 x 34 px (raggio 17), disegnato nel rettangolo del corpo |
| `button_on.png` | Pulsanti attivi (Sync, Sidechain, Bypass, nota selezionata) | disegnata nel rettangolo del pulsante (stessa immagine per tutti, quindi stirata) |
| `button_off.png` | Pulsanti non attivi | come sopra |

Il codice disegna comunque sopra l'immagine: arco di valore e indicatore sulle manopole, etichette di manopole e pulsanti, titolo, testi di sezione e display. Il display del ducking non ha asset. I pulsanti delle note hanno dimensioni diverse da Sync, Sidechain e Bypass: una sola immagine on/off per tutti viene scalata e non è il formato definitivo.

## Fallback

Senza il file l'elemento torna al disegno da codice, con i colori del mockup approvato:

- pannello `#2a2926` con bordo `#4a463e`; testo `#e8e2d4`, secondario `#9a9384`;
- corpo manopola e pulsante spento `#171614`, bordo `#6b675e`;
- ambra (arco, pulsante attivo) `#d9a441`, testo sul pulsante attivo `#2a1d05`;
- display `#0f0e0d`.

La cache (anche dei file mancanti) vive quanto l'oggetto `Editor` (`Skin skin_` è un suo membro) e `Editor::close()` non la svuota. Chiudere e riaprire l'editor basta se l'host distrugge la vista alla chiusura (lo fanno la maggior parte degli host); se l'host riusa lo stesso oggetto, bisogna ricaricare il plugin o il progetto.

## Sostituire un elemento alla volta

1. Copiare un solo file (per esempio `panel.png`) in `Contents/Resources`.
2. Chiudere e riaprire l'editor (o ricaricare il plugin/progetto, vedi sopra): quell'elemento usa l'immagine, gli altri restano vettoriali.
3. Per tornare al disegno da codice si rimuove il file.

## Formati non ancora definiti

I formati delle **filmstrip delle manopole** (numero di fotogrammi, dimensione, orientamento) e delle **immagini dei pulsanti** (stati, dimensioni per pulsante, stato attenuato) **non sono fissati**: si decidono insieme alle grafiche reali. Oggi `knob_body.png` è solo un corpo statico (l'arco e l'indicatore restano da codice) e non è una filmstrip. I nomi e le dimensioni in questa pagina possono cambiare.

## Vincoli visivi

- Dimensione logica fissa 480 x 560: l'utente non può ridimensionare l'editor (`checkSizeConstraint` forza 480 x 560 per il fattore di scala).
- HiDPI: su Windows e Linux l'host passa il fattore di scala (`setContentScaleFactor`) e l'editor fa lo zoom del frame VSTGUI (finestra 480 x 560 per il fattore); su macOS la scala Retina è del sistema. Il disegno da codice resta nitido, un PNG a 1x viene ingrandito e può risultare sfocato. Il supporto a file `@2x` non è definito.
- I controlli attenuati (Sync che spegne il ramo non in uso) sono disegnati con il 40 % di opacità sopra l'immagine; restano operativi.
- Testi e posizioni dei controlli sono fissi nel codice (`src/pillow-talk/src/ui/editor.cpp`, `panel_view.cpp`): un pannello nuovo deve lasciare libere le stesse zone.
