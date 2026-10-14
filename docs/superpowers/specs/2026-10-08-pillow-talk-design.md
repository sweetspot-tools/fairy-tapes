# Pillow Talk — Design

**Data:** 2026-10-08
**Stato:** bozza per revisione
**Suite:** Fairy Tapes (terzo plugin, dopo The Stripper e Doggystyle)

## 1. Scopo

Delay analogico (tape echo) che si "autoautomatizza" sulla voce: la mandata del delay si abbassa (o sparisce) mentre la voce canta e si apre nelle pause e a fine frase, lasciando le code. Elimina le automazioni manuali di mandata che servono a evitare che il delay impasti la voce.

Criteri di successo:
- Voce definita durante le frasi, code udibili nelle pause, senza automazioni.
- Suono tape realistico, armonico, coerente con lo spirito analogico della suite.
- Funziona in Reaper e Cubase, sia come insert sulla voce sia su un bus di ritorno con sidechain esterno.

Il nome (Pillow Talk) richiama il sussurro dopo la fine dell'"atto": il delay parla solo quando la voce ha finito. Tono irriverente in linea con la suite.

## 2. Struttura del progetto

`src/pillow-talk/`, stessa organizzazione di The Stripper:

- `plug.cpp`, `controller.{h,cpp}`, `processor.{h,cpp}`, `params.h`, `cids.h`, `Info.plist`
- `dsp/` — DSP senza JUCE (solo VST3 SDK e C++ standard)
- `tests/`, `docs/`, `CMakeLists.txt`

Aggiunto a `CMakeLists.txt` di suite con `add_subdirectory(src/pillow-talk)`. Produce `PillowTalk.vst3` in `dist/`.

Riferimento DSP: `vendors/dusk-audio-plugins/plugins/tape-echo` (GPL-3, `TapeEcho.h`, `WowFlutter.h`, `TapeSaturation.h`). Dipende da JUCE, quindi il DSP è riscritto senza JUCE seguendone la struttura, non incluso. Licenza invariata (GPL-3.0).

## 3. Segnale

1. **Ingresso principale** stereo.
2. **Sidechain opzionale** stereo (secondo bus di ingresso VST3). Se non è collegato, il rilevatore usa l'ingresso principale.
3. **Rilevatore:** filtro passa-banda sulla gamma vocale, envelope follower, soglia, hold, release.
4. **Gain di ducking:** applicato **all'uscita wet** del delay, prima del mix; l'ingresso del delay resta sempre aperto. La frase viene registrata sul nastro mentre la voce canta; la coda già nella linea continua a decadere e si sente quando il gate si riapre (dopo hold e release).
5. **Tape echo.**
6. **Mix wet/dry** in parallelo.

## 4. Rilevatore e ducking

- Il gain di ducking agisce sull'uscita wet: durante la voce il ritorno del delay è ridotto di Depth, ma la linea continua a registrare e a decadere.
- Dopo l'ultimo istante sopra soglia, il rilevatore mantiene il gain ridotto per il tempo di **Hold**.
- Hold è espresso in **ms** o in **note** (1/16 – 1/2) calcolate dal BPM dell'host. Senza BPM dall'host si usa 120.
- Terminato l'hold, il gain risale con una **curva esponenziale** governata da Release.
- **Ducking depth** definisce la riduzione durante la voce: da pochi dB fino a muto totale. A depth minima il plugin si comporta come un delay normale.
- Il gain di ducking è smussato per evitare click e pompaggi.

## 5. Tape echo

- Linea di ritardo con interpolazione a 4 punti (tipo Hermite).
- Tempo modulato da LFO lenta (wow) più componente veloce e leggermente casuale (flutter).
- Loop di feedback: HP (circa 100 Hz), LP regolabile da Tone, saturazione `tanh` con bias leggero. Ogni ripetizione risulta più scura e compressa.
- Cambio di Time con glide, che produce il pitch-bend tipico del nastro senza click.
- Oversampling 2x solo sullo stadio di saturazione.
- Feedback limitato a un massimo sicuro, per evitare l'autoscillazione incontrollata.

## 6. Parametri

Time (ms/nota), Feedback, Tone, Saturation, Wow/Flutter, Mix, Threshold, Ducking depth, Hold (ms/nota), Release, Sidechain on/off.

Lo stato dei parametri si salva e si ricarica con il progetto.

## 7. Casi limite

- Sidechain collegato ma silenzioso: il delay resta aperto.
- Host senza BPM: default 120 BPM per i valori in note.
- Sample rate e dimensione del blocco variabili: i buffer si ricalcolano senza click.

## 8. Test

Seguendo `src/the-stripper/tests/test_params.cpp`:

- Parametri: valori, range, mapping.
- Rilevatore: soglia e hold su segnali sintetici (impulso, burst, silenzio).
- Gain di ducking: scende durante il burst, sale dopo hold e release; nella pausa si sentono le ripetizioni della frase.
- Delay: la prima ripetizione cade al tempo giusto, il feedback decade.
- Sidechain: con ingresso esterno collegato, il rilevatore usa quello.

## 9. Fuori scopo (per ora)

- Modalità BBD o delay digitale pulito (possibile estensione futura).
- Rilevamento di frase "intelligente" (scartato: meno prevedibile, più difficile da tarare).
- Interfaccia grafica personalizzata: segue la stessa strategia di The Stripper e sarà definita nel piano.

## 10. Decisioni prese in implementazione

- **Ducking sull'uscita wet**, non sull'ingresso del nastro (vedi §3 e §4): con l'ingresso abbassato la frase non veniva registrata e nelle pause non restava eco.
- **Rilevatore:** rilascio dell'envelope a 3 ms con compensazione di 8 ms sull'hold, così l'hold parte dall'ultimo istante sopra soglia indipendentemente dal livello; passa-alto del 4° ordine a 120 Hz.
- **Stato:** i valori non finiti si rifiutano e quelli ripristinati si limitano a 0..1.
- **Modulo VST3:** factory con le macro del SDK, target `MODULE`, layout dei bundle come nei moduli CMake del SDK.
- Il glide del Time non ha limite di slew e Saturation a 0 applica comunque `tanh` (comportamento da nastro, accettato).

## 11. Decisioni aperte

- **Default di Hold (224 ms), Release (316 ms) e Feedback (38 %):** con questi valori la coda della frase nella pausa è molto attenuata (circa -22 dB sull'intera pausa). Da tarare ad orecchio in un host.
- **Non verificato:** build e bundle su Windows e macOS (la prima build vera è la CI), caricamento in Reaper e Cubase, attivazione del sidechain nei due host.
- Il test della factory è registrato anche su macOS, dove fallirebbe (cerca `ModuleEntry`, macOS esporta `bundleEntry`).
- Rilevamento di frase "intelligente" e modalità BBD restano fuori scopo.

