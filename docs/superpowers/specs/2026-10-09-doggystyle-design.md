# Doggystyle — Design

**Data:** 2026-10-09 (rivisto lo stesso giorno: motori dedicati al posto di FET+Opto dello Stripper)
**Stato:** bozza per revisione
**Suite:** Fairy Tapes (secondo plugin; sviluppato dopo Pillow Talk e il completamento dello Stripper)
**Concept originale:** `src/doggystyle/docs/CONCEPT.md` (nome non definitivo; qui si usa Doggystyle)

## 1. Scopo

Compressione parallela aggressiva per gruppi di batteria e per basso+chitarra in rock, metal e grindcore: plugin da mettere su un gruppo per "prendere a schiaffi l'ascoltatore". Due rami in parallelo con caratteri opposti, più uno stadio NAM opzionale in coda per saturazione.

- **Slap:** compressione FET-style veloce e brutale, fino al "tutti i pulsanti" (circa 20:1). Schiaccia e fa uscire i transienti.
- **Thump:** compressione VCA pesante con attacco lento, che lascia passare il colpo di cassa e rullante e aggiunge corpo e densità con saturazione a valvola.

Decisioni (2026-10-09):
- **Solo parallelo** (niente switch seriale).
- **Motori dedicati** scritti per Doggystyle. I motori di The Stripper non vanno bene qui: il suo FET ha ratio fissa a 4:1 e attacco/rilascio bloccati a metà corsa, e l'Opto è morbido per definizione. Dell'emulazione hardware di Dusk Audio (GPL-3.0: trasformatore e valvola) si riusa solo il colore.
- **NAM su GPU in coda**, opzionale e bypassabile.
- Nessuna GUI custom: lista parametri dell'host.

Successo: sul gruppo batteria si regola il carattere con pochi controlli (quanto Slap, quanto Thump, bilanciamento, mix col segnale originale) e il risultato è denso e aggressivo senza perdere i transienti.

## 2. Struttura

`src/doggystyle/`, stesso schema di The Stripper: `plug.cpp`, `controller.{h,cpp}`, `processor.{h,cpp}`, `params.h`, `cids.h`, `Info.plist`, `dsp/`, `tests/`, `docs/`, `CMakeLists.txt`. Aggiunto con `add_subdirectory(src/doggystyle)` in `CMakeLists.txt` della radice. Bundle `Doggystyle.vst3` in `dist/`. Namespace `FairyTapes::Doggystyle`.

## 3. Componenti condivisi

- **`shared/hardware_emulation/`** (nuovo, solo header): `TransformerEmulation.h`, `TubeEmulation.h`, `HardwareProfiles.h`, `WaveshaperCurves.h` estratti da `src/the-stripper/src/dsp/hardware_emulation/`. Libreria CMake `hardware_emulation` di tipo `INTERFACE`; namespace `FairyTapes::Shared::HardwareEmulation`. Lo Stripper la usa al posto dei file locali; il suo suono e i suoi test non cambiano. I motori `MultiComp*` dello Stripper **non** si toccano.
- **`shared/nam_drive/`**: creato dal piano del completamento dello Stripper (spec `2026-10-09-the-stripper-completion-design.md` §2-3), con caricamento asincrono. Doggystyle lo usa invariato.

Questo spec dipende dai §2-3 dello spec dello Stripper.

## 4. Segnale

1. **Input** (-12..+12 dB).
2. **Detector HP:** passa-alto a 12 dB/ott (20..250 Hz, default 80 Hz) applicato **solo al segnale di rilevamento** di entrambi i rami, mai all'audio. Impedisce che la cassa o il basso facciano pompare tutto il gruppo.
3. **Due rami in parallelo**, ciascuno con il segnale di ingresso:
   - **Slap (FET-style):** il rilevatore è un picco sul segnale filtrato. Input gain in un trasformatore (profilo FET 1176) da 0 a +30 dB secondo Amount. Soglia fissa. Ratio a scelta fra 4, 8, 12, 20, **All**: All = 20:1 con attacco e rilascio dimezzati e uscita del trasformatore più spinta. Attacco 0.02-0.8 ms, rilascio 20-1100 ms (scale logaritmiche).
   - **Thump (VCA pesante):** feedforward, rilevatore RMS+picco, soglia da -6 a -30 dB secondo Amount, ratio 8:1 con ginocchio morbido di 6 dB, attacco 5-60 ms, rilascio 40-800 ms a due stadi (programma-dipendente). Dopo la compressione, stadio a valvola (triodo 12BH7) con drive legato ad Amount.
   - **Compensazione di livello:** ogni ramo compensa il proprio guadagno in modo che un segnale stazionario a -18 dBFS RMS esca entro ±3 dB dal livello d'ingresso per qualunque Amount.
4. **Blend:** crossfade a potenza costante fra i due rami (0 = solo Slap, 1 = solo Thump). Guadagni `gSlap = cos(blend·π/2)`, `gThump = sin(blend·π/2)`.
5. **Mix:** miscela segnale originale (dry, dopo l'input gain) e compresso (wet).
6. **NAM Drive (opzionale):** `NamDrive` di `shared/nam_drive`, bypassabile.
7. **Output** (-12..+12 dB).

I rami non introducono latenza, quindi dry e wet restano allineati. Il NAM sta dopo la miscela e la sua latenza su GPU è riportata all'host con `getLatencySamples()`.

## 5. Parametri (15)

| Parametro | Range | Default |
|-----------|-------|---------|
| Bypass | 0/1 | 0 |
| Input | -12..+12 dB | 0 dB |
| Slap Amount | 0..1 | 0.5 |
| Slap Ratio | 5 scelte: 4, 8, 12, 20, All | 12 |
| Slap Attack | 0.02-0.8 ms | 0.3 (norm.) |
| Slap Release | 20-1100 ms | 0.35 (norm.) |
| Thump Amount | 0..1 | 0.4 |
| Thump Attack | 5-60 ms | 0.4 (norm.) |
| Thump Release | 40-800 ms | 0.5 (norm.) |
| Detector HP | 20-250 Hz | 80 Hz |
| Blend | 0..1 (Slap → Thump) | 0.5 |
| Mix | 0..1 | 1.0 |
| NAM Bypass | 0/1 | 1 (bypassato) |
| NAM Drive | 0..1 | 0.3 |
| Output | -12..+12 dB | 0 dB |

Il modello NAM si carica come nello Stripper (spec dello Stripper §3), ma senza editor. Finché la GUI non esiste, il percorso si imposta con un file `doggystyle.json` accanto al plugin (chiave `nam_model`); il percorso scelto si salva comunque nello stato del progetto.

## 6. Stato

`kStateVersion = 1`: versione, `kNumParams` valori `double`, percorso del modello (stringa UTF-8 con lunghezza).

## 7. Casi limite

- Nessun modello NAM o NAM bypassato: segnale invariato dopo il Mix.
- Sample rate variabile: motori e filtri si ricalcolano in `setActive`.
- Blocchi da 0 o 1 campione: nessun crash.
- Entrambi gli Amount a 0: il wet resta entro ±3 dB dal dry (compensazione di livello); nessun NaN.
- Ingresso mono: i due canali restano identici in uscita.
- Ingresso a fondo scala o silenzio digitale: nessun NaN né denormali.

## 8. Test

- Parametri: range, default, mapping, ordine dell'enum.
- Legge del blend: potenza costante (±0.05 dB) per 0, 0.25, 0.5, 0.75, 1.
- Slap: più ratio → più riduzione; attacco più veloce → picco in uscita più basso; compensazione di livello a Amount 0, 0.5, 1.
- Thump: attacco lento lascia passare più picco di attacco veloce; più Amount → più riduzione; compensazione di livello.
- Detector HP: una sinusoide a 40 Hz produce meno riduzione con HP a 250 Hz che a 20 Hz.
- `ParallelComp`: Mix 0 = dry; Blend 0 = `SlapComp` da solo; Blend 1 = `ThumpComp` da solo; su clic a 120 BPM, Slap lascia un picco più basso di Thump; mono resta mono.
- Stato: round-trip e rifiuto di versioni sconosciute.
- Dopo l'estrazione in `shared/hardware_emulation`, i test dello Stripper restano verdi.

## 9. Fuori scopo

- Modalità seriale e switch serie/parallelo.
- GUI custom.
- Sidechain esterno.
- Motori Opto, VCA e Bus dello Stripper.

## 10. Decisioni aperte

- **SET per la release (proposto 2026-10-24, da decidere):** lo schema comune di `docs/DESIGN_SYSTEM.md` ("Taratura guidata della release") applicato ai due rami: SET fa sentire solo il ramo che si sta tarando (Slap o Thump), lo schiaccia e blocca tutto tranne la sua Release. Da decidere se un SET per ramo o uno solo con la scelta del ramo.
