# The Stripper — Completamento: modello NAM, GUI, rifinitura

**Data:** 2026-10-09
**Stato:** bozza per revisione
**Plugin:** The Stripper (`src/the-stripper/`)

## 1. Scopo

Portare The Stripper da "DSP completo con lista parametri dell'host" a plugin utilizzabile: scelta del modello NAM da dentro il plugin, editor grafico, parametri di limiter e gate esposti, conformità ai requisiti di controllo esterno (SSL 360 Link) e documentazione allineata al codice.

Fuori scopo (decisione del 2026-10-09):
- **Gain staging** (parametri `kBypassGainStaging` e `kGainStagingTrim` restano, inerti, come oggi).
- **Tone3000**: richiede rete e account.
- Asset grafici definitivi: la GUI parte con controlli vettoriali di riserva (§4).

## 2. Componente condiviso: `shared/nam_drive`

`NamDrive` (oggi in `src/the-stripper/src/dsp/nam_drive.{h,cpp}`) si sposta in `shared/nam_drive/`, perché lo userà anche Doggystyle e i plugin non devono dipendere l'uno dall'altro.

- Libreria CMake `nam_drive` (compila anche le sorgenti `NeuralAmpModelerCore`, oggi elencate in `NAM_SOURCES`), che dipende da `gpu_nam`. Lo Stripper e Doggystyle la collegano.
- Namespace `FairyTapes::Shared::DSP`. Lo Stripper aggiorna gli include e le dichiarazioni d'uso.
- Il comportamento audio non cambia: i test esistenti dello Stripper restano verdi.

## 3. Caricamento del modello NAM

Oggi `NamDrive::loadModel(path)` è sincrono e non c'è alcuna interfaccia per sceglierlo.

- **Caricamento asincrono:** nuovo metodo `requestLoad(const std::filesystem::path&)`, chiamabile dal thread dei messaggi. Il modello si costruisce su un thread di lavoro e il thread audio lo adotta tra due blocchi con scambio atomico del puntatore. Il thread audio non alloca e non attende mai. Il modello sostituito viene rilasciato fuori dal thread audio.
- **Stato:** il percorso del file si salva nello stato del plugin (§6) e si ricarica in `setState`.
- **Errori:** file mancante, illeggibile o non supportato → Drive in passthrough (comportamento attuale senza modello), nome del modello vuoto nell'editor e messaggio breve di errore accanto al pulsante. Nessun crash.
- **Comunicazione processor ↔ controller:** il percorso passa con un `IMessage` dal controller al processor; lo stato di caricamento (nome del file o errore) torna con un messaggio inverso.

## 4. GUI custom (VSTGUI4)

Editor VSTGUI4 (`vendors/vst3sdk/vstgui4`), costruito in codice, senza file `.uidesc`.

- **Dimensioni:** 480 × 720 (1x), proporzioni 2:3 come da `docs/UI_ASSETS_SPEC.md`. Gli asset a 2x (960 × 1440) si caricano quando disponibili; la finestra è scalata dall'host.
- **Sezioni, dall'alto:** Globale (Bypass, Strip Mode, Input/Output Gain) · LP/HP · EQ (4 bande + Saturation) · Compression (Amount, Mix, Voice/Drums/Classic visibili solo in Channel) · Drive (Amount, Bypass, pulsante **Load .nam**, nome del modello) · Limiter (Pound) · Gate (Threshold).
- **Controlli:** manopole, pulsanti on/off e pulsanti a scelta. Ogni controllo ha un **disegno vettoriale di riserva**; se nella cartella degli asset esiste il PNG corrispondente (nomi da `UI_ASSETS_SPEC.md`), lo usa al posto del disegno.
- **Sfondo:** 2-3 varianti di texture. La variante è scelta una volta per istanza (casuale alla creazione) e salvata nello stato (§6); senza asset, tre toni di tinta unita.
- **Binding:** ogni controllo è legato a un `ParamID` e supporta automazione, annullamento e testo del valore fornito dal `Controller`.
- **Mix bus:** i controlli non pertinenti (Voice/Drums/Classic) si nascondono; non si ricostruisce l'editor.

## 5. Rifinitura e conformità

- **Nuovi parametri** (aggiunti **in coda** a `ParamID`, per non cambiare gli ID esistenti): `kLimiterPound` (0..1, ingresso `pound` di `BlockParty::setParams`) e `kGateThreshold` (0..1, ingresso `threshold` di `DeNoise::setParams`). `wet` di entrambi resta fisso a 1.0 e `speed` del gate al valore attuale.
- **Stabilità dei parametri:** test che fissa la tabella ID → nome → numero di passi → default di tutti i parametri, così un cambio accidentale dell'ordine fa fallire la build. Requisito per SSL 360 Link e per la compatibilità con i progetti salvati.
- **SSL 360 Link:** attività di ricerca dei requisiti ufficiali; il risultato va in una sottosezione di `docs/FEATURES.md §4` con fonti e data. Se non esistono requisiti pubblici, la sezione lo dichiara e si mantiene la regola attuale (VST3 rigoroso, ID stabili, nomi chiari).
- **Documentazione:** `docs/PARAMS_STATUS.md`, `docs/CONTROLS.md`, `docs/BUILD_AND_USE.md` e `README.md` del plugin allineati al codice (conteggio parametri, Drive, nuovi parametri, GUI, caricamento del modello).

## 6. Stato

`kStateVersion` passa da 7 a **8**. Aggiunte: stringa UTF-8 con lunghezza (percorso del modello, vuota se assente) e intero con la variante di texture (0..2). Lo stato v7 resta caricabile: percorso vuoto, variante 0.

## 7. Test

- Percorso asincrono di `NamDrive` con un modello `.nam` minimo di fixture: `requestLoad` + attesa → `isModelLoaded()`; file inesistente → passthrough senza crash; due `requestLoad` ravvicinati → vince l'ultimo.
- Round-trip dello stato v8 e caricamento di uno stato v7 di fixture.
- Tabella dei parametri bloccata (§5).
- Mapping dei due nuovi parametri.
- La GUI si verifica a mano in Reaper e Cubase (aprire/chiudere l'editor più volte, automazione, cambio di Strip Mode).

## 8. Rischi

- `NamDrive` alloca durante `loadModel`: il percorso asincrono deve garantire che nulla di tutto ciò avvenga sul thread audio. Verificato in revisione e con un test che chiama `process` mentre il caricamento è in corso.
- Il porting di `NamDrive` in `shared/` tocca la build di CUDA/`gpu_nam`: va controllata la build con e senza CUDA.
- VSTGUI4 aggiunge dipendenze di build (finestra, bitmap, file selector) per piattaforma; Linux/Windows/macOS verificati dalla CI esistente (`.github/workflows/build.yml`).

## 9. Dipendenze con altri piani

Doggystyle riusa `shared/nam_drive` e il caricamento asincrono: questo spec va implementato **prima** (almeno §2 e §3).
