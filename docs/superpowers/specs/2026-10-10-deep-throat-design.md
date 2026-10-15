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
- **De-esser e compressore sono scritti da noi.** `DeEss` di Airwindows (MIT) è stato valutato e scartato: la sua Freq non è in Hz, i canali non sono collegati, non ha Listen. Nessun Distressor open source è riusabile (Thistressor dichiara GPL-3 ma è venduto senza sorgente pubblico; Valentine è un compressore generico).

## 3. Segnale

1. **Input**.
2. **Rider:** RMS a finestra 100-300 ms (o LUFS a breve termine) confrontato col Target. Il guadagno si muove di ±Range dB con pendenza massima limitata (Speed). Resta fermo dove il Breath control rileva silenzio o respiro. Look-ahead 50-100 ms.
3. **Breath control:** abbassa i respiri di una quantità (Amount), con look-ahead e isteresi; non chiude mai del tutto. La sensibilità del rilevatore è regolabile (Sensitivity) e un pulsante Listen fa ascoltare i soli respiri tolti.
4. **EQ correttivo:** passa-alto, taglio del fango (200-400 Hz), notch dinamico per risonanze. Valori di base dipendenti dalla modalità; il controllo **EQ Amount** (0..1, default 0.5 = valori della modalità) li scala: frequenza del passa-alto × (0.5 + Amount), taglio del fango e profondità del notch × (2 · Amount).
5. **De-esser:** a banda separata, frequenza regolabile (Freq, 4-10 kHz, default 6,5 kHz), con ascolto del solo segnale tagliato.
6. **Compressore stile Distressor:** Ratio selezionabile (AUTO, 2, 3, 4, 6, 8, 10, 20:1), Dist selezionabile (AUTO, OFF, Dist 2, Dist 3: armoniche pari/dispari), topologia feedforward con rilevatore in scala logaritmica e passa-alto sul solo rilevamento (SC HPF selezionabile: OFF, 80 Hz, 150 Hz; default 80 Hz), tempi programma-dipendenti legati al rapporto, distorsione che cresce con la riduzione di guadagno e con l'attacco, Mix parallelo. Dettagli e fonti in `src/deep-throat/docs/COMPRESSOR_RESEARCH.md`. AUTO usa il valore della modalità. Scritto da noi (vedi §2).
7. **Tone:** una manopola che muove insieme taglio del fango, presenza e aria lungo una curva preimpostata per modalità.
8. **Output**.

## 4. Breath control: rilevamento

Interfaccia `BreathDetector`: in ingresso blocchi di audio, in uscita per ogni frame una probabilità di respiro in [0, 1]. Il riduttore usa solo questa uscita. **Sensitivity** (0..1, default 0.5) sposta le soglie del rilevatore: più alta = rileva anche respiri più deboli.

- **Speak e Sing (rilevatore spettrale):** per frame si calcolano sonorità (flatness spettrale; pitch tramite autocorrelazione o cepstrum), centroide spettrale, durata e contesto (tra due frasi, vicino a silenzio, non agganciato a una vocale). Voce bassa = armonica; respiro = rumoroso, centroide alto, nessun pitch.
- **Puke (rilevatore a livello):** l'analisi spettrale non vale, perché growl e scream sono rumorosi e senza pitch e verrebbero scambiati per respiri. Un respiro è un segmento circa 20 dB o più sotto il livello medio della frase, circondato da voce. Un'inalazione forte tra due urla viene al massimo accorciata, mai tagliata.

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

Bypass, Mode (Speak/Sing/Puke), Input, Rider Target, Rider Range, Breath Amount, Breath Sensitivity, Breath Listen, EQ Amount, De-esser Amount, De-ess Freq, De-ess Listen, Comp Amount, Comp Mix, Comp Ratio, Comp Dist, Comp SC HPF, Tone, Output, più un pulsante **IN** per ogni modulo (Rider In, Breath In, EQ In, De-ess In, Comp In, Tone In; default acceso): 25 parametri. A IN spento il modulo lascia passare il segnale (latenza invariata). Il modulo EQ è l'EQ correttivo. Il resto è interno per modalità.

## 7. Struttura

`src/deep-throat/` con lo stesso schema degli altri plugin (`plug.cpp`, `controller`, `processor`, `params.h`, `cids.h`, `dsp/`, `tests/`, `docs/`). Namespace `FairyTapes::DeepThroat`. Aggiunto con `add_subdirectory` nel `CMakeLists.txt` della radice. Grafica: `docs/DESIGN_SYSTEM.md`; testo visibile in inglese.

## 8. Test

Ogni blocco ha un test che prima fallisce: Rider (guadagno verso il Target, nessun salto oltre la pendenza), Breath control (respiro sintetico abbassato, vocale bassa armonica intatta, rumore di growl intatto in Puke), De-esser, Tone. Latenza riportata all'host (somma di look-ahead).

## 9. Decisioni aperte

- (risolta) Nome: Deep Throat.
- Registrazioni di riferimento per tarare le soglie, soprattutto in Puke (growl e scream).
- Dati etichettati per il livello 2 (rete RTNeural).
- (risolta) Riuso per De-esser e Distressor: no, scritti da noi (§2).
- Interfaccia grafica: layout in anteprima in `src/deep-throat/docs/mockups/deep_throat_rack.html` (vedi §10); implementazione in un piano a parte.
- (risolta) Il modulo EQ (ex Clean) ha il controllo Amount.

## 10. Grafica

Segue `docs/DESIGN_SYSTEM.md` (rack orizzontale, materiali e grana di The Stripper, livelli, usura con seme, viti, nastro, retro). Specifico di Deep Throat:

- Intestazione con titolo e BACK; il Bypass globale sta nel riquadro OUTPUT (LED rosso).
- **Riga 1:** INPUT (Gain), **SCOPE** (grafico con voce, guadagno del rider e banda dei respiri rilevati), LEVEL (ingresso, uscita, riduzione del compressore, clip LED come ultima riga), OUTPUT (Gain e Bypass).
- **Riga 2:** MODE (selettore Speak / Sing / Puke, LED giallo), RIDER (Target, Range), BREATH (Amount, Sensitivity, Listen, LED di rilevamento, nome del rilevatore), EQ (Amount; HPF e Mud cut di sola lettura che seguono modalità e Amount).
- **Riga 3:** DE-ESS (Amount, Freq, pulsante Listen), COMP (Amount, Mix, selettori Ratio e Dist con AUTO, selettore SC HPF), TONE (una manopola).
- Ogni modulo con un controllo di segnale (RIDER, BREATH, EQ, DE-ESS, COMP, TONE) ha il pulsante IN; titoli dei riquadri in basso.
- Retro: schede SETTINGS, CREDITS e **MODES**; MODES elenca i valori imposti da ciascuna modalità ed è generata dalla stessa tabella dei preset usata dal codice.
