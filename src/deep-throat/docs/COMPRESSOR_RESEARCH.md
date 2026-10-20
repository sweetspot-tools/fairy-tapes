# Deep Throat — Ricerca sul compressore (stile Distressor)

**Data:** 2026-10-15. Appunti per il Task 8 del piano (`DistressorComp`). Fonti pubbliche, lette solo tramite ricerca web: i dettagli non confermati dal manuale sono segnati come tali. Il nome del dispositivo non compare nella UI del plugin.

## 1. Cosa fa l'hardware di riferimento

- **Rapporti:** 1:1, 2:1, 3:1, 4:1, 6:1, 10:1, 20:1 e Nuke. A 1:1 non comprime ma aggiunge armoniche. Il 10:1 ha un caso speciale (modo "Opto") con un rilevatore separato che imita i compressori a cellula ottica. Del Nuke le fonti descrivono il carattere (eccitazione esplosiva su stanza e mix), non il circuito.
- **Distorsione:** Dist 2 aggiunge soprattutto la 2ª armonica (circa 3%, carattere "a valvola"); Dist 3 la 3ª armonica (simmetrica, carattere "a nastro"), con risposta in frequenza ridotta su alti e bassi. L'entità va dallo 0,02% al 20% secondo il modo e il rilascio. **Dipende dalla compressione applicata e dall'attacco.**
- **Tempi:** attacco da 50 µs a circa 30-50 ms (le fonti non concordano); rilascio da 0,05 s a 3,5 s, fino a 20 s nel 10:1 Opto. Entrambi variano con il rapporto. Le manopole non hanno una scala in millisecondi.
- **Filtri passa-alto:** uno nel percorso audio (Bessel, circa 80 Hz) e uno nel rilevatore, per evitare il pompaggio dei bassi. Un pulsante LINK collega i due canali del rilevatore; i modi del rilevatore cambiano la risposta al programma.

## 2. Come si implementa in digitale

- Riferimento: Giannoulis, Massberg, Reiss, [*Digital Dynamic Range Compressor Design — A Tutorial and Analysis*](https://aes2.org/publications/elibrary-page/?id=16354) (JAES 60, 6, 2012). Raccomandano la topologia **feedforward** (stabile e prevedibile in digitale) e il rilevatore **nel dominio logaritmico, dopo il calcolo del guadagno**: inviluppo liscio, nessun ritardo d'attacco, ginocchio regolabile. Il testo completo è riservato agli iscritti AES.
- Implementazioni di riferimento con codice: la [libreria compressori di Faust](https://faustlibraries.grame.fr/libs/compressors/) e l'[oggetto `compressor` di MATLAB](https://au.mathworks.com/help/audio/ref/compressor.html) citano questo articolo.
- Il "rilascio programma-dipendente" (due stadi, veloce e lento) è quello che avevamo già nel piano; non ho trovato una descrizione pubblica del metodo esatto dell'hardware.

## 3. Altre emulazioni (solo come informazione)

- Quella di Universal Audio nasce dal rilievo completo del circuito di un esemplare di riferimento più tre unità di epoche diverse. Quella di Slate è su licenza del costruttore. Nessuna ha il sorgente.
- Thistressor (23DSP) descrive un'architettura parallela con distorsione asimmetrica a diodi, pre-distorsione e post-distorsione scalata dal rapporto attorno allo stadio di compressione. Non ha sorgente pubblico (vedi spec §2).
- Non ho trovato alcuna fonte su come si modella in DSP il VCA dell'hardware originale. Cercarla su KVR (sezione DSP) o nelle liste Music-DSP.

## 4. Cosa cambia per il nostro `DistressorComp` (punti 1-5 recepiti in spec e piano, Task 8; il 6 resta una pratica di taratura)

1. **Rilevatore con passa-alto** (selezionabile OFF, 80 o 150 Hz; 80 Hz a default): oggi il piano non lo prevede e sulla voce serve contro il pompaggio da plosive e fiato.
2. **Distorsione legata alla riduzione di guadagno e all'attacco**, non a un drive fisso: Dist 2 cresce con la compressione, Dist 3 con risposta ridotta agli estremi.
3. **Ratio:** aggiungere 20:1 alla lista (oggi: AUTO, 2, 3, 4, 6, 8, 10). Il "Nuke" e il modo Opto 10:1 sono fuori scopo per ora.
4. **Tempi:** attacco da 0,05 a 30 ms e rilascio da 50 ms a 3,5 s, con la scala che dipende dal rapporto.
5. **Struttura feedforward con rilevatore logaritmico** come da Giannoulis et al. (il piano dice già feedforward e ginocchio morbido di 6 dB).
6. **Tarare a orecchio e con misure** (rumore rosa in ingresso, attacco misurato) perché le manopole dell'originale non hanno una scala in ms.

## 5. Fonti

- [Manuale del Distressor (copia su barryrudolph.com)](https://www.barryrudolph.com/recall/manuals/distressor1.pdf) e [altra copia](https://azur.novelty.fr/download/materiel_manuels/empirical_labs_distressor_manuel.pdf).
- [Manuale EL8 Distressor di Universal Audio](https://help.uaudio.com/hc/en-us/articles/18741515014676-Empirical-Labs-EL8-Distressor-Manual).
- [Sound On Sound sul Distressor](https://www.soundonsound.com/node/4922601) e [Mixdown, Gear Icons](https://mixdownmag.com.au/features/gear-icons-empirical-labs-distressor/).
- [Intervista a Dave Derr, Tape Op](https://tapeop.com/interviews/33/dave-derr).
- [Forum: tempi d'attacco del Distressor](https://gearspace.com/threads/distressor-attack-times.1050264/).
