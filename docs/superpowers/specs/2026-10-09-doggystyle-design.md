# Doggystyle — Design

**Data:** 2026-10-09
**Stato:** bozza per revisione
**Suite:** Fairy Tapes (secondo plugin; sviluppato dopo Pillow Talk e il completamento dello Stripper)
**Concept originale:** `src/doggystyle/docs/CONCEPT.md` (nome non definitivo; qui si usa Doggystyle)

## 1. Scopo

Compressione parallela con due compressori **diversi** per gruppi di batteria e per basso+chitarra in rock, metal e grindcore: plugin da mettere su un gruppo per "prendere a schiaffi l'ascoltatore". Il FET dà punch e aggressività, l'Opto dà corpo e glue. In coda, uno stadio NAM opzionale per saturazione.

Decisioni (2026-10-09):
- **Solo parallelo** (niente switch seriale).
- **FET + Opto** dai motori Multi-Comp di Dusk Audio (GPL-3.0), già portati nello Stripper.
- **NAM su GPU in coda**, opzionale e bypassabile.
- Nessuna GUI custom: lista parametri dell'host, come lo Stripper di oggi.

Successo: sul gruppo batteria il carattere si regola con pochi controlli (quanto FET, quanto Opto, bilanciamento, quanto mix col segnale originale) senza perdere i transienti.

## 2. Struttura

`src/doggystyle/`, stesso schema di The Stripper: `plug.cpp`, `controller.{h,cpp}`, `processor.{h,cpp}`, `params.h`, `cids.h`, `Info.plist`, `tests/`, `docs/`, `CMakeLists.txt`. Aggiunto con `add_subdirectory(src/doggystyle)` in `CMakeLists.txt` della radice. Bundle di output `Doggystyle.vst3` in `dist/`. Namespace `FairyTapes::Doggystyle`.

## 3. Componenti condivisi

Per evitare dipendenze tra plugin il codice comune va in `shared/`:

- **`shared/multicomp/`** (nuovo): `multicomp_engines.{h,cpp}` e `hardware_emulation/` estratti da `src/the-stripper/src/dsp/`. Libreria CMake `multicomp`; namespace `FairyTapes::Shared::DSP`. Lo Stripper la collega al posto dei file locali; il suo comportamento audio non cambia e i suoi test restano verdi.
- **`shared/nam_drive/`**: creato dal piano del completamento dello Stripper (spec `2026-10-09-the-stripper-completion-design.md` §2-3), incluso il caricamento asincrono del modello. Doggystyle lo usa invariato.

L'estrazione di `multicomp` è il primo task del piano. Questo spec dipende dai §2-3 dello spec dello Stripper.

## 4. Segnale

1. **Input gain** (-12..+12 dB).
2. **Due rami in parallelo**, ciascuno con il segnale di ingresso:
   - **FET:** `MultiCompFET`, con `amount` = FET Amount e `mix` = 1.
   - **Opto:** `MultiCompOpto`, con `amount` = Opto Amount e `mix` = 1.
3. **Blend:** crossfade tra le uscite dei due rami a **potenza costante** (0 = solo FET, 1 = solo Opto, 0.5 = pari merito).
4. **Mix:** miscela segnale originale (dry) e compresso (wet), con wet a 1.0 come default. Il dry è quello dopo l'input gain.
5. **NAM Drive (opzionale):** `NamDrive` di `shared/nam_drive`, bypassabile.
6. **Output gain** (-12..+12 dB).

Il Mix è in parallelo, quindi il dry deve essere allineato in fase ai rami compressi. I motori FET e Opto non introducono latenza; il NAM sta dopo la miscela, quindi non richiede di allineare il dry. La **latenza del NAM su GPU** è riportata all'host con `getLatencySamples()`, come fa lo Stripper.

## 5. Parametri

| Parametro | Range | Default |
|-----------|-------|---------|
| Bypass | 0/1 | 0 |
| Input | -12..+12 dB | 0 dB |
| FET Amount | 0..1 | 0.5 |
| Opto Amount | 0..1 | 0.3 |
| Blend | 0..1 (FET→Opto) | 0.5 |
| Mix | 0..1 | 1.0 |
| NAM Bypass | 0/1 | 1 (bypassato) |
| NAM Drive | 0..1 | 0.3 |
| Output | -12..+12 dB | 0 dB |

Il modello NAM si sceglie come nello Stripper (§3 dello spec dello Stripper), ma senza editor: il controller espone il percorso come parametro di stato e il file si imposta da un messaggio del controller. Finché la GUI non esiste, l'impostazione avviene tramite un file di configurazione `doggystyle.json` accanto al plugin con la chiave `nam_model`; il percorso scelto si salva comunque nello stato del progetto.

## 6. Stato

`kStateVersion = 1`: versione, valori dei parametri come `double`, percorso del modello (stringa UTF-8 con lunghezza).

## 7. Casi limite

- Nessun modello NAM caricato o NAM bypassato: segnale invariato dopo il Mix.
- Sample rate variabile: i motori si ricalcolano in `setActive`.
- Blocchi da 0 o 1 campione: nessun crash.
- Entrambi gli Amount a 0: il wet coincide con il dry (nessuna compressione).
- Ingresso mono: le due metà stereo si trattano in modo identico, senza panning spurio.

## 8. Test

- Parametri: range, default, mapping.
- Crossfade a potenza costante: potenza totale costante (±0.1 dB) per Blend 0, 0.25, 0.5, 0.75, 1 su rumore bianco.
- Mix = 0 → uscita identica al dry (fino all'input gain).
- Amount = 0 su entrambi → il wet coincide con il dry.
- Con ingresso a transienti (burst di clic): il ramo FET riduce il picco più dell'Opto a parità di Amount.
- Stato: round-trip e rifiuto di versioni sconosciute.
- Dopo l'estrazione in `shared/multicomp`, i test dello Stripper devono restare verdi.

## 9. Fuori scopo

- Modalità seriale e switch serie/parallelo.
- GUI custom.
- Sidechain esterno (i motori lo supportano, ma non è richiesto in questo caso d'uso).
- Altri tipi di compressore (VCA, Bus).
