# The Stripper — Completamento: modello NAM, GUI, rifinitura

**Data:** 2026-10-09 (aggiornato 2026-10-10)
**Stato:** in implementazione; design della GUI approvato nelle preview (`src/the-stripper/docs/mockups/`)
**Plugin:** The Stripper (`src/the-stripper/`)

## 0. Avanzamento (2026-10-10)

- **Fatto:** pacchetto VST3 corretto (factory, bundle Windows/macOS); `NamDrive` estratto in `shared/nam_drive` e compilato con NAM v0.6.0 (49 test su 49, CUDA non verificato); default dell'HPF a 20 Hz.
- **Editor (in corso):** layout, binding, attenuazione dei moduli e fascia dei meter e striscia/popup del modello fatti (8a, 8b, 8c); mancano grana, usura, nastro, retro, viti (Task 6, 10-14). I controlli hanno ancora l'aspetto vecchio.
- **Solo design (nelle preview HTML):** il resto dell'interfaccia (§3.1, §4).
- **Da implementare (piano):** caricamento asincrono del modello, nuovi parametri e stato v8, meter, messaggi del modello, editor VSTGUI e relative extra (Task 2-5 e 7-14 del piano).
- **Decisioni aperte:** vedi §11.

## 1. Scopo

Portare The Stripper da "DSP completo con lista parametri dell'host" a plugin utilizzabile: scelta del modello NAM da dentro il plugin, editor grafico, parametri di limiter e gate esposti, conformità ai requisiti di controllo esterno (SSL 360 Link) e documentazione allineata al codice.

Fuori scopo (decisione del 2026-10-09):
- **Gain staging** (parametri `kBypassGainStaging` e `kGainStagingTrim` restano, inerti, come oggi).
- **Integrazione Tone3000**: richiede rete e account; ha uno spec a parte (§10). Il popup di scelta modello nasce con la sola sorgente locale (§3.1).
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

### 3.1 Popup di scelta del modello

Il clic sul campo del modello (Drive) apre un popup in stile rack (preview: `src/the-stripper/docs/mockups/stripper_model_browser.html`), con due schede:

- **LOCAL:** elenco delle cartelle (aggiungi, rimuovi, riscansiona; scansione ricorsiva dei `.nam`, cartelle salvate nelle preferenze dell'utente) e elenco dei modelli con ricerca per nome, dimensione e tipo; il punto verde marca il modello caricato. Pulsanti LOAD, CANCEL e **NO MODEL** (scarica il modello e riporta il Drive in passthrough). La scansione gira fuori dal thread audio e dal thread dell'interfaccia.
- **TONE3000:** scheda prevista dallo spec a parte (§10). Nella prima versione è assente o disabilitata.
- **Architettura:** le sorgenti stanno dietro un'interfaccia comune (`ModelSource`: elenco, ricerca, risoluzione del percorso locale del file); `LocalFolderSource` è la prima implementazione, Tone3000 sarà la seconda. Il caricamento passa sempre dal percorso asincrono (§3).
- **Nessun modello:** il Drive è in passthrough (nessun gain applicato) e il suo controllo si attenua come un modulo spento; il campo mostra "No model". Nessun modello viene incorporato nel binario del plugin.

## 4. GUI custom (VSTGUI4)

Editor VSTGUI4 (`vendors/vst3sdk/vstgui4`), costruito in codice, senza file `.uidesc`.

- **Dimensioni:** 1000 × 628, orizzontale, stile pannello rack (preview approvata: `src/the-stripper/docs/mockups/stripper_rack.html`, che resta il riferimento di layout). `docs/UI_ASSETS_SPEC.md` (480 × 720) è superato. Il disegno è vettoriale e la finestra è scalata dall'host, quindi non servono asset separati a 2x.
- **Ritocchi di layout (2026-10-14):** i controlli della prima riga (manopole con scale, etichette e valori) sono spostati di 8 px in basso per centrarli nei riquadri, e i pulsanti CHAN/BUS di 14 px; fatto in preview e in `ui/layout.h`.
- **Layout, dall'alto:** riga 1: Input (gain) · Filters (High Pass, Low Pass) · Level (meter e LED di clip, §4.1) · Output (gain, Bypass globale, Chan/Bus) · riga 2: Equaliser a tutta larghezza (4 bande + Saturate; intestazioni LF/LM/HM/HF con etichette dei controlli solo Gain/Freq/Q) · riga 3: Compression (Amount, Mix, Voice/Drums/Classic in basso a destra, visibili solo in Channel) · Drive (Amount, campo del modello NAM) · Limiter (Pound) · Gate (Threshold). Sparisce il pulsante **Load .nam**: il nome del modello è scritto su una **striscia di nastro di carta fissa**, in basso a destra nel riquadro Drive (al posto del vecchio riquadro di input, scelta fuori dai canoni della UI ma voluta). È cliccabile e apre il popup di scelta modello (§3.1); l'icona del chip (inferenza su GPU) sta **dentro il pulsante NAM** (deciso 2026-10-14), accanto all'interruttore NAM/Saturate, e compare **solo quando il plugin sta davvero usando la GPU** (serve un campo GPU in `ModelStatus`); si attenua con il modulo. Il nome è mostrato senza l'estensione `.nam`, nel **font del nastro** scelto dall'utente, ed è **troncato per larghezza misurata in pixel** (non per numero di caratteri), con un puntino di sospensione finale, così sta con tutti i font; il nome completo è nel suggerimento al passaggio del mouse. Senza modello la striscia dice "No model".
- **Pulsanti IN e Bypass:** ogni modulo (Filters, EQ, Compression, Drive, Limiter, Gate) ha un pulsante **IN** in alto a destra del riquadro (i due filtri High Pass e Low Pass **condividono un solo IN**, deciso 2026-10-14: usa `kBypassLPHP` con la polarità invertita come gli altri, mentre `kEqHPFOn`/`kEqLPFOn` restano a 1 senza controllo), **acceso di default** (un channel strip parte con tutto inserito). Solo l'interfaccia inverte la polarità: i parametri `kBypass*` restano com'erano (ID e valori invariati; IN acceso = bypass 0). `kEqHPFOn`/`kEqLPFOn` hanno default 1 (neutro all'ascolto: HPF 20 Hz, LPF 20 kHz) e non hanno più un controllo proprio. Il **Bypass globale** resta un pulsante BYPASS nel riquadro Output. Le sezioni Bell di LF e HF sono un pulsante con l'icona della campana sulla riga d'intestazione della banda (spento = shelf).
- **Ordine dei livelli (dal fondo):** sfondo e grana · bordi e titoli dei riquadri · **serigrafia e valori dei controlli** (puntini e valori di fondo scala delle manopole, nomi delle manopole e dei pulsanti, **valori gialli correnti**) · **usura** (graffi, aloni, scheggiature, nastro, residui di colla) · viti · manopole, pulsanti e LED. Il nastro (opacità circa 80%), i graffi e le macchie coprono quindi la serigrafia, i **valori gialli** e i bordi bianchi, ma mai le manopole, i pulsanti o i LED. Lo strato dell'usura lascia passare i clic (solo il nastro li cattura, per scriverci sopra), quindi il valore giallo resta modificabile con un clic anche sotto uno scratch. La serigrafia si attenua insieme al suo modulo e con il bypass globale.
- **Attenuazione:** quando un IN è spento i controlli del suo modulo si attenuano (opacità 35%, dissolvenza 0,15 s). Con il Bypass globale acceso si attenuano tutti i moduli, tutti i pulsanti IN, Chan/Bus, il blocco GR e i LED di clip EQ/CMP/DRV/LIM; restano a piena luce solo i LED IN e OUT e i meter di livello. In bypass globale il segnale è identico in ingresso e uscita: gain reduction a zero, nessun clip se non in IN/OUT.
- **Materiale dei controlli (deciso 2026-10-10):** **tutti i plugin della suite** (decisione aggiornata il 2026-10-10: stesso materiale e stessa texture per semplificare, vedi `docs/DESIGN_SYSTEM.md`) usano **alluminio tornito con finitura nera anodizzata**: cerchi di tornitura sottilissimi, smusso sul bordo, grana finissima illuminata da in alto a sinistra, nessun lucido. L'usura dei controlli è guidata dalla forma: scaglie di vernice staccata (bordo netto fuori, irregolare dentro) sul lato dove si appoggia il dito (in basso a destra), sporco alla base delle manopole e nello spazio attorno ai pulsanti, puntini di due tipi (sporco in bruni, vernice scalfita in grigi), tutto nitido e opaco (niente aloni sfocati). Tutti gli strati di texture e di usura dei controlli sono **semitrasparenti (circa 25%)** sopra il colore di base, in modo che il materiale si legga appena e non copra la forma; la quantità di usura segue l'impostazione Wear come per il pannello. Le macchie di sporco appiccicoso sul pannello restano opache. Pulsanti: stessa finitura, con angoli e spigoli consumati. Preview di confronto dei materiali: `stripper_controls_lab.html`.
- **Stile generale:** pannello nero uniforme, riquadri con bordo bianco e titolo sul bordo, tacche bianche e valori a fondo scala, LED senza alone, niente gradienti lucidi né anello giallo; il materiale delle manopole e dei pulsanti è nel punto precedente. Ogni controllo è disegnato dal codice (VSTGUI `CDrawContext`); se esiste il PNG corrispondente (nomi da `UI_ASSETS_SPEC.md`) lo usa al posto del disegno, ma non serve per rilasciare. Niente nomi o marchi delle fonti d'ispirazione.
- **Sfondo (approvato 2026-10-10):** nero `#161617` con una **grana finissima quasi impercettibile** (vernice opaca a grana fine, tipo powder coat), più un gradiente di luce morbido (riflesso in alto, ombreggiatura in basso). Una sola texture: una tessera ripetibile 256 × 256 in PNG con soli punti chiari a bassa opacità (valore di riferimento della preview: opacità massima circa 7%), **generata da script** (`src/the-stripper/tools/gen_panel_grain.py`, rumore procedurale con seme fisso) e committata in `src/the-stripper/assets/panel_grain.png`; il gradiente è disegnato dal codice. Se il PNG manca, l'editor ripiega sulla tinta unita. La texture è **fissa** (non casuale) e **comune a tutti i plugin** (decisione del 2026-10-10); la finitura rugosa ("wrinkle") provata nella preview resta solo come riferimento (`twrink`).
- **Lingua dell'interfaccia:** tutto il testo visibile nell'editor (etichette, suggerimenti, messaggi di errore, credits, popup) è **in inglese**.
- **Spiegazione dei pulsanti (deciso 2026-10-10):** il retro include una sezione (nella scheda SETTINGS o una scheda HELP) che spiega, per ogni pulsante che impone una configurazione o cambia modello (NO MODEL, Chan/Bus, Voice/Drums/Classic, Bypass globale, IN dei moduli, Re-roll, Mark now, cartelle dei modelli, ecc.), che cosa fa e **quali valori impone o modifica** (per esempio: Chan/Bus cambia il motore dell'EQ e nasconde i modi del compressore; NO MODEL scarica il modello e porta il Drive in passthrough senza toccare il suo valore). Testo in inglese. Decisione comune a tutti i plugin (`docs/DESIGN_SYSTEM.md`): è una **scheda dedicata** del retro, **generata dalla stessa tabella usata dal codice** (ogni voce della tabella dichiara pulsante, azione e valori imposti), così non diverge. The Stripper (VOICE, DRUMS, CLASSIC, Chan/Bus, NO MODEL) va allineato al primo caso, la scheda DEVICES di Hotline.
- **Retro del pannello:** a destra dell'intestazione, con lo stesso carattere e colore di "FAIRY TAPES", c'è l'unica voce **BACK →**; il clic ruota il pannello (flip 3D, circa 0,8 s) e mostra il retro con due schede, **SETTINGS** e **CREDITS** (si ricorda l'ultima usata, default SETTINGS) e **← BACK** per tornare davanti. **CREDITS:** versione, licenza del plugin (GPL-3.0), componenti di terze parti con licenza (4K-EQ e Multi-Comp di Dusk Audio, Airwindows, Neural Amp Modeler Core, Eigen, nlohmann/json, VST 3 SDK, VSTGUI), avvisi (marchio VST di Steinberg, licenze dei modelli `.nam`); le licenze vanno ricontrollate sui file `LICENSE` dei sottomoduli prima del rilascio; con Tone3000 attivo si aggiunge "Powered by TONE3000" con link. **SETTINGS:** *Appearance* (salvata per l'utente, valida in ogni progetto: usura Off/Light/Heavy, livello note Hide/Show, mantenimento e rilascio dei meter, Re-roll) e *Behaviour* (salvata nel progetto: inferenza NAM Auto/GPU/CPU, segni di richiamo Auto/Manual con Mark now, gestione cartelle modelli, accesso a Tone3000 più avanti). Preview: `stripper_rack.html`.
- **Font del nastro:** tre font open a scelta dell'utente nelle impostazioni (Appearance, riga TAPE FONT, controllo con frecce): **Zeyada** (scarabocchio sottile), **Rock Salt** (pennarello ruvido), **Homemade Apple** (corsivo a stilografica); default Zeyada. Solo per il nastro. Licenze, a memoria: Zeyada OFL, Rock Salt e Homemade Apple Apache 2.0 (da ricontrollare sulle pagine dei font prima del rilascio; il testo della licenza va incluso nei credits). In VSTGUI i glifi (maiuscole, cifre, punteggiatura) sono convertiti a build in **tracciati vettoriali** con uno script e disegnati come percorsi: nessun font da installare né da registrare sulle piattaforme, e una piccola variazione casuale di posizione e inclinazione per lettera, derivata dal seme.
- **Valori scritti a mano (opzione):** in Settings > Appearance, riga **VALUES FONT** (**STANDARD** o **TAPE FONT**, cioè lo stesso carattere scelto nella riga TAPE FONT), vale per i valori sotto le manopole, con lo stesso font scelto per il nastro (il nome del modello sul nastro usa sempre il font del nastro). Per evitare che alcuni sembrino più grandi di altri, la dimensione di ogni font è **normalizzata**: l'altezza delle cifre è portata a quella del testo standard e poi moltiplicata per 1,3 (leggibilità); il valore sta 3,5 px più in basso dell'etichetta sopra, per staccarsi da essa. **Default: TAPE FONT** (scelto il 2026-10-10); l'utente può tornare a Standard per leggere i numeri più nitidamente. Preview: `stripper_rack.html`.
- **Testo selezionabile solo dove serve:** nessun testo dell'editor è selezionabile con il mouse, tranne due casi. (1) Il **valore sotto una manopola**: un clic apre un campo di inserimento manuale (Invio o perdita del fuoco conferma, Esc annulla; accetta "3", "+3.5 dB", "1.2k", "45 %"; il numero viene riportato nel range con la mappa inversa del parametro). (2) Un **pezzo di nastro**: un clic permette di scriverci sopra (fino a 14 caratteri), il testo si salva nello stato e si azzera con Re-roll. Con VSTGUI: `CTextEdit` su entrambi.
- **Spazio:** margine di circa 30 px sotto l'ultima riga di riquadri.
- **Doppio clic:** su qualunque manopola riporta il parametro al suo **valore predefinito** (`defaultNormalizedValue` dichiarato dal `Controller`, non il valore iniziale della vista), come un'unica modifica annullabile (`beginEdit` / `performEdit` / `endEdit`).
- **Binding:** ogni controllo è legato a un `ParamID` e supporta automazione, annullamento e testo del valore fornito dal `Controller`.
- **Mix bus:** i controlli non pertinenti (Voice/Drums/Classic) si nascondono; non si ricostruisce l'editor.

### 4.1 Meter e indicatori di clipping

Servono per vedere a colpo d'occhio dove il segnale si sporca e quanto lavorano le dinamiche.

- **Meter di picco stereo** in ingresso (prima di tutto) e in uscita (dopo l'Output gain): scala da -60 a +6 dBFS, attacco istantaneo, rilascio di 20 dB/s, indicatore di picco mantenuto per 1,5 s.
- **Gain reduction** per compressore (scala 0..-20 dB) e limiter (0..-12 dB), disegnate dall'alto verso il basso. I due parametri trasportano la riduzione nella **codifica di SSL 360 Link** (0 = nessuna, dB = 20·log10(1 − valore)), sono in sola lettura e **visibili**; l'editor li riconverte per la scala del proprio meter (vedi FEATURES §4).
- **LED di clipping per stadio:** Input, EQ, Compression, Drive, Limiter, Output. Un LED si accende quando il picco all'**uscita di quello stadio** raggiunge o supera 0 dBFS (valore assoluto ≥ 1.0) e resta acceso 2 s; un clic su qualunque LED li azzera tutti (parametro nascosto e scrivibile `kClearClips`, unica eccezione alla sola lettura). Questo mostra quale stadio introduce il clip. Gli stadi in bypass non accendono il loro LED.
- **Trasporto dei dati:** parametri **in sola lettura** (`kIsReadOnly`, nascosti all'host) in un intervallo di ID separato, da `kMeterBase = 1000` in su. Il processor li scrive in `outputParameterChanges` al massimo ogni 16 ms (circa 60 Hz). Quelli di gain reduction sono visibili (SSL 360 Link), gli altri nascosti. Non fanno parte di `kNumParams`, non si salvano nello stato e la tabella dei parametri bloccata (§5) non li include.
- **Editor:** legge i valori da un timer a 30 ms (`CVSTGUITimer`) e non tocca mai il thread audio.
- **Gain reduction dai motori:** i motori Multi-Comp e il limiter espongono `currentGainReductionDb()` (valore ≥ 0, in dB). Se `BlockParty` non lo permette, si ricava dal rapporto tra i picchi di uscita e ingresso del blocco.
- **Costo:** il calcolo dei picchi è `O(n)` per blocco, senza allocazioni.

## 5. Rifinitura e conformità

- **Nuovi parametri** (aggiunti **in coda** a `ParamID`, per non cambiare gli ID esistenti): `kLimiterPound` (0..1, ingresso `pound` di `BlockParty::setParams`) e `kGateThreshold` (0..1, ingresso `threshold` di `DeNoise::setParams`). `wet` di entrambi resta fisso a 1.0 e `speed` del gate al valore attuale.
- **Stabilità dei parametri:** test che fissa la tabella ID → nome → numero di passi → default di tutti i parametri, così un cambio accidentale dell'ordine fa fallire la build. Requisito per SSL 360 Link e per la compatibilità con i progetti salvati.
- **SSL 360 Link:** attività di ricerca dei requisiti ufficiali; il risultato va in una sottosezione di `docs/FEATURES.md §4` con fonti e data. Se non esistono requisiti pubblici, la sezione lo dichiara e si mantiene la regola attuale (VST3 rigoroso, ID stabili, nomi chiari).
- **Documentazione:** `docs/PARAMS_STATUS.md`, `docs/CONTROLS.md`, `docs/BUILD_AND_USE.md` e `README.md` del plugin allineati al codice (conteggio parametri, Drive, nuovi parametri, GUI, caricamento del modello).
- **Default dei filtri (deciso 2026-10-10):** il doppio clic su High Pass riporta a **20 Hz** e su Low Pass a **20 kHz**. Per l'HPF `kDefaultEqHPFFreq` passa da `20/500` (che con la mappa lineare `20 + n·480` valeva 39 Hz) a **0,0**; il LPF era già a 1,0 (20 kHz). La tabella bloccata (§5) nasce con questi valori.

## 6. Stato e preferenze

**Stato del progetto** (nello stato del plugin, per istanza). `kStateVersion` passa da 7 a **8**. Aggiunte, in coda e tutte opzionali alla lettura (uno stato v7 resta caricabile con i valori di default indicati):
- percorso del modello NAM (stringa UTF-8 con lunghezza; vuota se assente);
- **seme dell'usura** (intero a 32 bit, generato alla creazione dell'istanza; se assente se ne genera uno e si salva);
- angoli delle 4 viti (4 reali) e testi scritti sul nastro (elenco di stringhe brevi, fino a 14 caratteri);
- **segni di richiamo**: i valori normalizzati dei parametri all'ultimo caricamento o "Mark now", più un flag di validità;
- impostazioni di comportamento: inferenza NAM (Auto/GPU/CPU) e modalità dei segni di richiamo (Auto/Manual).

**Preferenze dell'utente** (file nella cartella delle preferenze, valide per ogni progetto e istanza, **non** nello stato): livello di usura (Off/Light/Heavy), livello note (Hide/Show), mantenimento e rilascio dei meter, font del nastro, font dei valori, cartelle dei modelli. Le chiavi sono nomi stabili; un file mancante o illeggibile significa valori di default.

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

- **Messaggi dal thread di caricamento:** lo stato del modello torna al controller con un `IMessage` inviato dal thread di lavoro di `NamDrive` (non dal thread audio, dove sarebbe vietato allocare). Il messaggio è serializzato con un mutex e il processor azzera la callback prima di distruggersi; resta da verificare che Reaper e Cubase accettino messaggi da un thread diverso da quello dell'interfaccia.

## 9. Dipendenze con altri piani

Doggystyle riusa `shared/nam_drive` e il caricamento asincrono: questo spec va implementato **prima** (almeno §2 e §3).

## 10. Tone3000 (spec a parte)

Esito della ricerca del 2026-10-10 (pagine lette da un agente, riassunti non verificati sul testo integrale; da riconfermare prima di scrivere il codice):

- Esiste un'API REST ufficiale (beta) con login OAuth2 PKCE (serve un account dell'utente), limite predefinito di 100 richieste al minuto. Lo sviluppo è gratuito; il livello gratuito è solo non commerciale (open source incluso) con ricerca limitata; un prodotto a pagamento richiede un accordo commerciale.
- Condizioni d'uso: vietati download in blocco, mirror e cache del catalogo; vietato incorporare modelli nel plugin; download solo su richiesta dell'utente. Obbligo di "Powered by TONE3000" con link e di mantenere nome dell'autore, metadati e licenza. Il campo `license` di ogni modello (t3k, cc-by, cc-by-sa, cc-by-nc, cc0, ...) è esposto dall'API.
- Non documentato: cache locale dei file già scaricati dall'utente (e quindi l'uso offline); da chiedere al supporto.
- Esiste un plugin ufficiale open source (MIT, JUCE, libcurl) come riferimento di integrazione.
- Conseguenze: nessun modello nel binario; scheda TONE3000 con badge di licenza; thread di rete dedicato, mai il thread audio; chiavi secret mai nel client.

## 11. Decisioni aperte (2026-10-10)

- **Richiamo da doppio clic:** il doppio clic riporta al valore neutro; resta da decidere se **Alt + doppio clic** (o Ctrl) riporta al valore di richiamo (il segno di biro).
- **Segni di biro e scotch sui valori:** il concetto è approvato; restano da definire il disegno, la soglia "valore cambiato" e il comportamento con molte manopole mosse.
- **Macchie da pennello asciutto:** la versione attuale (setole) è provvisoria; l'utente aveva in mente un'altra forma.
- **Drive: NAM o Saturate (deciso 2026-10-14, implementato nel Task 15 tranne l'icona GPU):** il Saturate del 4K-EQ esce dall'EQ (e dal suo bypass) e diventa il motore alternativo del Drive. Nel riquadro Drive c'è un **pulsante NAM**, in alto a destra **accanto al pulsante IN** (a sinistra di esso) (etichetta "NAM", LED acceso/spento come tutti gli altri): acceso = il Drive usa il NAM, spento = usa il Saturate (stessa classe `SSLSaturation`, spostata dopo l'EQ). C'è **una sola manopola DRIVE**, nella stessa posizione e con la stessa etichetta: graficamente non si nota lo scambio; sotto, la manopola è legata a `kDriveAmount` (NAM acceso) o a `kEqSaturation` (NAM spento), ciascun parametro conserva il proprio valore e lo stesso controllo li mostra a turno. Serve un nuovo parametro in coda (ID 35, interruttore NAM, default acceso) e lo stato v9. Il pulsante IN del Drive resta il bypass dell'intero modulo. Con il NAM spento la **striscia di nastro** non mostra il nome del modello ma la scritta **"4K Saturate"** (testo provvisorio, nello stesso font del nastro, nessuna attenuazione): dice quale motore sta lavorando. **A NAM spento non è cliccabile** (deciso 2026-10-14): il popup dei modelli si apre solo con il NAM acceso; il modello caricato resta in memoria e torna visibile riaccendendo il NAM. **Deciso (2026-10-14):** nessuna migrazione, non esistono progetti vecchi da proteggere; NAM acceso senza modello = passthrough. **Ancora da decidere:** formato del valore mostrato (stessa scala per i due). **Compensazione del livello (misurata 2026-10-14):** non serve; con una sinusoide a 1 kHz il Saturate non cambia il livello a −30 e −18 dBFS (≤ +0,7 dB a drive massimo) e cala solo a −6 dBFS (fino a −5 dB), come compressione di picco che fa parte dell'effetto; il NAM è già compensato. Si aggiunge un trim solo se a orecchio si sente un salto.
- **LED dei selettori (deciso 2026-10-14):** i selettori di modo (Chan/Bus, Voice/Drums/Classic, NAM) hanno tutti il LED **giallo**; i pulsanti IN restano rossi. Fatto nella preview; l'editor VSTGUI lo avrà col restyle dei controlli (Task 11).
- **Titolo del riquadro (2026-10-14):** il riquadro si chiama **DRIVE** (non più "DRIVE · NAM"): contiene due motori; anche la riga sotto il titolo dice "DRIVE".
- **Meter del gate (deciso 2026-10-14, fatto nel Task 16):** terza riga GR ("G") nel riquadro Level, tra GR e CLIP, con i dB di attenuazione del gate ricavati dal confronto dei picchi in/out del blocco (Denoise non espone la riduzione), con ballistica veloce (rilascio rapido e breve tenuta del picco) per vedere quando e con che velocità chiude. Nuovo parametro di meter `kMeterGrGate` in sola lettura, visibile, nella codifica SSL 360 Link (terza corsia sul UC1); si attenua con il Bypass globale come le altre righe GR. Limite: stima, meno precisa con segnali molto bassi.
- **Tone3000:** spec a parte, dopo la verifica dei termini (§10).
- **Estrazione di un kit di interfaccia comune** (usura, nastro, viti, retro con Settings e Credits, controlli) in `shared/` per riusarlo negli altri plugin, con lo stesso materiale e la stessa texture. Da valutare quando si inizia il secondo plugin con editor.
- **Licenze** di font e componenti da ricontrollare prima del rilascio; **build con CUDA** da verificare su una macchina con driver NVIDIA.
