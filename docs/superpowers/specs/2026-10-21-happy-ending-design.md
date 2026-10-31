# Happy Ending — Design

**Data:** 2026-10-21
**Stato:** decisioni chiuse il 2026-10-24 (brainstorming dal 2026-10-15); motore audio e plugin VST3 realizzati
**Avanzamento:** motore audio e plugin VST3 fatti (Task 1-13 del piano `docs/superpowers/plans/2026-10-24-happy-ending.md`); taratura a orecchio in attesa dell'utente (Task 14); editor: piano `docs/superpowers/plans/2026-10-31-happy-ending-editor.md` scritto il 2026-10-31, da eseguire.
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
- **Ceiling (deciso 2026-10-24):** con LIMIT, FINAL o METALHEAD acceso nessun campione d'uscita supera il CEIL, **OUTPUT compreso**. Il filtro di decimazione della zona sovracampionata può ricostruire campioni fino a circa +0,4 dB sopra il ceiling tenuto alla frequenza sovracampionata, e OUTPUT > 0 dB li alza: dopo il guadagno OUTPUT c'è un **limiter di sicurezza** alla frequenza base (deciso dall'utente il 2026-10-31 al posto del solo clip duro): soglia = CEIL, nessun recupero di guadagno, look-ahead di circa 1 ms, release di circa 50 ms, così raccoglie la risalita della decimazione e l'OUTPUT sopra 0 dB senza tagliare di netto; dietro resta un guardiano a clip duro come ultima rete, che non deve scattare sul programma normale. Non è un maximizer: non alza il volume. Con i tre stadi spenti il guardiano è spento e OUTPUT agisce liberamente; il BYPASS, che sta dopo, resta l'ingresso esatto.
- **LISTEN bloccato a banda o stadio spenti (deciso 2026-10-24):** se la banda scelta o l'intero FINE EQ è spento, LISTEN vale spento (nessun ascolto da una banda che non lavora).
- **BYPASS:** l'uscita si dissolve in 64 campioni verso l'ingresso ritardato della latenza dichiarata (un'unica linea di ritardo) ed è esatta al campione a regime; la catena continua a girare, quindi meter, LUFS e LED restano vivi.
- Ingressi non finiti (NaN, Inf) sono azzerati all'ingresso; i denormali sono eliminati (FTZ/DAZ) e dopo la coda l'uscita è esattamente zero.
- Licenza del plugin GPL-3.0 (componenti Dusk Audio e ZLInflator sono GPL-3.0).

## 3. Riga di servizio

- **INPUT:** GAIN ±12 dB a passi di 0,5.
- **LEVEL:**
  - meter di picco IN e OUT stereo (stessa scala e colori di The Stripper);
  - blocco **GR** con cinque righe ambra, 0-12 dB, rosse in cima: **G** glue, **C** clip, **L** limit, **F** final, **M** metalhead; ogni riga si attenua con l'IN del suo stadio e con il BYPASS;
  - **LED di clip per modulo**, in ordine di segnale: IN, FEQ, VEQ, GLU, CLP, LIM, FIN, MTL, OUT (sigle confermate, §7); un clic su uno li azzera tutti; quelli degli stadi si attenuano con il BYPASS e con il loro IN;
  - lettura **LUFS** su uno schermo piccolo accanto ai LED: short-term, integrata (clic per azzerare) e **TO TARGET** (scarto dal target, LED verde entro ±0,5 LU); la riga dei LED di clip continua sotto lo schermo. Il meter IN legge dopo il guadagno INPUT; il meter OUT e i LUFS leggono l'uscita del plugin (dopo OUTPUT e guardiano); un bus mono è elaborato come doppio mono e i LUFS pubblicati sono corretti di −3,01 LU (§9). Il LED di clip di uno stadio si accende per un campione con |x| ≥ 1,0 all'uscita dello stadio (quelli della zona sovracampionata sono letti alla frequenza sovracampionata) e resta acceso 2 s.
- **LOUDNESS** (blocco tra LEVEL e OUTPUT, deciso 2026-10-21): manopola LOUDNESS 0-10 a passi di 0,5; **MATCH** (azione, in alto a destra); **TARGET** −14 / −11 / −9 / −7 LUFS (selettore a ciclo, in basso a destra). Sta tra LEVEL e OUTPUT perché legge i LUFS accanto e agisce sugli ultimi stadi prima dell'uscita. INPUT e OUTPUT sono più stretti per farle posto.
- **OUTPUT:** GAIN ±12 dB, **BYPASS** (attenua e blocca tutto tranne meter e LUFS; uscita = ingresso ritardato della latenza, §2), **CEIL**: ceiling d'uscita unico −0,1 / −0,3 / −0,5 / −1,0 dB (default −0,3), selettore a clic con LED giallo e valore scritto sotto. Vale per LIMIT, FINAL (clip e inflate) e METALHEAD, anche dopo il guadagno OUTPUT (§2).

## 4. Schermo condiviso

Un solo schermo in un riquadro senza titolo, accanto al FINE EQ, con a destra una colonna di pulsanti a faccia accesa **EQ · GLUE · CLIP · LIMIT · FINAL**:

- **EQ:** curve del FINE EQ (una curva STEREO, oppure MID e SIDE appena una banda lavora in M/S) e curva del VIBE EQ in ambra (tratteggiata l'impostazione statica quando il DYN ritira un boost); durante LISTEN la zona ascoltata.
- **GLUE / LIMIT:** livello, gain reduction e soglia (GLUE, più la riduzione netta dopo il MIX) o ceiling (LIMIT), che scorrono.
- **CLIP / FINAL:** curva di trasferimento con il punto che segue i picchi (FINAL con la linea del ceiling).

Gli schermi propri di VIBE, GLUE, CLIP, LIMIT e FINAL sono stati tolti: le loro informazioni sono qui o nel LEVEL.

## 5. Stadi

### 5.1 FINE EQ

- **DSP:** port senza JUCE del Multi-Q di Dusk Audio (`AnalogMatchedBiquad`: struttura delle bande, Q-coupling); al posto dell'oversampling coefficienti **matched**: **latenza zero**, curve corrette fino a Nyquist, fase minima, nessuna saturazione.
  - **Campane** e **HIGH SHELF**: progetto matched di Vicanek (boost diretto, taglio come reciproco esatto del boost); l'errore contro il prototipo analogico resta sotto 0,3 dB fino a Nyquist per tutti gli angoli a 44,1 / 48 / 96 kHz. **LOW SHELF** e passa-alto restano i progetti del Multi-Q (frequenza sul punto di svolta, Q 0,7071 per shelf e passa-alto).
  - Le frequenze sono limitate sotto Nyquist (HIGH SHELF 20 kHz e HIGH MID 12,5 kHz restano stabili a 44,1 kHz e a 192 kHz); le variazioni di coefficienti scorrono linearmente su 64 campioni, quindi niente scatti sulle manopole.
- **Bande (tutte a scatti):**
  - HIGH PASS 12 dB/ottava: Off, 20, 25, 30, 35, 40 Hz (sei posizioni, come la scala del SIDE); quando lavora sul SIDE la scala diventa Off, 60, 80, 120, 160, 200 Hz (mono-bass);
  - LOW SHELF 30, 50, 70, 100, 150, 200, 300 Hz;
  - LOW MID 100-800 Hz, MID 400 Hz-3,15 kHz, HIGH MID 1,6-12,5 kHz (dieci posizioni a terzi d'ottava ciascuna);
  - HIGH SHELF 3, 5, 8, 10, 12, 16, 20 kHz;
  - guadagno ±4 dB a passi di 0,5; Q delle campane Wide / Medium / Narrow / Surgical (0,5 / 1 / 2 / 5) con **Q-coupling** (più guadagno, campana più stretta).
- **Per banda:** IN; **M/S** (ST, MID, SIDE; niente L/R), selettore a clic con valore sotto; **LISTEN** sulle tre campane.
- **LISTEN:** esclusivo (una banda sola); le altre bande si attenuano e si bloccano (IN compreso); **si spegne alla chiusura dell'editor e non si salva** nello stato. Suona la campana **sulla propria via** (deciso 2026-10-24): se la banda è su ST si ascolta lo stereo, se è su MID il solo MID, se è su SIDE il solo SIDE; è un passa-banda alla frequenza e al Q effettivo della campana. Con la banda o lo stadio spento LISTEN vale spento (§2).
- Bande spente o escluse: comandi attenuati e non cliccabili; resta vivo solo il loro IN.
- Layout: intestazione a tratto con il nome della banda (come l'EQ di The Stripper).

### 5.2 VIBE EQ

- **DSP:** port senza JUCE del Tube EQ del Multi-Q di Dusk Audio (`TubeEQProcessor.h`: `InductorModel`, `TubeEqLFSection`, `TubeEQTubeStage`; rete LC passiva, non linearità dell'induttore, stadio valvolare tipo 12AX7).
  - **Induttori:** lavorano molto sotto il livello del segnale (−30 dB, `kVibeInductorLevelDb`): la sorgente nei commenti dice 10-15 dB ma li alimenta a livello pieno, e con LOW 10 comprimeva già a −10 dBFS. Il modello è limitato (eccesso e termini polinomiali non crescono oltre il fondo scala; limite di sicurezza dello stato 1e3): la saturazione del nucleo tocca solo i picchi più alti (LOW 10 a 60 Hz: THD sotto −50 dB a −10 dBFS).
  - Tutto ciò che si tara a orecchio sta in `tuning.h` (`kVibe*`, `kDyn*`).
- **LOW:** frequenza 20 / 30 / 60 / 100 Hz e quantità 0-10: un'unica manopola fa boost e taglio subito sopra nella proporzione classica ("Pultec trick"). Il boost è una campana alla frequenza scelta (1,4 dB per scatto); il **taglio è una campana a 2,5 volte la frequenza del boost** (Q 0,7, 1,75 dB per scatto di attenuazione, con attenuazione = 0,5 × LOW), non lo shelf basso alla stessa frequenza della sorgente, che taglierebbe tutto il fondo e non può fare una gobba sopra il boost.
- **AIR:** frequenza 5 / 8 / 10 / 12 / 16 kHz e quantità 0-10 (1,8 dB per scatto), banda larga: Q fisso 0,8 e campana matched di Vicanek, quindi vicino a Nyquist tiene la larghezza e non alza i medi (il polinomio del Q della sorgente è fatto per il basso e a 10 kHz darebbe un'alzata piatta di tutta la banda).
- **TILT:** ±3 dB a passi di 0,5 (DARK…BRIGHT): shelf basso e alto in direzioni opposte attorno a 1 kHz (shelf matched, Q 0,7071), simmetrico: il guadagno a 50 Hz è l'opposto di quello a 10 kHz.
- **TUBE:** 0-10, default 2: lo stadio valvolare dopo tutta la rete, cioè l'uscita dello stadio (colora di più dove l'EQ alza). Fedele alla sorgente (circa −44 dB di THD a TUBE 10); **TUBE 0 è un bypass esatto**. Gira nella zona sovracampionata A (§9).
- **DYN su LOW e su AIR:** il boost si ritira sul programma forte (soglia e tempi fissi, valori di partenza in `tuning.h`, da tarare a orecchio: soglia −18 dB, attacco 10 ms, rilascio 200 ms, ritiro fino all'80 % della quantità; il rilevatore di LOW ascolta sotto 400 Hz, quello di AIR sopra 2 kHz). Pulsantino sulla riga d'intestazione del gruppo, a faccia accesa (eccezione come la campana di The Stripper), con il LED **ACT** ambra accanto, acceso quanto il boost si sta ritirando.
- Layout: gruppi LOW e AIR con intestazione a tratto, FREQ in alto e quantità in diagonale; TILT e TUBE a destra.

### 5.3 GLUE

- **DSP:** port senza JUCE di `UniversalCompressor::BusCompressor` di Multi-Comp (Dusk Audio, GPL-3.0; stile SSL G); il `MultiCompBus` di The Stripper ha solo amount e mix e non basta. In parallelo (MIX).
  - Rilevatore in retroazione come nella sorgente (l'uscita compressa precedente, passa-alto a 2 poli, media RMS di 5 ms), ginocchio morbido di 10 dB, riduzione massima 20 dB, attacco e rilascio esponenziali, rilascio Auto dipendente dal programma (150-450 ms). **Stereo sempre collegato**: un solo guadagno, dal rilevatore più forte.
  - Colore del profilo Console Bus: trasformatore d'ingresso e d'uscita, armoniche k2/k3; compensazione di guadagno misurata in `prepare()` (a 1 kHz, −18 dBFS il blocco è a guadagno unitario). La convoluzione corta della sorgente è un bypass unitario e non è portata.
  - MAKEUP e MIX scorrono linearmente in 10 ms, come l'accensione e lo spegnimento (a stadio spento: identità esatta). MAKEUP agisce solo sul ramo compresso; il rilevatore non dipende né da MIX né da MAKEUP, quindi MIX 0 è l'ingresso bit per bit. **GR mostrata:** `grDb()` = riduzione del compressore; `netGrDb()` = riduzione dopo il MIX e **prima** del MAKEUP (è quella che legge lo schermo).
- **Comandi:** THRESH −20…0 dB (1 dB), ATTACK 0,1 / 0,3 / 1 / 3 / 10 / 30 ms, RELEASE 0,1 / 0,3 / 0,6 / 1,2 s / Auto, MAKEUP 0…+6 dB (0,5), **MIX** 0-100 % a passi di 5 (default 50 %); selettori a clic **RATIO** 1,5 / 2 / 4 (default 2:1) e **SC HPF** Off / 60 / 90 / 150 Hz (default 60); **WARM** (attacco ammorbidito); IN.
- Parametri congelati il 2026-10-24 (servono per fissare gli ID); **WARM** moltiplica per 3 la costante d'attacco del rilevatore; il suono si rivede a orecchio con tutta la catena nel compito di taratura (§9).

### 5.4 CLIP

- **DSP:** `ClipSoftly` di Airwindows (MIT), port **fedele** della curva: sin(x) con l'ingresso limitato a ±π/2 e la miscela "soft speed" sopra il fondo scala. Tolti dither di uscita, scala 0,955 della sorgente (−0,4 dB: i piccoli segnali devono passare a guadagno unitario) e il buffer di ritardo (nella sorgente il ciclo di scorrimento copia il campione in ogni cella, quindi dava sempre un campione, senza effetto sui valori): latenza zero, nessuna dipendenza dalla frequenza di campionamento.
- **DRIVE** 0…+6 dB a passi di 0,5, a **livello compensato** (guadagno in ingresso e reciproco in uscita: le parti piano passano invariate, i picchi si arrotondano; l'uscita massima è 1/drive); scorre in 10 ms; IN. CLIP non ha uno stato spento proprio: l'engine lo dissolve con il segnale secco in 10 ms.
- La curva sin(x) a DRIVE 0 dà circa −39,5 dB di THD su un seno a −6 dBFS: è la curva fedele; a −20 dBFS la distorsione è sotto −60 dB e il livello resta entro 0,1 dB.

### 5.5 LIMIT

- **DSP:** limiter nostro con look-ahead sul picco del campione; la soglia fa anche da recupero di guadagno come negli L2 (i picchi arrivano sul ceiling d'uscita). Obiettivo: 2-3 dB di riduzione. Un solo guadagno collegato sui due canali: minimo scorrevole sulla finestra di look-ahead, attacco in rampa, release esponenziale, media mobile lunga quanto la rampa, poi ritardo del guadagno per allinearlo al segnale. Un guardiano finale per campione, collegato, tiene l'uscita sotto il ceiling in ogni stile, soglia, SET e transizione; con la finestra giusta non interviene, tranne in PUNCH. Spento: ingresso ritardato bit per bit (dissolvenza di 10 ms), il rilevatore gira comunque; ingressi non finiti azzerati.
- **THRESH** 0…−4 dB a passi di 0,5.
- **Stili** (scelta esclusiva, faccia accesa, in colonna in basso a destra): **MODERN** (look-ahead pieno, 1,5 ms; release veloce e fissa; ginocchio duro), **PUNCH** (look-ahead corto, 0,3 ms, rampa d'attacco di 0,45 ms: lascia passare il primo istante del transiente; release media; duro), **DYN** (look-ahead 0,8 ms; release lenta e dipendente dal programma; ginocchio morbido di 6 dB).
  - **Latenza costante:** la latenza dichiarata è sempre quella di MODERN, per ogni stile (arrotondata al multiplo del fattore di oversampling, perché resti intera a frequenza base); PUNCH guarda meno avanti ma il ritardo resta quello di MODERN.
  - **Garanzia del ceiling:** MODERN e DYN scendono nella stessa lunghezza del look-ahead, quindi vale per costruzione; PUNCH scende più piano di quanto guarda avanti (0,45 ms contro 0,3), quindi qui è il guardiano per campione a tenere il picco sotto il ceiling.
  - **DYN:** la release è moltiplicata per un fattore 0,5…1,5 che segue la riduzione media su 400 ms (riferimento 3 dB): picchi brevi, release breve; riduzione pesante e prolungata, release lunga.
- **RELEASE** è un ritocco attorno alla release dello stile: tacca centrale = come vuole lo stile, 4 scatti per parte (un terzo d'ottava ciascuno: fattore 2^(k/3), k = −4…+4), da FAST a SLOW; il valore mostra i ms reali. Basi provvisorie (`tuning.h`): MODERN 50 ms, PUNCH 100 ms, DYN 250 ms.
- **SET** (taratura guidata della release): la soglia scende in modo visibile fino a −8 dB (oltre la scala: il puntatore resta a fondo corsa, il valore mostra quella vera), tutto il blocco si attenua e si blocca tranne RELEASE, compare "SET THE RELEASE"; ripremendo SET la soglia torna com'era. **Si spegne alla chiusura dell'editor e non si salva.**
  - **SET mantiene il recupero di THRESH (deciso 2026-10-24):** la soglia effettiva scorre (8 dB in 150 ms) verso −8 dB, ma il recupero di guadagno segue sempre THRESH (scorre alla stessa velocità), non la soglia forzata. Con SET la schiacciata si sente di più, senza salto di volume (rumore rosa a −12 dBFS RMS, THRESH −2: uscita non più forte di +0,5 dB con SET, GR maggiore) e i picchi restano sotto il ceiling. Anche i passi normali di THRESH scorrono alla stessa velocità.

### 5.6 FINAL

- **INFLATE** (port di ZLInflator in `shared/inflator`, GPL-3.0, già con test): EFFECT 0-100 % (passi di 10), CURVE 0 / 25 / 50 / 75 / 100 %. Come nella preview: shaper quartico (CURVE va dritto al parametro dello shaper) e quadratico al 50 %, senza compensazione, wet = EFFECT. Il **fondoscala dell'inflator è il CEIL** (ingresso diviso per il ceiling, uscita moltiplicata): il corpo sale e i picchi al ceiling restano lì; il resto sopra il ceiling (ingresso caldo con EFFECT < 100 %) è tagliato da un clamp. EFFECT 0 = ritardo esatto.
- **CLIP** (`ClipOnly2` di Airwindows, MIT, un solo percorso double, senza dither): DRIVE 0…+2 dB a passi di 0,5 contro il ceiling d'uscita; il ginocchio (0,955, −0,4 dB) sta sul ceiling; DRIVE è guadagno in ingresso **non** compensato; il clamp finale c'è comunque. Il buffer di ritardo della sorgente (bug del ciclo di scorrimento) dava sempre un campione: il port ha latenza esatta di un campione.
- **Latenza e passaggi:** uguale in entrambi i modi e da spento (un campione a frequenza base per fattore di oversampling); accensione, spegnimento, cambio di modo e glide di EFFECT e DRIVE sono dissolvenze di 10 ms; spento = ingresso ritardato bit per bit.
- Scelta esclusiva INFLATE / CLIP in colonna sotto l'IN; **si vedono solo i comandi del modo scelto**; ognuno conserva i suoi valori. Entrambi i modi restano sotto il ceiling.

### 5.7 METALHEAD

- Nome confermato il 2026-10-24. Stadio facoltativo, **spento di default**: limiter digitale brickwall dopo il FINAL, per la loudness estrema. Spento: riquadro attenuato e bloccato tranne l'IN.
- **PUSH** 0…+6 dB contro il ceiling d'uscita. Serigrafia "SHAME ON YOU" nell'angolo, con il carattere delle sigle.
- **DSP:** il limiter del LIMIT (§5.5) in un **quarto stile fisso, METAL**, che la manopola LIMIT non offre: look-ahead 1 ms, rampa d'attacco 1 ms (quindi il ceiling tiene per costruzione), release 30 ms, ginocchio duro. Soglia = CEIL − PUSH, così il recupero di guadagno è esattamente PUSH dB e ciò che supera il ceiling è abbassato al ceiling. Spento: ingresso ritardato bit per bit; latenza costante da acceso e da spento (quella di MODERN, arrotondata al multiplo del fattore di oversampling).

## 6. Automatismi

- **Macro LOUDNESS (decisa 2026-10-15; rapporto con i comandi deciso 2026-10-24):** una manopola aggiunge spinta **sopra** le regolazioni degli stadi, con una curva fissa: prima CLIP (fino a +1,5 dB di drive), poi LIMIT (soglia fino a −3 dB, cioè 2-3 dB di riduzione), infine FINAL (EFFECT o DRIVE in più, sempre sotto il ceiling). La corsa 0…10 è divisa in terzi: ogni stadio possiede un terzo (CLIP 0…3,33, LIMIT 3,33…6,67, FINAL 6,67…10), sale linearmente nel proprio terzo e poi tiene il massimo; FINAL aggiunge al massimo +60 punti percentuali di EFFECT o +2 dB di DRIVE (sempre entro il fondo scala del comando). I valori della curva sono in `tuning.h` (`kLoudCurve`). Le manopole degli stadi non si muovono e fissano il punto di partenza; a LOUDNESS 0 il suono è identico bit per bit a quello delle regolazioni (e `mapParams` restituisce gli stessi indici degli stadi anche con LOUDNESS 10). Quanto lavora ogni stadio si legge nelle righe GR. Distribuzione legata allo stile o manopola CHARACTER: scartate per ora, si possono aggiungere sopra questo schema.
- **Target LUFS con MATCH (deciso 2026-10-15):** scelta del target (−14, −11, −9, −7 LUFS); il pannello indica lo scarto; il tasto MATCH, premuto sulla parte più forte del brano, porta una sola volta la LOUDNESS al valore che centra il target, poi resta ferma: sposta l'indice di (scarto ÷ 0,75 dB) scatti, nei limiti 0…20 (`kMatchDbPerStep`, da tarare); senza misura (short-term assente) non fa nulla. Nel plugin è una modifica unica dell'host (`Controller::doMatch()`).
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
| Multi-Q (Dusk Audio): `AnalogMatchedBiquad`, Tube EQ (`TubeEQProcessor`, `ADAASaturation`) | FINE EQ, VIBE EQ | GPL-3.0 |
| Multi-Comp (Dusk Audio): `BusCompressor` | GLUE | GPL-3.0 |
| Airwindows (Chris Johnson) | CLIP (`ClipSoftly`), FINAL (`ClipOnly2`) | MIT |
| ZLInflator (ZL Audio, zsliu98) | FINAL inflate (`shared/inflator`) | GPL-3.0 |
| libebur128 (Jan Kokemüller) | costanti del prototipo analogico della pesatura K del meter LUFS | MIT |
| Vicanek, "Matched Second Order Digital Filters" (2016) | progetto matched di campane e shelf alto | metodo pubblicato (nessun codice) |
| VST 3 SDK, VSTGUI (Steinberg) | plugin ed editor | MIT, BSD-3-Clause |
| Font del nastro | come gli altri plugin | OFL-1.1, Apache-2.0 |

Percorsi nei submodule e licenze dei componenti portati sono nel README del plugin (`src/happy-ending/README.md`); l'elenco va ripreso nei CREDITS del retro. Licenze da ricontrollare sui file `LICENSE` prima del rilascio. ZL Equalizer è stato escluso (AGPL-3.0).

## 9. Stato, latenza, robustezza

- **Non salvati nello stato:** LISTEN e SET (sempre spenti all'apertura del progetto e alla chiusura dell'editor).
- **Stato (Task 13):** versione 1, i soli parametri della tabella congelata (OVERSAMPLING compreso); uno stato più corto (build più vecchia) carica ciò che c'è e lascia il resto ai default. **Bus:** un ingresso e un'uscita, entrambi mono o entrambi stereo, nessun sidechain; il mono è elaborato come doppio mono (un buffer di scarto) e i LUFS pubblicati sono corretti di −3,01 LU (`kDualMonoLufsOffsetDb`) solo a bus mono. **Conversione degli indici:** `indexFromNorm` è `min(steps − 1, floor(norm · steps))`, cioè la conversione dell'SDK (`FromNormalized`): tutto il plugin legge lo stesso indice. **Display:** parametri di sola lettura per il futuro editor da ID 1000 (picchi, GR, LED di clip, LUFS, DYN, latenza), più `kClearClips` e `kResetIntegrated` scrivibili. Il cambio di OVERSAMPLING chiede all'host di rileggere la latenza e ricostruisce le zone alla riattivazione.
- **Latenza:** FINE EQ, VIBE EQ lineare, GLUE e CLIP a latenza zero. Latenza dichiarata all'host = oversampler della zona A + oversampler della zona B + (look-ahead di LIMIT, FINAL e METALHEAD alla frequenza sovracampionata) ÷ fattore + look-ahead del limiter di sicurezza (1 ms alla frequenza base: 48 campioni a 48 kHz, 45 a 44,1 kHz, 192 a 192 kHz); dipende solo dal fattore di OVERSAMPLING e dalla frequenza di campionamento (costante tra stili, modi e acceso/spento, limiter di sicurezza compreso: da spento è un puro ritardo, così il BYPASS resta esatto) e si ricalcola solo cambiando quell'impostazione. I numeri 69 / 76 sono la latenza di un solo oversampler (la catena ne ha due, zone A e B), non il totale. `getTailSamples` = latenza.
- **Oversampling (deciso 2026-10-24):** impostazione **OVERSAMPLING** in BEHAVIOUR (salvata nel progetto): Off / 2× / 4×, default 2×; vale solo per gli stadi non lineari (CLIP, TUBE del VIBE EQ, FINAL, METALHEAD). Due zone, con lo stesso oversampler: **A** = TUBE del VIBE EQ; **B** = CLIP → LIMIT → FINAL → METALHEAD (il limiter sta dentro perché è in mezzo agli stadi non lineari: una sola coppia di filtri invece di tre). Filtri a fase lineare: semibanda FIR simmetrica in forma polifase (Kaiser, 100 dB di reiezione), **piatta fino a 20 kHz** a 44,1 kHz (bordo della banda passante 0,4535 × fs, ondulazione ben sotto 0,1 dB; ×2 è il default e la zona B copre CLIP→METALHEAD); latenza dell'oversampler **69 campioni a ×2 e 76 a ×4** a frequenza base (sempre intera; a ×4 un campione extra a 2× nella catena di decimazione la rende tale), che si somma a quella dei look-ahead. Un cambio di OVERSAMPLING mentre il plugin gira non tocca mai l'engine nel thread audio: il controller manda al processor un messaggio e chiede all'host di rileggere la latenza (`kLatencyChanged`); le zone si ricostruiscono alla riattivazione. Resta il caso di un host che ripristina i parametri uno a uno al caricamento: può dare un riavvio per un OVERSAMPLING non di default.
- **Ceiling dopo il filtro di decimazione:** vedi §2 (limiter di sicurezza e guardiano dopo OUTPUT; la latenza aggiunge il look-ahead del limiter di sicurezza): il filtro della zona B può ricostruire campioni fino a circa +0,4 dB sopra il ceiling tenuto alla frequenza sovracampionata.
- **Valori da tarare a orecchio (deciso 2026-10-24):** soglie e tempi del DYN del VIBE, basi di release degli stili, soglia forzata di SET, curva della LOUDNESS. Si parte dai valori della preview, raccolti in un'unica tabella nel codice (`src/happy-ending/src/tuning.h`; i test leggono la tabella, non copie dei numeri); il piano ha un compito di taratura con una build di prova, che comprende anche l'ascolto finale di FINE EQ e GLUE con tutta la catena. **Stato: non ancora fatta**, i valori in `tuning.h` sono quelli di partenza (anche il passo di MATCH); quando l'utente avrà ascoltato, i valori finali si riportano qui.
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

Nessuna decisione di progetto aperta (chiuse il 2026-10-24: LOUDNESS sopra i comandi §6, oversampling §9, valori da tarare §9, parametri di FINE EQ e GLUE congelati §5, sigle §7, nome METALHEAD §5.7, dimensioni del pannello e principio a due livelli §7). Restano aperti:

- **Taratura a orecchio** (§9, Task 14 del piano): soglia e tempi del DYN, basi di release degli stili, soglia di SET, curva della LOUDNESS, passo di MATCH, ascolto finale di FINE EQ e GLUE con tutta la catena, in un host. Tocca all'utente; i valori di `tuning.h` sono quelli di partenza.
- **Verifica della latenza in un host reale** (REAPER, Bitwig): che la compensazione di ritardo dichiarata (69/76 campioni di oversampler più i look-ahead) allinei davvero la traccia, anche cambiando OVERSAMPLING; finora provata solo con i test del motore e del processor.
- **Licenze:** controllo sui file `LICENSE` dei componenti prima del rilascio (§8); l'elenco va ripreso nei CREDITS dell'editor (piano a parte).
- **Editor grafico:** piano `docs/superpowers/plans/2026-10-31-happy-ending-editor.md` (Task 1-16), scelte tecniche nel §13; da eseguire.

## 12. Storia del brainstorming

Punto 0 (2026-10-15) e riepilogo: `.superpowers/brainstorm/mastering/` (fuori da git). Le scelte successive sono in questa spec.

## 13. Editor: scelte tecniche (2026-10-31)

Approvate dall'utente il 2026-10-31 con il piano dell'editor (SIZE e retro corretti da lui: ogni plugin ha la sua dimensione).

- **SIZE di ciascun plugin (decisione dell'utente, 2026-10-31):** ogni plugin ha la sua dimensione: SIZE si salva nel file di preferenze del plugin (`HappyEnding.prefs`, chiave `size=75|90|100|125`, default 100), non in un file comune. La finestra è 1280 × 890 × fattore dell'host × SIZE. Cambiata dal retro si applica subito chiedendo all'host di ridimensionare; se l'host rifiuta, vale alla prossima apertura. `shared/ui` dà solo la matematica (fattori, dimensione in pixel, zoom).
- **Retro con la dimensione del plugin:** anche il retro ha la dimensione di ciascun plugin. Il template comune (`shared/ui`) prende la geometria orizzontale e verticale dal layout che il plugin gli passa; quello di Happy Ending (1280 × 890, numeri della preview) sta nel plugin. I layout degli altri plugin non cambiano.
- **Dati dello schermo:** il motore pubblica altri sei valori di visualizzazione, dopo quelli esistenti: picco all'ingresso di GLUE, LIMIT, CLIP e FINAL (−60…+12 dB), riduzione del compressore della colla prima del MIX e soglia effettiva del LIMIT (con la spinta della LOUDNESS e SET).
- **Curve EQ:** le calcola l'editor con copie proprie di FINE EQ e VIBE EQ a 48 kHz, impostate dai parametri come il motore (mai sull'audio); la curva del VIBE con il ritiro del DYN usa il valore pubblicato.
- **Vista dello schermo** (EQ, GLUE, CLIP, LIMIT, FINAL): stato dell'editor, ricordato finché vive l'istanza, non salvato nel progetto; all'apertura di un progetto è EQ.
- **Stato v2:** come Deep Throat, i parametri più il decor (seme, angoli delle quattro viti, fino a sei scritte sul nastro); un progetto v1 si apre con il decor di default.
- **Preferenze del plugin:** `HappyEnding.prefs` con le chiavi degli altri plugin (wear, notes, tapeFont, valuesFont).
- **LISTEN e SET alla chiusura dell'editor:** l'editor li spegne con una modifica unica ciascuno (sono parametri di sessione nascosti; il motore non li salva già oggi).
- **Comandi del pannello (Task 9 del piano dell'editor):** il valore sotto le manopole si stampa a R + 36 dal centro del cappuccio come nella preview (la serigrafia comune lo mette a R + 39,5, dove sotto le manopole basse del FINE EQ tocca le etichette dei pulsanti); nomi a R + 24 (preview R + 22) ed etichette dei pulsanti a 11 px (preview 10) restano quelli comuni. I selettori M/S stampano i testi di `formatValue` (Stereo / Mid / Side, come l'host), non le sigle della preview (ST / MID / SIDE). LIMIT RELEASE stampa il tempo vero in ms; il suo campo di testo usa i testi dell'host (Style, −4…+4). I pulsanti delle viste sono una sola scelta a faccia accesa con facce 34 × 20 (preview 34 × 18). I LED di LISTEN mostrano la campana davvero ascoltata (spenti con la banda o il FINE EQ spenti).
- **Parametri a scelta per l'host:** restano parametri a passi con i testi dati da `getParamStringByValue` (niente `StringListParameter`): l'editor mostra comunque i valori, e gli host leggono già i testi.
