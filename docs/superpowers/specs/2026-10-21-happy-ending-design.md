# Happy Ending — Design

**Data:** 2026-10-21
**Stato:** decisioni chiuse il 2026-10-24, in revisione prima del piano (brainstorming dal 2026-10-15)
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
  - **LED di clip per modulo**, in ordine di segnale: IN, FEQ, VEQ, GLU, CLP, LIM, FIN, MTL, OUT (sigle confermate, §7); un clic su uno li azzera tutti; quelli degli stadi si attenuano con il BYPASS e con il loro IN;
  - lettura **LUFS** su uno schermo piccolo accanto ai LED: short-term, integrata (clic per azzerare) e **TO TARGET** (scarto dal target, LED verde entro ±0,5 LU); la riga dei LED di clip continua sotto lo schermo.
- **LOUDNESS** (blocco tra LEVEL e OUTPUT, deciso 2026-10-21): manopola LOUDNESS 0-10 a passi di 0,5; **MATCH** (azione, in alto a destra); **TARGET** −14 / −11 / −9 / −7 LUFS (selettore a ciclo, in basso a destra). Sta tra LEVEL e OUTPUT perché legge i LUFS accanto e agisce sugli ultimi stadi prima dell'uscita. INPUT e OUTPUT sono più stretti per farle posto.
- **OUTPUT:** GAIN ±12 dB, **BYPASS** (attenua e blocca tutto tranne meter e LUFS; uscita = ingresso), **CEIL**: ceiling d'uscita unico −0,1 / −0,3 / −0,5 / −1,0 dB (default −0,3), selettore a clic con LED giallo e valore scritto sotto. Vale per LIMIT, FINAL (clip e inflate) e METALHEAD.

## 4. Schermo condiviso

Un solo schermo in un riquadro senza titolo, accanto al FINE EQ, con a destra una colonna di pulsanti a faccia accesa **EQ · GLUE · CLIP · LIMIT · FINAL**:

- **EQ:** curve del FINE EQ (una curva STEREO, oppure MID e SIDE appena una banda lavora in M/S) e curva del VIBE EQ in ambra (tratteggiata l'impostazione statica quando il DYN ritira un boost); durante LISTEN la zona ascoltata.
- **GLUE / LIMIT:** livello, gain reduction e soglia (GLUE, più la riduzione netta dopo il MIX) o ceiling (LIMIT), che scorrono.
- **CLIP / FINAL:** curva di trasferimento con il punto che segue i picchi (FINAL con la linea del ceiling).

Gli schermi propri di VIBE, GLUE, CLIP, LIMIT e FINAL sono stati tolti: le loro informazioni sono qui o nel LEVEL.

## 5. Stadi

### 5.1 FINE EQ

- **DSP:** port senza JUCE del Multi-Q di Dusk Audio (struttura delle bande, Q-coupling); coefficienti **matched** (Vicanek) al posto dell'oversampling: **latenza zero**, curve corrette fino a Nyquist, fase minima, nessuna saturazione.
- **Bande (tutte a scatti):**
  - HIGH PASS 12 dB/ottava: Off, 20, 25, 30, 35, 40 Hz (sei posizioni, come la scala del SIDE); quando lavora sul SIDE la scala diventa Off, 60, 80, 120, 160, 200 Hz (mono-bass);
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
- Parametri congelati il 2026-10-24 (servono per fissare gli ID); il suono si rivede a orecchio con tutta la catena nel compito di taratura (§9).

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

- Nome confermato il 2026-10-24. Stadio facoltativo, **spento di default**: limiter digitale brickwall dopo il FINAL, per la loudness estrema. Spento: riquadro attenuato e bloccato tranne l'IN.
- **PUSH** 0…+6 dB contro il ceiling d'uscita. Serigrafia "SHAME ON YOU" nell'angolo, con il carattere delle sigle.

## 6. Automatismi

- **Macro LOUDNESS (decisa 2026-10-15; rapporto con i comandi deciso 2026-10-24):** una manopola aggiunge spinta **sopra** le regolazioni degli stadi, con una curva fissa: prima CLIP (fino a +1,5 dB di drive), poi LIMIT (soglia fino a −3 dB, cioè 2-3 dB di riduzione), infine FINAL (EFFECT o DRIVE in più, sempre sotto il ceiling). Le manopole degli stadi non si muovono e fissano il punto di partenza; a LOUDNESS 0 il suono è esattamente quello delle regolazioni. Quanto lavora ogni stadio si legge nelle righe GR. Distribuzione legata allo stile o manopola CHARACTER: scartate per ora, si possono aggiungere sopra questo schema.
- **Target LUFS con MATCH (deciso 2026-10-15):** scelta del target (−14, −11, −9, −7 LUFS); il pannello indica lo scarto; il tasto MATCH, premuto sulla parte più forte del brano, porta una sola volta la LOUDNESS al valore che centra il target, poi resta ferma.
- Scartati: livello compensato in ogni stadio (MAKEUP resta nel GLUE), LEARN sull'INPUT.

## 7. Grafica

- Regole comuni di `docs/DESIGN_SYSTEM.md`: titoli dei riquadri sul bordo basso, IN nell'angolo in alto a destra, selettori di modo in basso a destra, sigla del motore esterno in alto a sinistra, tre tipi di pulsante (scelte esclusive a faccia accesa ambra; acceso/spento con LED rosso; selettori a ciclo con LED giallo e valore sotto), retro con il template di The Stripper.
- **Sigle dei blocchi (confermate il 2026-10-24):** FINE EQ `MULTI-Q`, VIBE EQ `TUBE-1A`, GLUE `4K-G`, CLIP `C-SOFTLY`, FINAL `INFL-Z` / `C-ONLY2`; LIMIT e METALHEAD sono motori nostri e non hanno sigla. Sigle dei LED di clip confermate: IN, FEQ, VEQ, GLU, CLP, LIM, FIN, MTL, OUT.
- **Pannello (deciso 2026-10-24):** **1280 × 890**, su quattro righe: servizio (INPUT, LEVEL, LOUDNESS, OUTPUT); FINE EQ con lo schermo delle curve accanto (viste EQ, GLUE, CLIP, LIMIT, FINAL); VIBE EQ e GLUE affiancati; CLIP, LIMIT, FINAL e METALHEAD affiancati su tutta la larghezza. Compattato dall'utente riga per riga (prima 1000 × 1256, poi 1280 × 1036, 934). Su 1080p al 100 % sta con circa 100 px di margine; per gli schermi piccoli c'è **SIZE** nel retro (75 / 90 / 100 / 125 %, comune a tutti i plugin, `docs/DESIGN_SYSTEM.md`).
- **Tutti i comandi sul pannello (deciso 2026-10-24):** il principio a due livelli (davanti i comandi musicali, il resto in un dettaglio a un clic) è stato provato in uno schizzo (1280 × 772) e scartato: nasconde le frequenze del FINE EQ, toglie la vista d'insieme della catena e guadagna meno di SIZE.
- **FINE EQ:** bande su una fila come in The Stripper, con le manopole alternate su due quote per incastrare le bande vicine: HI PASS una manopola; LOW SHELF e HI SHELF con FREQ e GAIN in diagonale (LOW SHELF: GAIN in alto, FREQ in basso; HI SHELF al contrario); LO MID e HI MID ribaltate (Q in alto al centro, FREQ e GAIN sotto); MID con FREQ e GAIN in alto e Q sotto. Etichette abbreviate: HI PASS, LOW SHELF, LO MID, MID, HI MID, HI SHELF.
- **GLUE:** IN da solo in alto a destra; WARM, RATIO e SC HPF in fila in basso a destra, a partire dalla colonna dell'IN.
- **LIMIT:** THRESH e RELEASE a sinistra, SET sulla diagonale in alto a destra di RELEASE, IN in alto a destra, stili MODERN / PUNCH / DYN in colonna tra SET e IN, centrati anche in altezza. **FINAL:** EFFECT e CURVE (o DRIVE) a sinistra, INFLATE / CLIP tra le manopole e IN, centrati in altezza. **CLIP:** la scritta "level matched" sta sotto l'IN, su due righe.
- **Valori, usura e viti come negli altri plugin:** valori in TAPE FONT (selezionabile dal retro), graffi, macchie, residui, nastro scrivibile e viti girevoli da un seme per istanza; usura sopra serigrafia e valori, sotto manopole, pulsanti e LED.
- **Retro:** SETTINGS (Appearance comune, con SIZE; Behaviour con **OVERSAMPLING**, §9), BUTTONS (spiega BYPASS, IN, M/S, LISTEN, DYN, viste, RATIO/SC HPF, WARM, stili, SET, CEILING, INFLATE/CLIP, METALHEAD), CREDITS.
- **Scheda ROUTING (regola comune del 2026-10-23, fatta nella preview):** schema a blocchi fisso nello stile dei manuali dell'outboard (simboli del `DESIGN_SYSTEM.md`), link tra BUTTONS e CREDITS. Riga 1: INPUT → FINE EQ (codifica M/S, percorsi M e S, decodifica) → VIBE EQ (rilevatore DYN tratteggiato su LOW e AIR) → GLUE in parallelo (percorso DRY, nodo MIX, SC HPF sul rilevatore); riga 2: CLIP → LIMIT → FINAL (INFLATE o CLIP in parallelo con selettore) → METALHEAD (IN disegnato spento, come di default) → OUTPUT → BYPASS globale. Linee di controllo tratteggiate: CEILING verso LIMIT, FINAL e METALHEAD; macro LOUDNESS verso CLIP, LIMIT e FINAL; MATCH dal meter LUFS alla LOUDNESS. Punti di misura: meter IN e OUT, righe GR e LED di clip di ogni stadio.
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
- **Latenza:** FINE EQ e GLUE a latenza zero; il look-ahead del LIMIT (e del METALHEAD, se lo usa) va dichiarato all'host e resta costante cambiando stile.
- **Oversampling (deciso 2026-10-24):** impostazione **OVERSAMPLING** in BEHAVIOUR (salvata nel progetto): Off / 2× / 4×, default 2×; vale solo per gli stadi non lineari (CLIP, TUBE del VIBE EQ, FINAL, METALHEAD). Filtri a fase lineare; la latenza aggiunta si somma a quella del LIMIT, è dichiarata all'host e cambia solo cambiando l'impostazione.
- **Valori da tarare a orecchio (deciso 2026-10-24):** soglie e tempi del DYN del VIBE, basi di release degli stili, soglia forzata di SET, curva della LOUDNESS. Si parte dai valori della preview, raccolti in un'unica tabella nel codice; il piano ha un compito di taratura con una build di prova, che comprende anche l'ascolto finale di FINE EQ e GLUE con tutta la catena.
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

Nessuna decisione di progetto aperta (chiuse il 2026-10-24: LOUDNESS sopra i comandi §6, oversampling §9, valori da tarare §9, parametri di FINE EQ e GLUE congelati §5, sigle §7, nome METALHEAD §5.7, dimensioni del pannello e principio a due livelli §7). Restano compiti di implementazione: taratura a orecchio (§9) e verifica delle licenze sui file `LICENSE` prima del rilascio (§8).

## 12. Storia del brainstorming

Punto 0 (2026-10-15) e riepilogo: `.superpowers/brainstorm/mastering/` (fuori da git). Le scelte successive sono in questa spec.
