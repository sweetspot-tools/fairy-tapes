# Deep Throat

Channel strip VST3 per voci, della suite Fairy Tapes. Catena fissa e pochi controlli: si mette sulla traccia voce, si sceglie la modalità e si regolano le manopole. Ingresso e uscita stereo (accettato anche mono → mono).

Documenti: spec `docs/superpowers/specs/2026-10-10-deep-throat-design.md`, piano `docs/superpowers/plans/2026-10-10-deep-throat.md`, ricerca sul compressore `src/deep-throat/docs/COMPRESSOR_RESEARCH.md`.

## Modalità

- **Speak:** voce parlata (podcast, voice-over). Rilevatore di respiri spettrale.
- **Sing:** voce cantata. Rilevatore di respiri spettrale.
- **Puke:** voci estreme (scream, growl). Rilevatore di respiri a livello (nessuna analisi spettrale, perché growl e scream sono rumorosi e senza pitch).

La modalità cambia i valori interni di tutta la catena:

| Blocco | Speak | Sing | Puke |
|---|---|---|---|
| Rider (range massimo, pendenza) | ±6 dB, 8 dB/s | ±4 dB, 4 dB/s | ±3 dB, 2 dB/s |
| EQ: passa-alto, taglio a 300 Hz | 80 Hz, -2 dB | 70 Hz, -3 dB | 60 Hz, -4 dB |
| De-esser | normale | normale | Amount × 0.15 |
| Compressore (AUTO) | 2:1, nessuna distorsione | 3:1, Dist 2 | 8:1, Dist 3 |
| Tone | più presenza | più aria | taglia il fango, tiene i medi |

## Catena del segnale

Input → Rider → Breath control → EQ correttivo → De-esser → Compressore → Tone → Output.

- **Rider:** livellatore automatico del volume, solo sul livello (RMS a finestra 200 ms), mai sul pitch. Porta il livello verso il Target muovendo il guadagno di al massimo ±Range dB con pendenza limitata. Resta fermo dove il Breath control rileva un respiro e con livello sotto -60 dB.
- **Breath control:** riduttore di respiri, non un gate: riduce fino a 12 dB × Amount (attacco 5 ms, rilascio 30 ms) in proporzione alla probabilità di respiro. Il rilevatore lavora sul mono (L+R)/2, quindi il guadagno è lo stesso sui due canali.
- **EQ correttivo:** passa-alto (12 dB/ott), taglio del fango a 300 Hz (Q 1) e notch dinamico (Q 8, fino a 6 dB) che segue la risonanza più prominente persistente tra 1 e 6 kHz. EQ Amount scala il tutto.
- **De-esser:** a banda separata, crossover Linkwitz-Riley a Freq, rilevatore sul mono linkato; riduce solo la banda alta (4:1, fino a 12 dB × Amount).
- **Compressore** (stile Distressor, nome mai mostrato nella UI): feedforward con rilevatore logaritmico e passa-alto sul solo rilevamento, ginocchio morbido 6 dB, tempi programma-dipendenti legati al rapporto, distorsione che cresce con la riduzione di guadagno e con l'attacco veloce, Mix parallelo.
- **Tone:** una manopola su tre filtri (fango 250 Hz, presenza 3,5 kHz, aria shelf 12 kHz) con curva per modalità; 0.5 = neutro.

A IN spento un modulo lascia passare il segnale (latenza invariata). Tutti i moduli continuano a girare in background, così riattivarli non produce salti; IN, Bypass e i due Listen sfumano in 5 ms.

## Parametri (25)

Tutti automatizzabili. I valori normalizzati sono 0..1.

| Nome | Intervallo | Default | Funzione |
|---|---|---|---|
| Bypass | on/off | off | Esce l'ingresso ritardato (latenza invariata) |
| Mode | Speak / Sing / Puke | Speak | Sceglie i valori interni e il rilevatore |
| Input | -12..+12 dB | 0 dB | Guadagno d'ingresso |
| Rider Target | -30..-10 dBFS | -20 dBFS | Livello verso cui il Rider porta la voce |
| Rider Range | 0..100% del massimo della modalità | 50% | Escursione massima del Rider |
| Breath | 0..100% | 50% | Riduzione dei respiri, fino a 12 dB |
| Breath Sensitivity | 0..100% | 50% | Più alto = rileva anche respiri più deboli |
| Breath Listen | on/off | off | Ascolta solo ciò che il riduttore toglie |
| EQ Amount | 0..100% | 50% | Scala passa-alto (× (0.5 + Amount)), taglio del fango e notch (× 2·Amount); 0 = taglio e notch spenti |
| De-ess | 0..100% | 50% | Riduzione massima 12 dB × Amount |
| De-ess Freq | 4..10 kHz (log) | 6,5 kHz | Frequenza del crossover e del rilevatore |
| De-ess Listen | on/off | off | Ascolta solo la banda tolta |
| Compressor | 0..100% | 50% | Soglia -6 - 24·Amount dBFS, con compensazione del guadagno |
| Comp Mix | 0..100% | 100% | Parallelo: 0 = solo segnale d'ingresso del compressore |
| Comp Ratio | AUTO, 2, 3, 4, 6, 8, 10, 20 :1 | AUTO | AUTO usa il valore della modalità |
| Comp Dist | AUTO, OFF, Dist 2, Dist 3 | AUTO | Armoniche pari (Dist 2) o dispari (Dist 3) |
| Comp SC HPF | OFF, 80 Hz, 150 Hz | 80 Hz | Passa-alto sul solo rilevamento |
| Tone | 0..100% | 50% | Neutro al centro; sotto = più caldo, sopra = più presenza/aria |
| Output | -12..+12 dB | 0 dB | Guadagno d'uscita |
| Rider In | on/off | on | Include il modulo |
| Breath In | on/off | on | Include il modulo |
| EQ In | on/off | on | Include il modulo |
| De-ess In | on/off | on | Include il modulo |
| Comp In | on/off | on | Include il modulo |
| Tone In | on/off | on | Include il modulo |

Breath Listen ha la precedenza su De-ess Listen. Breath Listen funziona anche con Breath In spento.

## Latenza

80 ms di look-ahead più un frame del rilevatore spettrale (`nextPow2(20 ms · sample rate)` campioni), uguale in tutte le modalità (in Puke il rilevatore a livello ha latenza 0 e riceve più look-ahead). Il valore è riportato all'host, che compensa; con Bypass e IN spenti non cambia. Un cambio di sample rate cambia la latenza e l'host la rilegge dopo `setupProcessing` (non c'è una notifica di riavvio).

## Costruire

Fuori dal repo (il disco `/srv/http` è piccolo):

```
cmake -S . -B /tmp/vc-build -DCMAKE_BUILD_TYPE=Release -DGPU_NAM_DISABLE_CUDA=ON
cmake --build /tmp/vc-build --target deep_throat_tests -j2
ctest --test-dir /tmp/vc-build -R deep_throat --output-on-failure
cmake --build /tmp/vc-build --target deep_throat -j2
```

Il bundle è `DeepThroat.vst3` (in `dist/` per la CI). Non costruire tutti i target insieme.

## Test

`tests/test_deep_throat.cpp` (harness `RUN_TEST`/`ASSERT_TRUE`, segnali sintetici) e `tests/test_factory.cpp` (carica il bundle, SKIP se manca). Coprono:

- parametri: numero, intervalli, scalini delle scelte (coerenti con la `StringListParameter` dell'SDK), default, salvataggio e ripristino dello stato, rifiuto di versioni sbagliate e NaN;
- ogni blocco: Rider, FFT, rilevatori di respiri (spettrale e a livello, sensibilità, silenzio, growl e inalazione forte in Puke), riduttore, EQ (passa-alto, fango, notch dinamico e sua persistenza), de-esser (Freq, Listen, collegamento stereo, Puke), compressore (rapporti, tempi, passa-alto, distorsione, compensazione, Mix, input estremi), Tone, preset;
- engine: valori finiti e silenzio esatto, blocchi da 0 e 1 campione, mono, latenza e Bypass, respiro ridotto end to end, bordi del respiro coperti dal look-ahead, cambio di modalità, sample rate 44,1-192 kHz, tutti gli IN spenti = passthrough, click dei toggle, Listen, ordine della catena contro un riferimento, nessuna allocazione in `process`.

## Limiti noti

- **Soglie tarate su segnali sintetici**, non su voci reali, growl e scream soprattutto. Vanno ritarate con registrazioni di riferimento (spec §9).
- **Nessuna GUI custom:** l'host mostra la lista dei parametri. Layout in anteprima in `src/deep-throat/docs/mockups/deep_throat_rack.html` (spec §10); l'implementazione è un piano a parte.
- **Rilevamento dei respiri:**
  - l'inizio di un respiro è riconosciuto con un ritardo di circa 60-80 ms (durata minima): in Puke la riduzione parte circa 30 ms dopo l'inizio del respiro; in Speak/Sing parte circa 25 ms prima e può intaccare la coda della frase precedente;
  - il primo respiro di una ripresa non viene rilevato (i rilevatori hanno bisogno del contesto di frase);
  - sibilanti contro respiri: non provato su voce reale;
  - un tono tenuto perfettamente dritto può attivare il notch dell'EQ dopo 300 ms (profondità massima 6 dB × 2·EQ Amount);
  - in Puke un respiro più lungo di 300 ms (contesto del rilevatore) viene rilasciato prima della fine;
  - dopo un cambio di modalità che cambia rilevatore, il nuovo rilevatore ha bisogno di qualche centinaio di ms di frase prima di rilevare di nuovo;
  - il rilascio di 30 ms del riduttore lascia circa 4 dB di riduzione 25 ms dopo la fine del respiro, quindi all'inizio della frase successiva.
- **Compressore:** non è un modello del circuito originale (nessuna fonte pubblica sul VCA); tempi e distorsione sono tarati a orecchio e con misure sintetiche. Con Puke e Amount 0.5 il livello sotto soglia è circa 0,6 dB sopra l'unità. Un limitatore di sicurezza morbido sopra 0 dBFS sul percorso trattato mantiene l'uscita sotto 2.
- **Sample rate bassi (22,05 kHz):** i bordi del rilevatore del de-esser e la shelf "aria" del Tone sono limitati a 0,45 × sample rate; il de-esser lavora in una banda ridotta e l'aria non arriva a 12 kHz. Verificato solo con test di fumo.
- Layout: stereo e mono → mono; altre disposizioni non sono previste.
