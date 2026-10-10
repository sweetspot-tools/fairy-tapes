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

- **Dimensioni:** 1000 × 608, orizzontale, stile pannello rack (preview approvata: `src/the-stripper/docs/mockups/stripper_rack.html`, che resta il riferimento di layout). `docs/UI_ASSETS_SPEC.md` (480 × 720) è superato. Il disegno è vettoriale e la finestra è scalata dall'host, quindi non servono asset separati a 2x.
- **Layout, dall'alto:** riga 1: Input (gain) · Filters (High Pass, Low Pass) · Level (meter e LED di clip, §4.1) · Output (gain, Bypass globale, Chan/Bus) · riga 2: Equaliser a tutta larghezza (4 bande + Saturate; intestazioni LF/LM/HM/HF con etichette dei controlli solo Gain/Freq/Q) · riga 3: Compression (Amount, Mix, Voice/Drums/Classic in basso a destra, visibili solo in Channel) · Drive (Amount, campo del modello NAM) · Limiter (Pound) · Gate (Threshold). Sparisce il pulsante **Load .nam**: il campo con il nome del modello, in basso a destra nel riquadro Drive, è cliccabile e apre il selettore di file; un'icona a chip grigia indica l'inferenza su GPU.
- **Pulsanti IN e Bypass:** ogni modulo (EQ, Compression, Drive, Limiter, Gate) e ogni filtro ha un pulsante **IN** in alto a destra del riquadro, **acceso di default** (un channel strip parte con tutto inserito). Solo l'interfaccia inverte la polarità: i parametri `kBypass*` restano com'erano (ID e valori invariati; IN acceso = bypass 0). Per i filtri `kEqHPFOn`/`kEqLPFOn` ha già polarità positiva; il loro default passa da 0 a 1 (neutro all'ascolto: HPF 20 Hz, LPF 20 kHz). Il **Bypass globale** resta un pulsante BYPASS nel riquadro Output. Le sezioni Bell di LF e HF sono un pulsante con l'icona della campana sulla riga d'intestazione della banda (spento = shelf).
- **Attenuazione:** quando un IN è spento i controlli del suo modulo si attenuano (opacità 35%, dissolvenza 0,15 s). Con il Bypass globale acceso si attenuano tutti i moduli, tutti i pulsanti IN, Chan/Bus, il blocco GR e i LED di clip EQ/CMP/DRV/LIM; restano a piena luce solo i LED IN e OUT e i meter di livello. In bypass globale il segnale è identico in ingresso e uscita: gain reduction a zero, nessun clip se non in IN/OUT.
- **Stile controlli:** pannello nero uniforme, riquadri con bordo bianco e titolo sul bordo, manopole **piatte e opache** (disco scuro, bordo sottile, indice bianco, tacche bianche e valori a fondo scala), pulsanti quadrati grigi piatti con LED sotto, LED senza alone. Niente gradienti lucidi, niente anello giallo, niente triangolo-puntatore. Ogni controllo è disegnato dal codice (VSTGUI `CDrawContext`); se esiste il PNG corrispondente (nomi da `UI_ASSETS_SPEC.md`) lo usa al posto del disegno, ma non serve per rilasciare. Niente nomi o marchi delle fonti d'ispirazione.
- **Sfondo:** *(da riconfermare)* la preview approvata usa nero uniforme, senza texture. Se si conferma, il generatore di texture (Task 6) e la variante di texture nello stato (§6) escono dallo scopo.
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
