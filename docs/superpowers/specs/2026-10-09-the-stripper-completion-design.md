# The Stripper — Completamento: modello NAM, GUI, rifinitura

**Data:** 2026-10-09
**Stato:** bozza per revisione
**Plugin:** The Stripper (`src/the-stripper/`)

## 1. Scopo

Portare The Stripper da "DSP completo con lista parametri dell'host" a plugin utilizzabile: scelta del modello NAM da dentro il plugin, editor grafico, parametri di limiter e gate esposti, conformità ai requisiti di controllo esterno (SSL 360 Link) e documentazione allineata al codice.

Fuori scopo (decisione del 2026-10-09):
- **Gain staging** (parametri `kBypassGainStaging` e `kGainStagingTrim` restano, inerti, come oggi).
- **Tone3000**: richiede rete e account.
- Asset grafici disegnati a mano: la GUI è disegnata dal codice (§4); i PNG manuali sono solo un'opzione futura.

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

- **Dimensioni:** 480 × 780, per fare spazio alla fascia dei meter (§4.1). `docs/UI_ASSETS_SPEC.md` (480 × 720) è superato da questa scelta. Il disegno è vettoriale e la finestra è scalata dall'host, quindi non servono asset separati a 2x.
- **Sezioni, dall'alto:** fascia dei meter (§4.1) · Globale (Bypass, Strip Mode, Input/Output Gain) · LP/HP · EQ (4 bande + Saturation) · Compression (Amount, Mix, Voice/Drums/Classic visibili solo in Channel) · Drive (Amount, Bypass, pulsante **Load .nam**, nome del modello) · Limiter (Pound) · Gate (Threshold).
- **Controlli:** manopole, pulsanti on/off e pulsanti a scelta. Ogni controllo è **disegnato dal codice** (VSTGUI `CDrawContext`): gradienti, ombre, tacche e indicatore in stile console analogica, definitivi e non una riserva. Se nella cartella degli asset esiste il PNG corrispondente (nomi da `UI_ASSETS_SPEC.md`), lo usa al posto del disegno; non serve per rilasciare.
- **Direzione visiva (2026-10-09):** pannello scuro e sobrio. Manopole nere con anello del valore color ambra, pulsanti piatti, testi chiari in sans-serif, sezioni separate da linee sottili. Il tentativo di stile console SSL usurata è stato provato e scartato. Il fondo può essere reso più personale (texture di console), ma senza tornare a quello stile.
- **Sfondo:** 2-3 varianti di texture da console (vernice satinata del pannello con usura diversa, serigrafia più o meno sbiadita, lievi segni d'uso). Le texture sono PNG **generati da uno script** (`src/the-stripper/tools/gen_textures.py`, rumore procedurale con parametri e seme regolabili), committati in `src/the-stripper/assets/`. La variante è scelta una volta per istanza (casuale alla creazione) e salvata nello stato (§6); se un PNG manca, ripiega su una tinta unita.
- **Binding:** ogni controllo è legato a un `ParamID` e supporta automazione, annullamento e testo del valore fornito dal `Controller`.
- **Mix bus:** i controlli non pertinenti (Voice/Drums/Classic) si nascondono; non si ricostruisce l'editor.

### 4.1 Meter e indicatori di clipping

Servono per vedere a colpo d'occhio dove il segnale si sporca e quanto lavorano le dinamiche.

- **Meter di picco stereo** in ingresso (prima di tutto) e in uscita (dopo l'Output gain): scala da -60 a +6 dBFS, attacco istantaneo, rilascio di 20 dB/s, indicatore di picco mantenuto per 1,5 s.
- **Gain reduction** per compressore (scala 0..-20 dB) e limiter (0..-12 dB), disegnate dall'alto verso il basso.
- **LED di clipping per stadio:** Input, EQ, Compression, Drive, Limiter, Output. Un LED si accende quando il picco all'**uscita di quello stadio** raggiunge o supera 0 dBFS (valore assoluto ≥ 1.0) e resta acceso 2 s; un clic su qualunque LED li azzera tutti (parametro nascosto e scrivibile `kClearClips`, unica eccezione alla sola lettura). Questo mostra quale stadio introduce il clip. Gli stadi in bypass non accendono il loro LED.
- **Trasporto dei dati:** parametri **in sola lettura** (`kIsReadOnly`, nascosti all'host) in un intervallo di ID separato, da `kMeterBase = 1000` in su. Il processor li scrive in `outputParameterChanges` al massimo ogni 16 ms (circa 60 Hz). Non fanno parte di `kNumParams`, non si salvano nello stato e la tabella dei parametri bloccata (§5) non li include.
- **Editor:** legge i valori da un timer a 30 ms (`CVSTGUITimer`) e non tocca mai il thread audio.
- **Gain reduction dai motori:** i motori Multi-Comp e il limiter espongono `currentGainReductionDb()` (valore ≥ 0, in dB). Se `BlockParty` non lo permette, si ricava dal rapporto tra i picchi di uscita e ingresso del blocco.
- **Costo:** il calcolo dei picchi è `O(n)` per blocco, senza allocazioni.

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
- Tabella dei parametri bloccata (§5) e tabella separata dei parametri di meter (ID da `kMeterBase`, nessuna collisione con quelli persistenti).
- Meter: `PeakMeter` (picco, rilascio a 20 dB/s, hold di 1,5 s), rilevamento del clip (|x| ≥ 1.0, hold di 2 s, azzeramento), gain reduction dei motori pari a 0 quando non comprimono e positiva quando comprimono, nessuna scrittura dei meter nello stato.
- Mapping dei due nuovi parametri.
- La GUI si verifica a mano in Reaper e Cubase (aprire/chiudere l'editor più volte, automazione, cambio di Strip Mode).

## 8. Rischi

- `NamDrive` alloca durante `loadModel`: il percorso asincrono deve garantire che nulla di tutto ciò avvenga sul thread audio. Verificato in revisione e con un test che chiama `process` mentre il caricamento è in corso.
- Il porting di `NamDrive` in `shared/` tocca la build di CUDA/`gpu_nam`: va controllata la build con e senza CUDA.
- VSTGUI4 aggiunge dipendenze di build (finestra, bitmap, file selector) per piattaforma; Linux/Windows/macOS verificati dalla CI esistente (`.github/workflows/build.yml`).

- I parametri di meter non devono saturare il canale host↔plugin: limite di una scrittura ogni 16 ms, verificato in test.

## 9. Dipendenze con altri piani

Doggystyle riusa `shared/nam_drive` e il caricamento asincrono: questo spec va implementato **prima** (almeno §2 e §3).
