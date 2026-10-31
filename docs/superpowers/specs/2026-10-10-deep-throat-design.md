# Deep Throat — Design

**Data:** 2026-10-10
**Stato:** bozza per revisione
**Suite:** Fairy Tapes (channel strip vocale "tutto dentro")
**Nome:** Deep Throat (deciso il 2026-10-10)

## 1. Scopo

Channel strip per voci, in catena fissa e con pochi controlli: si mette sulla traccia voce e basta. Tre modalità che cambiano i parametri interni di tutta la catena (e il metodo di rilevamento dei respiri):

- **Speak:** voce parlata (podcast, voice-over).
- **Sing:** voce cantata.
- **Puke:** voci estreme (scream, growl).

Successo: su una voce grezza, scelta la modalità, si arriva a un suono uniforme, pulito dai respiri e compatto con 4-5 manopole.

## 2. Decisioni (2026-10-10)

- Catena fissa: **Rider → Breath control → EQ correttivo → De-esser → Compressore (stile Distressor) → EQ Tone a una manopola**.
- **Rider** (livellatore automatico del volume, tipo Waves Vocal Rider) scritto da noi: non esiste un rider open source da inglobare (verificato; i gratuiti sono closed source). Basato solo sul livello, mai sul pitch (i growl non ne hanno).
- **Breath control** non è un gate: è un riduttore di respiri. Non esiste un modello o plugin open source da inglobare (verificato; Waves DeBreath, T-De-Breather, VoiceGate sono commerciali).
  - Livello 1 (ora): rilevatore DSP classico.
  - Livello 2 (futuro): rete piccola eseguita con RTNeural, dietro l'interfaccia `BreathDetector`. Servono dati etichettati.
- **EQ finale a una manopola** (Tone).
- Il VocAlign (allineamento nel tempo di due tracce) è fuori scopo.
- **De-esser e compressore sono scritti da noi.** `DeEss` di Airwindows (MIT) è stato valutato e scartato: la sua Freq non è in Hz, i canali non sono collegati, non ha Listen. Nessun Distressor open source è riusabile (Thistressor dichiara GPL-3 ma è venduto senza sorgente pubblico; Valentine è un compressore generico). Il nome del dispositivo non compare mai nella UI; la classe e i file interni si chiamano `DistressorComp` / `distressor_comp.{h,cpp}` (nome interno deciso dal piano, mantenuto).

## 3. Segnale

1. **Input**.
2. **Rider:** RMS a finestra 100-300 ms (o LUFS a breve termine) confrontato col Target. Il guadagno si muove di ±Range dB con pendenza massima limitata (Speed). Resta fermo dove il Breath control rileva silenzio o respiro. Look-ahead 50-100 ms. Implementato: livello RMS a finestra esponenziale di 200 ms sul mono linkato (L+R)/2, ignorato sotto -60 dB come durante il freeze.
3. **Breath control:** abbassa i respiri di una quantità (Amount), con look-ahead e isteresi; non chiude mai del tutto. La sensibilità del rilevatore è regolabile (Sensitivity) e un pulsante Listen fa ascoltare i soli respiri tolti. Il Listen usa solo il guadagno del riduttore (non quello del Rider), funziona anche con Breath IN spento e ha la precedenza sul De-ess Listen.
4. **EQ correttivo:** passa-alto, taglio del fango (200-400 Hz), notch dinamico per risonanze. Valori di base dipendenti dalla modalità. Il passa-alto è a 12 dB/ott, che a 30 Hz con il passa-alto a 80 Hz dà circa 17 dB di attenuazione (il test chiede 15 dB, non i 20 del piano originale). Il notch dinamico analizza la somma mono ogni 50 ms e si inserisce solo se lo stesso picco prominente resta entro ±1,5 bin FFT per almeno 300 ms (persistenza): le armoniche con vibrato e il pitch del parlato in movimento passano; un tono tenuto perfettamente dritto può invece essere inciso dopo 300 ms (profondità massima 6 dB × 2·Amount; vedi Decisioni aperte). Nei test la planarità della banda passante si misura a 1 kHz solo nei primi 10-30 ms (un seno stazionario conta come risonanza) e a 800 Hz nella prova lunga. Il controllo **EQ Amount** (0..1, default 0.5 = valori della modalità) li scala: frequenza del passa-alto × (0.5 + Amount), taglio del fango e profondità del notch × (2 · Amount).
5. **De-esser:** a banda separata, frequenza regolabile (Freq, 4-10 kHz, default 6,5 kHz), con ascolto del solo segnale tagliato. Il crossover è complementare: la banda bassa è un passa-basso Linkwitz-Riley del 4° ordine e la banda alta è `in − low`, così con guadagno 1 l'uscita coincide con l'ingresso entro l'arrotondamento in virgola mobile (una somma LR4 vera sarebbe un passa-tutto). Il rilevatore è un RMS di 2 ms sulla banda 0,62..1,38 × Freq (bordi limitati a 0,45 × sample rate): la soglia di -30 dBFS vale circa -27 dBFS di picco per un seno. Il guadagno è lo stesso su tutti i canali; Listen esce `high · (1 − gain)`.
6. **Compressore stile Distressor:** Ratio selezionabile (AUTO, 2, 3, 4, 6, 8, 10, 20:1), Dist selezionabile (AUTO, OFF, Dist 2, Dist 3: armoniche pari/dispari), topologia feedforward con rilevatore in scala logaritmica e passa-alto sul solo rilevamento (SC HPF selezionabile: OFF, 80 Hz, 150 Hz; default 80 Hz), tempi programma-dipendenti legati al rapporto, distorsione che cresce con la riduzione di guadagno e con l'attacco, Mix parallelo. Dettagli e fonti in `src/deep-throat/docs/COMPRESSOR_RESEARCH.md`. AUTO usa il valore della modalità. Scritto da noi (vedi §2). Scelte implementative: il waveshaper agisce sull'ingresso **prima** dello stadio di guadagno (dopo, la 2ª armonica cresce solo di 10,6 dB e non soddisfa il requisito di 20 dB); il rilascio fonde i due stadi (veloce 50 ms, lento `clamp(100·r, 200, 3500)` ms) con un peso ricavato dal fattore di cresta del rilevatore (picco su RMS, entrambi a 100 ms: cresta ≤ 6 dB → lento, ≥ 12 dB → veloce, lineare in dB tra i due); la compensazione del guadagno è `max(0, GR_ref − 2 dB)` sul solo percorso trattato, con `GR_ref` la riduzione statica di un seno a -15 dBFS di picco alla soglia e al rapporto correnti (così un seno a -18 dBFS RMS esce entro ±3 dB per ogni Amount e sotto soglia il guadagno resta unitario); il percorso trattato termina in un soft clip di sicurezza (identità fino a 0 dBFS, mai oltre 2) per il superamento d'attacco di un transiente forte dopo il silenzio; rilevatore e ingresso del Mix sono limitati a ±1e6 e i valori non finiti valgono 0. Dist 3 è `x + LP12k(HP30(k3·g·x³))`: il passa-alto a 30 Hz e il passa-basso a 12 kHz limitano in banda **solo i prodotti di distorsione** aggiunti, non il segnale (decisione della revisione finale: prima filtravano tutto il percorso trattato, che in Puke con Amount 0 perdeva 10 dB a 16 kHz e 3 dB a 30 Hz anche senza riduzione); senza riduzione l'uscita è piatta (test con seni a 40 Hz, 1 kHz e 16 kHz, Mix 1 e 0,5). Costo se sbagliato: Dist 3 non ha più l'attenuazione agli estremi del segnale principale tipica dell'hardware. Il test «guadagno unitario sotto soglia» vale per Speak e Sing: in Puke (8:1) con Amount 0,5 il segnale sotto soglia guadagna circa +0,6 dB, accettato.
7. **Tone:** una manopola che muove insieme taglio del fango, presenza e aria lungo una curva preimpostata per modalità.
8. **Output**.

**Motore** (`engine.{h,cpp}`): il guadagno d'ingresso è applicato **prima** del ritardo (appartiene alla linea temporale dell'ingresso: una variazione o un'automazione di Input raggiunge l'audio quando l'audio di quel momento esce dal ritardo, non subito all'uscita); Bypass riproduce l'ingresso grezzo ritardato. Latenza costante **D = 80 ms + F** in ogni modalità, con F il frame del rilevatore spettrale (`nextPow2(20 ms · sr)`); il rilevatore a livello (Puke) dichiara latenza 0 e il motore lo allinea, quindi cambiare modalità non cambia la latenza. La probabilità di respiro che pilota riduttore e freeze del Rider è il **massimo scorrevole** delle probabilità prodotte da quando il campione ritardato è entrato nel motore: la finestra è l'intero ritardo D (D + 1 valori), perché entrambi i rilevatori abbassano l'uscita appena la frase raggiunge il loro campione più recente; con questa finestra la riduzione tiene fino all'ultimo campione del respiro nell'audio ritardato e il rilascio di 30 ms parte alla fine del respiro. IN, Bypass e i due Listen sfumano in modo lineare in 5 ms; Input e Output sono smussati per campione; tutti i moduli girano anche con IN spento. De-ess Listen esce `input − output` del de-esser (la banda tolta) e gli stadi successivi non si sentono. Un cambio di modalità che cambia rilevatore azzera il rilevatore appena scelto. I moduli sono chiamati per campione con tutti i canali nello stesso passo (mai per canale su un blocco). Sono accettati sia stereo sia mono → mono. Gli scalini dei parametri a scelta usano `floor(n · count)` (limitato a `count − 1`), come `toPlain` della `StringListParameter` dell'SDK. Il processor non invia una notifica di riavvio per la latenza: gli host la rileggono dopo `setupProcessing`.

## 4. Breath control: rilevamento

Interfaccia `BreathDetector`: in ingresso blocchi di audio, in uscita per ogni frame una probabilità di respiro in [0, 1]. Il riduttore usa solo questa uscita. **Sensitivity** (0..1, default 0.5) sposta le soglie del rilevatore: più alta = rileva anche respiri più deboli.

- **Speak e Sing (rilevatore spettrale):** per frame si calcolano sonorità (flatness spettrale; pitch tramite autocorrelazione o cepstrum), centroide spettrale, durata e contesto (tra due frasi, vicino a silenzio, non agganciato a una vocale). Voce bassa = armonica; respiro = rumoroso, centroide alto, nessun pitch.
- **Puke (rilevatore a livello):** l'analisi spettrale non vale, perché growl e scream sono rumorosi e senza pitch e verrebbero scambiati per respiri. Un respiro è un segmento circa 20 dB o più sotto il livello medio della frase, circondato da voce. Un'inalazione forte tra due urla viene al massimo accorciata, mai tagliata.

Valori implementati, scelti su segnali sintetici (vedi Decisioni aperte):

- **Spettrale (`SpectralTuning`):** `flatnessMin` 0,12 (il valore iniziale 0,30 non scatta su un respiro limitato in banda, che misura circa 0,2), `centroidMinHz` 2000, `harmonicityMax` 0,30, `belowPhraseDb` 10, `voicedMin` 0,6 (armonicità dei frame che pilotano l'inseguitore di frase), `depthFullDb` 15 e `depthZeroDb` 45 (confidenza piena fino a 15 dB sotto la frase, nulla a 45 dB: il rumore debole è ambiente, non respiro), `minDurationMs` 60. Le condizioni sono combinate con `min()`. L'uscita sale con attacco 5 ms e scende con `releaseMs` **2 ms** (non 20): così la frase dopo un respiro è libera entro un frame. L'inseguitore di frase ha rilascio 1,5 s e rilascia solo sui frame sonori; prima del primo frame sonoro non c'è rilevamento, quindi il primo respiro di una ripresa si perde.
- **Livello (`LevelTuning`):** finestra del misuratore **10 ms** (esponenziale; il piano diceva 25 ms: con 10 ms un calo di 25 dB si vede in circa 60 ms), `voiceWithinDb` 15, `phraseReleaseMs` 2000, `holdMarginDb` 3, attacco 5 ms, discese immediate. `latencySamples()` vale 0: la probabilità sale circa 80 ms (durata minima) più il ritardo del misuratore dopo l'inizio del respiro, e il look-ahead del motore deve coprirlo. Il contesto di voce (`contextMs`, 300 ms) serve per **entrare** in un respiro, non per restarci: un respiro attivo resta tale anche oltre i 300 ms, fino a circa 0,7-0,8 s per un respiro profondo 25 dB (oltre, il decadimento di 2 s dell'inseguitore di frase lo rilascia prima del tetto) (decisione della revisione finale; prima un respiro di 400-600 ms veniva rilasciato a metà, con un salto di 12 dB × Amount dentro il rumore del respiro). Tetto `maxDurationMs` = 1500 ms: oltre, la probabilità cade e lo stesso passaggio quieto non rientra finché il livello non torna entro 15 dB dal livello di frase di quando il respiro era iniziato, oppure non sale di `unlockRiseDb` = 8 dB sopra il livello del passaggio al momento del blocco (una voce che riprende più piano dello scream); in questo secondo caso il livello di frase viene riportato al livello corrente, così il respiro successivo è giudicato rispetto alla nuova voce. Il decadimento dell'inseguitore di frase non sblocca, né lo fanno oscillazioni del passaggio sotto gli 8 dB. Costo se sbagliato: un passaggio quieto subito dopo uno scream viene ridotto come un respiro per al massimo 1,5 s.

## 5. Parametri per modalità

| Blocco | Speak | Sing | Puke |
|---|---|---|---|
| Rider | medio, ±6 dB | lento, ±4 dB | quasi fermo, ±3 dB |
| De-esser | normale | normale | quasi spento |
| Compressore | rapporto basso | medio | alto, Dist attivo |
| Tone | più presenza | più aria | taglia il fango, tiene i medi |

Per il compressore, Ratio e Dist con scelta AUTO usano i valori della tabella (Speak 2:1 e nessuna distorsione, Sing 3:1 e Dist 2, Puke 8:1 e Dist 3); le altre scelte li ignorano.

Valori numerici da tarare con registrazioni reali (vedi "Decisioni aperte").

## 6. Parametri esposti

Bypass, Mode (Speak/Sing/Puke), Input, Rider Target, Rider Range, Breath Amount, Breath Sensitivity, Breath Listen, EQ Amount, De-esser Amount, De-ess Freq, De-ess Listen, Comp Amount, Comp Mix, Comp Ratio, Comp Dist, Comp SC HPF, Tone, Output, più un pulsante **IN** per ogni modulo (Rider In, Breath In, EQ In, De-ess In, Comp In, Tone In; default acceso): 25 parametri. A IN spento il modulo lascia passare il segnale (latenza invariata). Il modulo EQ è l'EQ correttivo. Il resto è interno per modalità. Il motore accetta stereo e mono → mono. Input e Output: -12..+12 dB; Rider Target: -30..-10 dBFS; De-ess Freq: 4-10 kHz logaritmica.

## 7. Struttura

`src/deep-throat/` con lo stesso schema degli altri plugin (`plug.cpp`, `controller`, `processor`, `params.h`, `cids.h`, `dsp/`, `tests/`, `docs/`). Namespace `FairyTapes::DeepThroat`. Aggiunto con `add_subdirectory` nel `CMakeLists.txt` della radice. Grafica: `docs/DESIGN_SYSTEM.md`; testo visibile in inglese.

## 8. Test

Ogni blocco ha un test che prima fallisce: Rider (guadagno verso il Target, nessun salto oltre la pendenza), Breath control (respiro sintetico abbassato, vocale bassa armonica intatta, rumore di growl intatto in Puke), De-esser, Tone. Latenza riportata all'host (D = 80 ms + F, costante nelle modalità). Nei test: soglia del passa-alto 15 dB a 30 Hz; de-esser in Puke con calo < 3 dB su un seno a 7 kHz (con crossover complementare un seno appena sopra lo split ha la banda alta a circa 1,4 volte l'ingresso, quindi la scala 0,15 dà 2,65 dB); i bordi del respiro nel motore coperti dal look-ahead (>= 11 dB negli ultimi 20 ms).

## 9. Decisioni aperte

- **OVERSAMPLING (proposto 2026-10-24, da decidere):** come in Happy Ending, impostazione OVERSAMPLING in BEHAVIOUR (Off / 2× / 4×, default 2×, filtri a fase lineare, latenza dichiarata e costante durante la riproduzione), applicata solo agli stadi non lineari di questo plugin: le armoniche Dist 2 / Dist 3 del compressore stile Distressor (con Dist OFF lo stadio è lineare e l'oversampling non serve).
- (risolta) Nome: Deep Throat.
- Registrazioni di riferimento per tarare **tutte** le soglie (rilevatori, de-esser, notch, compressore), soprattutto in Puke (growl e scream): oggi sono tarate su segnali sintetici. Sibilanti contro respiri mai provati su voce reale.
- Dati etichettati per il livello 2 (rete RTNeural).
- Limiti di temporizzazione del Breath control, con D = 80 ms + F: in Speak/Sing la riduzione parte circa 25 ms **prima** dell'inizio del respiro (può intaccare la coda della frase precedente; non coperto da test); in Puke parte circa 30 ms **dopo** (durata minima di 80 ms del rilevatore a livello, riduzione piena circa 50 ms dopo l'inizio).
- Il rilascio di 30 ms del riduttore lascia circa 4 dB di riduzione 25 ms dopo la fine del respiro, cioè sull'inizio della frase successiva (valore del piano).
- Un tono tenuto perfettamente dritto attiva il notch dell'EQ dopo 300 ms (persistenza); da rivedere con registrazioni reali.
- ~~Il rilevatore a livello di Puke si ferma dopo `contextMs` (300 ms) su respiri lunghi.~~ Risolta: il contesto serve solo per entrare in un respiro, con tetto di 1,5 s (§4).
- ~~Difetto aperto: dopo il tetto di 1,5 s il blocco del rilevatore a livello si toglieva solo con un livello entro 15 dB dallo scream precedente (una voce che riprende più piano lasciava il rilevamento spento).~~ Risolta: il blocco si toglie anche quando il livello sale di `unlockRiseDb` (8 dB) sopra il livello del passaggio bloccato (§4).
- (risolta) Riuso per De-esser e Distressor: no, scritti da noi (§2). Nessun riuso del drive di The Stripper né di `shared/hardware_emulation`: i waveshaper sono funzioni locali.
- (risolta) Interfaccia grafica: editor VSTGUI implementato (2026-10-31) secondo la spec `docs/superpowers/specs/2026-10-24-deep-throat-gui-design.md` e il piano `docs/superpowers/plans/2026-10-24-deep-throat-gui.md`; i punti ancora aperti dell'editor stanno nel §11 di quella spec.
- (risolta) Il modulo EQ (ex Clean) ha il controllo Amount.

## 10. Grafica

Segue `docs/DESIGN_SYSTEM.md` (rack orizzontale, materiali e grana di The Stripper, livelli, usura con seme, viti, nastro, retro). **L'editor esiste** (2026-10-31): spec del GUI `docs/superpowers/specs/2026-10-24-deep-throat-gui-design.md`, piano `docs/superpowers/plans/2026-10-24-deep-throat-gui.md`; segue la preview 2026-10-21/23 descritta qui sotto, e le note "editor da allineare" / "editor da fare" sono superate. Specifico di Deep Throat:

- Intestazione con titolo e BACK; il Bypass globale sta nel riquadro OUTPUT (LED rosso).
- **Riga 1:** INPUT (Gain), **SCOPE** (grafico con voce, guadagno del rider e banda dei respiri rilevati), LEVEL (ingresso, uscita, riduzione del compressore, clip LED come ultima riga), OUTPUT (Gain e Bypass).
- **Riga 2:** MODE (selettore Speak / Sing / Puke: tre pulsanti uno sopra l'altro con il nome sulla faccia, quello attivo a faccia ambra, nessun LED — regola comune del 2026-10-21; riquadro più stretto, lo spazio va a BREATH ed EQ; preview 2026-10-21, editor da allineare), RIDER (Target, Range), BREATH (Amount, Sensitivity, Listen, LED di rilevamento, nome del rilevatore), EQ (Amount; HPF e Mud cut di sola lettura che seguono modalità e Amount).
- **Riga 3:** DE-ESS (Amount, Freq, pulsante Listen), COMP (Amount, Mix, selettori Ratio e Dist con AUTO, selettore SC HPF), TONE (una manopola). Preview 2026-10-21: DE-ESS più stretto (IN e LISTEN in colonna, con 36 px di margine a destra), COMP con MIX più staccato da AMOUNT e RATIO/DIST/SC HPF più vicini, TONE più largo; in riga 2 BREATH ed EQ più larghi grazie allo spazio tolto a MODE. Editor da allineare.
- Ogni modulo con un controllo di segnale (RIDER, BREATH, EQ, DE-ESS, COMP, TONE) ha il pulsante IN; titoli dei riquadri in basso.
- Retro: schede SETTINGS, MODES, ROUTING e CREDITS; MODES elenca i valori imposti da ciascuna modalità ed è generata dalla stessa tabella dei preset usata dal codice.
- **Scheda ROUTING (regola comune del 2026-10-23, preview fatta, editor da fare):** link nell'ordine SETTINGS, MODES, ROUTING, CREDITS. Schema fisso nell'ordine del motore (`engine.cpp`): riga 1 IN → DELAY (look-ahead) → INPUT → RIDER → BREATH → EQ → DE-ESS; riga 2 COMP → TONE → LISTEN → OUTPUT → OUT. In mezzo, tratteggiato, il ramo di rilevamento sul segnale non ritardato (INPUT, somma mono, LEVEL del rider, BREATH DETECTOR spettrale o a livello, MAX del look-ahead) che comanda RIDER e BREATH, con il blocco del rider durante i respiri. Il Bypass globale parte dopo il ritardo (uscita = ingresso ritardato); le uscite "removed band" del DE-ESS e "removed breath" del BREATH portano al blocco LISTEN; l'SC HPF sta sul rilevatore del COMP. Punti di misura: IN METER e CLIP IN dopo l'INPUT, GR dopo il COMP, OUT METER e CLIP OUT dopo il Bypass (meter nel processor dal 2026-10-24, parametri nascosti da 1000; i picchi d'ingresso si misurano prima del guadagno d'ingresso, non dopo come nello schema: punto aperto nel §11 della spec del GUI).
