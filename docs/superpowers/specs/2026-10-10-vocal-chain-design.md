# Vocal Chain — Design

**Data:** 2026-10-10
**Stato:** bozza per revisione
**Suite:** Fairy Tapes (channel strip vocale "tutto dentro")
**Nome:** provvisorio (`Vocal Chain`), da scegliere in tema con gli altri plugin

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

## 3. Segnale

1. **Input**.
2. **Rider:** RMS a finestra 100-300 ms (o LUFS a breve termine) confrontato col Target. Il guadagno si muove di ±Range dB con pendenza massima limitata (Speed). Resta fermo dove il Breath control rileva silenzio o respiro. Look-ahead 50-100 ms.
3. **Breath control:** abbassa i respiri di una quantità fissa (Amount), con look-ahead e isteresi; non chiude mai del tutto.
4. **EQ correttivo:** passa-alto, taglio del fango (200-400 Hz), notch dinamico per risonanze. Parametri interni dipendenti dalla modalità.
5. **De-esser:** a banda separata, con ascolto del solo segnale tagliato.
6. **Compressore stile Distressor:** rapporti selezionabili, modalità Dist 2/3 (armoniche pari/dispari), tempi programma-dipendenti. Possibile riuso di parti di drive/saturazione già presenti nel repo: da valutare.
7. **Tone:** una manopola che muove insieme taglio del fango, presenza e aria lungo una curva preimpostata per modalità.
8. **Output**.

## 4. Breath control: rilevamento

Interfaccia `BreathDetector`: in ingresso blocchi di audio, in uscita per ogni frame una probabilità di respiro in [0, 1]. Il riduttore usa solo questa uscita.

- **Speak e Sing (rilevatore spettrale):** per frame si calcolano sonorità (flatness spettrale; pitch tramite autocorrelazione o cepstrum), centroide spettrale, durata e contesto (tra due frasi, vicino a silenzio, non agganciato a una vocale). Voce bassa = armonica; respiro = rumoroso, centroide alto, nessun pitch.
- **Puke (rilevatore a livello):** l'analisi spettrale non vale, perché growl e scream sono rumorosi e senza pitch e verrebbero scambiati per respiri. Un respiro è un segmento circa 20 dB o più sotto il livello medio della frase, circondato da voce. Un'inalazione forte tra due urla viene al massimo accorciata, mai tagliata.

## 5. Parametri per modalità

| Blocco | Speak | Sing | Puke |
|---|---|---|---|
| Rider | medio, ±6 dB | lento, ±4 dB | quasi fermo, ±3 dB |
| De-esser | normale | normale | quasi spento |
| Compressore | rapporto basso | medio | alto, Dist attivo |
| Tone | più presenza | più aria | taglia il fango, tiene i medi |

Valori numerici da tarare con registrazioni reali (vedi "Decisioni aperte").

## 6. Parametri esposti

Bypass, Mode (Speak/Sing/Puke), Input, Rider Target, Rider Range, Breath Amount, De-esser Amount, Comp Amount, Tone, Output. Il resto è interno per modalità.

## 7. Struttura

`src/vocal-chain/` con lo stesso schema degli altri plugin (`plug.cpp`, `controller`, `processor`, `params.h`, `cids.h`, `dsp/`, `tests/`, `docs/`). Namespace `FairyTapes::VocalChain`. Aggiunto con `add_subdirectory` nel `CMakeLists.txt` della radice. Grafica: `docs/DESIGN_SYSTEM.md`; testo visibile in inglese.

## 8. Test

Ogni blocco ha un test che prima fallisce: Rider (guadagno verso il Target, nessun salto oltre la pendenza), Breath control (respiro sintetico abbassato, vocale bassa armonica intatta, rumore di growl intatto in Puke), De-esser, Tone. Latenza riportata all'host (somma di look-ahead).

## 9. Decisioni aperte

- Nome definitivo del plugin.
- Registrazioni di riferimento per tarare le soglie, soprattutto in Puke (growl e scream).
- Dati etichettati per il livello 2 (rete RTNeural).
- Riuso di drive/saturazione esistenti per il Distressor.
- Interfaccia grafica: ora da definire; mockup di riferimento `stripper_rack.html`.
