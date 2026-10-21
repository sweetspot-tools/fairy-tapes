# Happy Ending — Design

**Data:** 2026-10-21
**Stato:** bozza per revisione (brainstorming dal 2026-10-15)
**Suite:** Fairy Tapes (catena di mastering "tutto dentro")
**Nome:** Happy Ending (deciso il 2026-10-21); cartella `src/happy-ending/`, bundle `HappyEnding.vst3`
**Preview:** `src/happy-ending/docs/mockups/happy_ending_rack.html` (anche nella pagina `docs/mockups/index.html`)

## 1. Scopo

Catena di mastering da mettere sul master bus nella fase finale di chiusura di un progetto. Obiettivo della suite: approccio da console, analogico, immediato e naturale; il mastering però è una fase delicata e richiede comandi fini. Il compromesso: comandi **a scatti** (valori richiamabili esattamente), range stretti, valori di partenza già adatti a un master.

Successo: su un mix finito, con pochi gesti (un ritocco di EQ, un po' di colla, la spinta finale) si ottiene un master pulito e forte, leggendo LUFS e gain reduction in un punto solo, senza poter lasciare acceso per sbaglio un ascolto o una taratura nel render.

## 2. Catena e decisioni generali

- Catena fissa: **INPUT → FINE EQ → VIBE EQ → GLUE → CLIP → LIMIT → FINAL → METALHEAD → OUTPUT**.
- Ogni stadio ha il suo **IN** (acceso di default, tranne METALHEAD) e c'è un **BYPASS** globale.
- **Nessuna rilevazione true peak** (scelta dell'utente): i limiter lavorano sul picco del campione; per lo streaming si usa il ceiling −1,0 dB come margine.
- Comandi a scatti dappertutto; doppio clic = valore di default.
- Licenza del plugin GPL-3.0 (componenti Dusk Audio e ZLInflator sono GPL-3.0).

## 3. Riga di servizio

- **INPUT:** GAIN ±12 dB a passi di 0,5.
- **LEVEL:**
  - meter di picco IN e OUT stereo (stessa scala e colori di The Stripper);
  - blocco **GR** con cinque righe ambra, 0-12 dB, rosse in cima: **G** glue, **C** clip, **L** limit, **F** final, **M** metalhead; ogni riga si attenua con l'IN del suo stadio e con il BYPASS;
  - **LED di clip per modulo**, in ordine di segnale: IN, FEQ, VEQ, GLU, CLP, LIM, FIN, MTL, OUT (sigle provvisorie); un clic su uno li azzera tutti; quelli degli stadi si attenuano con il BYPASS e con il loro IN;
  - lettura **LUFS** short-term e integrata (clic per azzerare l'integrata).
- **OUTPUT:** GAIN ±12 dB, **BYPASS** (attenua e blocca tutto tranne meter e LUFS; uscita = ingresso), **CEIL**: ceiling d'uscita unico −0,1 / −0,3 / −0,5 / −1,0 dB (default −0,3), selettore a clic con LED giallo e valore scritto sotto. Vale per LIMIT, FINAL (clip e inflate) e METALHEAD.

## 4. Schermo condiviso

Un solo schermo in un riquadro senza titolo, sopra il FINE EQ, con a destra una colonna di pulsanti a faccia accesa **EQ · GLUE · CLIP · LIMIT · FINAL**:

- **EQ:** curve del FINE EQ (una curva STEREO, oppure MID e SIDE appena una banda lavora in M/S) e curva del VIBE EQ in ambra (tratteggiata l'impostazione statica quando il DYN ritira un boost); durante LISTEN la zona ascoltata.
- **GLUE / LIMIT:** livello, gain reduction e soglia (GLUE, più la riduzione netta dopo il MIX) o ceiling (LIMIT), che scorrono.
- **CLIP / FINAL:** curva di trasferimento con il punto che segue i picchi (FINAL con la linea del ceiling).

Gli schermi propri di VIBE, GLUE, CLIP, LIMIT e FINAL sono stati tolti: le loro informazioni sono qui o nel LEVEL.

## 5. Stadi

### 5.1 FINE EQ

- **DSP:** port senza JUCE del Multi-Q di Dusk Audio (struttura delle bande, Q-coupling); coefficienti **matched** (Vicanek) al posto dell'oversampling: **latenza zero**, curve corrette fino a Nyquist, fase minima, nessuna saturazione.
- **Bande (tutte a scatti):**
  - HIGH PASS 12 dB/ottava: Off, 20, 25, 30, 40 Hz; quando lavora sul SIDE la scala diventa Off, 60, 80, 120, 160, 200 Hz (mono-bass);
  - LOW SHELF 30, 50, 70, 100, 150, 200, 300 Hz;
  - LOW MID 100-800 Hz, MID 400 Hz-3,15 kHz, HIGH MID 1,6-12,5 kHz (dieci posizioni a terzi d'ottava ciascuna);
  - HIGH SHELF 3, 5, 8, 10, 12, 16, 20 kHz;
  - guadagno ±4 dB a passi di 0,5; Q delle campane Wide / Medium / Narrow / Surgical (0,5 / 1 / 2 / 5) con **Q-coupling** (più guadagno, campana più stretta).
- **Per banda:** IN; **M/S** (ST, MID, SIDE; niente L/R), selettore a clic con valore sotto; **LISTEN** sulle tre campane.
- **LISTEN:** esclusivo (una banda sola); le altre bande si attenuano e si bloccano (IN compreso); **si spegne alla chiusura dell'editor e non si salva** nello stato.
- Bande spente o escluse: comandi attenuati e non cliccabili; resta vivo solo il loro IN.
- Layout: intestazione a tratto con il nome della banda (come l'EQ di The Stripper).

### 5.2 VIBE EQ

- **DSP:** port senza JUCE del `PultecProcessor` di Dusk Audio (rete LC passiva, non linearità dell'induttore, stadio 12AX7).
- **LOW:** frequenza 20 / 30 / 60 / 100 Hz e quantità 0-10: un'unica manopola fa boost e taglio subito sopra nella proporzione classica ("Pultec trick").
- **AIR:** frequenza 5 / 8 / 10 / 12 / 16 kHz e quantità 0-10, banda larga.
- **TILT:** ±3 dB a passi di 0,5 (DARK…BRIGHT): shelf basso e alto in direzioni opposte, nello stesso circuito.
- **TUBE:** 0-10, default 2: lo stadio valvolare dopo tutta la rete, cioè l'uscita dello stadio (colora di più dove l'EQ alza).
- **DYN su LOW e su AIR:** il boost si ritira sul programma forte (soglia e tempi fissi, da tarare a orecchio). Pulsantino sulla riga d'intestazione del gruppo, a faccia accesa (eccezione come la campana di The Stripper), con il LED **ACT** ambra accanto, acceso quanto il boost si sta ritirando.
- Layout: gruppi LOW e AIR con intestazione a tratto, FREQ in alto e quantità in diagonale; TILT e TUBE a destra.

### 5.3 GLUE

- **DSP:** `MultiCompBus` (stile SSL G, da Multi-Comp di Dusk Audio), già portato senza JUCE in The Stripper; in parallelo.
- **Comandi:** THRESH −20…0 dB (1 dB), ATTACK 0,1 / 0,3 / 1 / 3 / 10 / 30 ms, RELEASE 0,1 / 0,3 / 0,6 / 1,2 s / Auto, MAKEUP 0…+6 dB (0,5), **MIX** 0-100 % a passi di 5 (default 50 %); selettori a clic **RATIO** 1,5 / 2 / 4 (default 2:1) e **SC HPF** Off / 60 / 90 / 150 Hz (default 60); **WARM** (attacco ammorbidito); IN.
- Provvisorio: da rivedere alla fine con tutta la catena davanti.

### 5.4 CLIP

- **DSP:** `ClipSoftly` di Airwindows (MIT), soft clipper senza grana digitale.
- **DRIVE** 0…+6 dB a passi di 0,5, a **livello compensato** (le parti piano passano invariate, i picchi si arrotondano); IN.

### 5.5 LIMIT

- **DSP:** limiter nostro con look-ahead sul picco del campione; la soglia fa anche da recupero di guadagno come negli L2 (i picchi arrivano sul ceiling d'uscita). Obiettivo: 2-3 dB di riduzione.
- **THRESH** 0…−4 dB a passi di 0,5.
- **Stili** (scelta esclusiva, faccia accesa, in colonna in basso a destra): **MODERN** (look-ahead pieno, circa 1,5 ms; release veloce e fissa; ginocchio duro), **PUNCH** (look-ahead corto, circa 0,3 ms, lascia passare il primo istante del transiente; release media; duro), **DYN** (medio; release lenta e dipendente dal programma; ginocchio morbido).
- **RELEASE** è un ritocco attorno alla release dello stile: tacca centrale = come vuole lo stile, 4 scatti per parte (un terzo d'ottava ciascuno), da FAST a SLOW; il valore mostra i ms reali. Basi provvisorie: MODERN 50 ms, PUNCH 100 ms, DYN 250 ms.
- **SET** (taratura guidata della release): la soglia scende in modo visibile fino a −8 dB (oltre la scala: il puntatore resta a fondo corsa, il valore mostra quella vera), tutto il blocco si attenua e si blocca tranne RELEASE, compare "SET THE RELEASE"; ripremendo SET la soglia torna com'era. **Si spegne alla chiusura dell'editor e non si salva.**

### 5.6 FINAL

- **INFLATE** (port di ZLInflator in `shared/inflator`, GPL-3.0, già con test): EFFECT 0-100 % (passi di 10), CURVE 0 / 25 / 50 / 75 / 100 %.
- **CLIP** (`ClipOnly2` di Airwindows, MIT): DRIVE 0…+2 dB a passi di 0,5 contro il ceiling d'uscita.
- Scelta esclusiva INFLATE / CLIP in colonna sotto l'IN; **si vedono solo i comandi del modo scelto**; ognuno conserva i suoi valori. Entrambi i modi restano sotto il ceiling.

### 5.7 METALHEAD

- Stadio facoltativo, **spento di default**: limiter digitale brickwall dopo il FINAL, per la loudness estrema. Spento: riquadro attenuato e bloccato tranne l'IN.
- **PUSH** 0…+6 dB contro il ceiling d'uscita. Serigrafia "SHAME ON YOU" nell'angolo, con il carattere delle sigle.

## 6. Automatismi

- **Macro LOUDNESS (decisa 2026-10-15):** una manopola distribuisce la spinta tra CLIP, LIMIT e FINAL tenendo ciascuno nella sua zona buona (circa 1,5 dB di clip, 2-3 dB di limiter, fino a +2 del FINAL). Come si combina con i comandi singoli è aperto (§11).
- **Target LUFS con MATCH (deciso 2026-10-15):** scelta del target (−14, −11, −9, −7 LUFS); il pannello indica lo scarto; il tasto MATCH, premuto sulla parte più forte del brano, porta una sola volta la LOUDNESS al valore che centra il target, poi resta ferma.
- Scartati: livello compensato in ogni stadio (MAKEUP resta nel GLUE), LEARN sull'INPUT.

## 7. Grafica

- Regole comuni di `docs/DESIGN_SYSTEM.md`: titoli dei riquadri sul bordo basso, IN nell'angolo in alto a destra, selettori di modo in basso a destra, sigla del motore esterno in alto a sinistra, tre tipi di pulsante (scelte esclusive a faccia accesa ambra; acceso/spento con LED rosso; selettori a ciclo con LED giallo e valore sotto), retro con il template di The Stripper.
- **Sigle dei blocchi (provvisorie):** FINE EQ `MULTI-Q`, VIBE EQ `TUBE-1A`, GLUE `4K-G`, CLIP `C-SOFTLY`, FINAL `ZL-INFL` / `C-ONLY2`; LIMIT e METALHEAD sono motori nostri e non hanno sigla.
- **Pannello attuale:** 1000 × 1256 (righe: servizio; schermo; FINE EQ; VIBE EQ e GLUE affiancati; CLIP, LIMIT, FINAL e METALHEAD affiancati). Più alto degli altri plugin: vedi §11.
- **Retro:** SETTINGS (Appearance comune; Behaviour per ora vuota), BUTTONS (spiega BYPASS, IN, M/S, LISTEN, DYN, viste, RATIO/SC HPF, WARM, stili, SET, CEILING, INFLATE/CLIP, METALHEAD), CREDITS.
- Testo dell'editor in inglese.

## 8. Componenti e licenze

| Componente | Uso | Licenza |
|---|---|---|
| Multi-Q (Dusk Audio) | FINE EQ, VIBE EQ (`PultecProcessor`) | GPL-3.0 |
| Multi-Comp (Dusk Audio) | GLUE (`MultiCompBus`) | GPL-3.0 |
| Airwindows (Chris Johnson) | CLIP (`ClipSoftly`), FINAL (`ClipOnly2`) | MIT |
| ZLInflator (ZL Audio, zsliu98) | FINAL inflate (`shared/inflator`) | GPL-3.0 |
| VST 3 SDK, VSTGUI (Steinberg) | plugin ed editor | MIT, BSD-3-Clause |
| Font del nastro | come gli altri plugin | OFL-1.1, Apache-2.0 |

Licenze da ricontrollare sui file `LICENSE` prima del rilascio. ZL Equalizer è stato escluso (AGPL-3.0).

## 9. Stato, latenza, robustezza

- **Non salvati nello stato:** LISTEN e SET (sempre spenti all'apertura del progetto e alla chiusura dell'editor).
- **Latenza:** FINE EQ, VIBE, GLUE, CLIP a latenza zero; il look-ahead del LIMIT (e del METALHEAD, se lo usa) va dichiarato all'host e resta costante cambiando stile.
- Parametri con ID fissati da subito e tabella congelata come The Stripper; ordine e default nel piano.

## 10. Test

Scritti prima del codice, devono prima fallire:
- curve del FINE EQ contro i prototipi analogici (guadagno al centro, fianchi, nessuna distorsione della curva vicino a Nyquist), Q-coupling, scala del passa-alto sul SIDE, M/S esatto (codifica e decodifica senza perdite), LISTEN esclusivo e non salvato;
- VIBE: trucco LOW, TILT simmetrico, DYN che ritira il boost oltre soglia;
- GLUE: riduzione e MIX parallelo;
- CLIP: livello compensato (piccoli segnali a guadagno unitario);
- LIMIT: nessun campione oltre il ceiling, latenza dichiarata, stili e ritocco della release, SET che non entra nello stato;
- FINAL: inflate (test già esistenti in `shared/inflator`) e clip sotto il ceiling;
- LUFS contro valori di riferimento; BYPASS = uscita identica all'ingresso.

## 11. Decisioni aperte

- **Principio a due livelli** (davanti i comandi musicali, il dettaglio a un clic) e layout "panoramica più dettaglio": proposto, non deciso; oggi tutti i comandi stanno sul pannello.
- **Macro LOUDNESS e comandi singoli:** lo stile decide anche la distribuzione / una manopola CHARACTER tra clip e limiter / ritocchi ± nel dettaglio di ogni stadio. Da decidere anche dove sta sul pannello, insieme a target LUFS e MATCH (non ancora nella preview).
- **Dimensioni del pannello:** 1000 × 1256 non sta su uno schermo 1080p a 1:1; da decidere tra scala circa 80 %, disposizione orizzontale (come gli altri plugin) o ulteriori compattazioni.
- **Oversampling** per CLIP, FINAL e TUBE (aliasing delle armoniche): da decidere.
- **Valori da tarare a orecchio:** soglie e tempi del DYN del VIBE, basi di release degli stili, soglia forzata di SET.
- **Revisione finale** degli stadi con tutta la catena davanti, in particolare FINE EQ (l'utente vuole tornarci) e GLUE.
- Sigle dei blocchi e dei LED di clip, conferma del nome METALHEAD.

## 12. Storia del brainstorming

Punto 0 (2026-10-15) e riepilogo: `.superpowers/brainstorm/mastering/` (fuori da git). Le scelte successive sono in questa spec.
