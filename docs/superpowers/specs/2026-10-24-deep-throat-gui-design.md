# Deep Throat — Editor (GUI) — Design

**Data:** 2026-10-24
**Stato:** implementata (2026-10-31, commit da `621d89c` a `a2b33f0` più la chiusura dei documenti); punti aperti per l'utente nel §11
**Suite:** Fairy Tapes
**Spec del plugin:** `docs/superpowers/specs/2026-10-10-deep-throat-design.md` (DSP, 25 parametri; §10 descrive la grafica ad alto livello)
**Riferimento visivo (fonte di verità delle coordinate):** `src/deep-throat/docs/mockups/deep_throat_rack.html`
**Regole comuni:** `docs/DESIGN_SYSTEM.md`

## 1. Scopo e successo

Scrivere l'editor VSTGUI di Deep Throat, uguale alla preview HTML (pannello 1000 × 614): riquadri con il titolo sul bordo basso, pulsante IN di ogni modulo, selettore di modo a faccia illuminata, SCOPE e LEVEL animati dal motore, retro con le schede SETTINGS, MODES, ROUTING e CREDITS. Prima di questo lavoro il plugin non aveva editor (l'host mostrava la lista dei parametri).

Successo: aprendo il plugin in un host (o con il tool di render fuori schermo) si vede lo stesso pannello della preview; ogni parametro si regola dalla UI e dall'automazione; i meter e lo scope seguono il segnale; lo stato si salva e si ricarica (anche quello di versione 1); i test restano verdi.

## 2. Decisioni (2026-10-24)

- **Una spec e un piano** per fronte e retro; nel piano il fronte viene per primo, il retro è l'ultimo gruppo di task.
- **Struttura copiata da Pillow Talk dentro `src/deep-throat/`**, con il kit `shared/ui` per tutto ciò che esiste (manopole, pulsanti, selettori, meter, retro, usura, viti, nastro, flip, schema ROUTING). Estrarre prefs, decor e nastri in `shared/` (oggi duplicati in The Stripper e Pillow Talk) tocca altri plugin e resta fuori: da fare in un'altra chat.
- **Una sola tabella per modalità** (`mode_table.h`), letta dai blocchi DSP, dall'engine e dalla UI (scritte dinamiche e scheda MODES), come chiede `DESIGN_SYSTEM.md` ("generata dalla stessa tabella usata dal codice").
- **Dati per scope e meter** tramite parametri di visualizzazione nascosti (ID da 1000), a circa 16 ms, come in Pillow Talk. Nessuna allocazione nel thread audio.
- **Stato v2:** dopo i 25 valori si salva il decor (seme, viti, testi dei nastri). La v1 si carica con i default.
- Niente file `.uidesc`: viste costruite in codice, come negli altri plugin.

## 3. Struttura dei file

`src/deep-throat/src/ui/` (namespace `FairyTapes::DeepThroat::Ui`; le parti pure non includono VSTGUI):
- `editor_size.h`: 1000 × 614 (da `fairy_ui/editor_size.h`).
- `layout.h`: tabella pura: riquadri, controlli (kind, rect, nome, scala, LED), elementi stampati (serigrafia), regole di attenuazione (IN, bypass), nastri e viti di base.
- `editor.{h,cpp}`: `VSTGUIEditor`, costruzione delle viste per livelli, legame ai parametri (`beginEdit` / `performEdit` / `endEdit`), timer UI che applica i cambi dall'host e legge i parametri di visualizzazione, flip del retro.
- `panel_view.{h,cpp}`: sfondo e grana (livello 1) e bordi con titoli (livello 2).
- `scope_view.{h,cpp}` e `history.h`: vista SCOPE e buffer circolare dei valori (parte pura testabile).
- `prefs.{h,cpp}`: preferenze utente, file `<cartella preferenze>/FairyTapes/DeepThroat.prefs`, stesso formato e stesse chiavi di The Stripper (`wear`, `notes`, `tapeFont`, `valuesFont`).
- `decor.{h,cpp}`, `screw_angles.h`, `tapes.h`, `tape_view.{h,cpp}`: posizionamento di nastri e viti dal seme, come Pillow Talk.
- `back_settings.h`, `back_modes.h`, `routing_diagram.h`, `credits.h`: contenuti delle quattro schede del retro.
- `tools/render_editor.cpp`: render fuori schermo del fronte e delle quattro schede in PNG (cartella scratch), per il confronto con la preview.

Fuori da `ui/`: `src/mode_table.h` (nuovo), `src/display.h` (valori di visualizzazione), modifiche a `engine`, `processor`, `controller`, `state.h`, CMake.

## 4. Pannello frontale

Pannello 1000 × 614, titoli dei riquadri sul bordo basso, regole di `DESIGN_SYSTEM.md`. La tabella di layout trascrive la preview: i contenuti dei moduli (`modBox`) hanno l'origine spostata di −12 px in y, l'IN sta in `(x+w−24, y+38)`, come nel `modBox` della preview approvata (BREATH 34 e DE-ESS 36 px di inserzione, per IN e LISTEN); `DESIGN_SYSTEM.md` indica 29 px: differenza aperta, vedi §11.

Riquadri (x, y, w, h): INPUT (24,96,110,130), SCOPE (140,96,324,130), LEVEL (470,96,330,130), OUTPUT (806,96,170,130), MODE (24,238,130,174), RIDER (160,238,234,174), BREATH (400,238,355,174), EQ (761,238,215,174), DE-ESS (24,424,237,166), COMP (267,424,403,166), TONE (676,424,300,166).

I 25 parametri, ciascuno legato a un solo controllo:
- **12 manopole** (`RackKnob`): Input, Output, Rider Target, Rider Range, Breath Amount, Breath Sensitivity, EQ Amount, De-ess Amount, De-ess Freq, Comp Amount, Comp Mix, Tone. Valori stampati sotto la manopola (font dei valori delle preferenze), campo modificabile con un clic sul valore, doppio clic = valore predefinito.
- **9 pulsanti on/off** (`RackToggle`, LED rosso): Bypass (nel riquadro OUTPUT, in alto a destra), sei IN (Rider, Breath, EQ, De-ess, Comp, Tone), Breath Listen, De-ess Listen.
- **Mode** (`RackLitChoice`, tre facce impilate SPEAK / SING / PUKE, faccia attiva illuminata ambra) nel riquadro MODE, con la sigla a due righe in alto a sinistra.
- **Tre selettori a ciclo** (`RackCycle`, LED giallo acceso quando il valore non è quello predefinito, valore sotto il LED): Comp Ratio (AUTO, 2, 3, 4, 6, 8, 10, 20:1), Comp Dist (AUTO, OFF, Dist 2, Dist 3), Comp SC HPF (OFF, 80 Hz, 150 Hz). In AUTO il valore stampato mostra quello imposto dalla modalità (per esempio `AUTO 3:1`).

Scritte dinamiche (serigrafia, sotto l'usura): sigla del rilevatore (SPECTRAL / LEVEL) in BREATH, descrizione della modalità in MODE, `MAX ±x dB · y dB/s` in RIDER, HPF e MUD CUT in EQ (seguono modalità e Amount: `hpf × (0.5 + Amount)`, `mud × 2·Amount`), i tre valori in dB del Tone (seguono modalità e manopola), `MOSTLY OFF IN PUKE (X0.15)` in DE-ESS solo in Puke. Tutte derivano da `mode_table.h` e dai valori correnti dei parametri tramite funzioni pure.

Attenuazione (35%), come la preview: un modulo con IN spento attenua i propri controlli e le proprie scritte, compreso il suo Listen (BREATH e DE-ESS); il Bypass globale attenua tutto tranne il proprio pulsante (anche gli IN si attenuano); SCOPE e LEVEL si attenuano solo col Bypass, salvo le righe IN e OUT dei meter e i LED di clip. Le scritte stampate seguono la preview: titolo, FAIRY TAPES, tagline, le etichette di LEVEL (L, R, IN, OUT, CLIP) e le sigle di MODE non si attenuano mai; la scala e l'etichetta GR, la legenda dello SCOPE e le scritte dei moduli sì (flag `dimsWithBypass` su ogni testo).

Controlli a scatti (Mode, Ratio, Dist, SC HPF): un valore dell'host fuori griglia si mostra sullo scatto che usa il DSP (`stepIndex` dell'SDK), così faccia accesa, scritte AUTO e audio coincidono. Le stringhe dei valori mostrate dall'host (`getParamStringByValue`) sono quelle del pannello, prodotte da una funzione pura comune a controller ed editor (`src/value_format.h`); il testo digitato passa per la stessa analisi ("%" sul Tone è la sua scala con segno, sulle altre manopole la corsa).

Livelli (dal fondo): pannello, bordi e titoli, serigrafia, usura, nastri, viti, controlli, SCOPE, meter, LED. La spia BREATH sta sul livello più alto (la `ReadoutView` separa le scritte, sotto l'usura, dalla spia, sopra), come nella preview.

## 5. Dati dal motore alla UI

Parametri nascosti, sola lettura, fuori dallo stato e dall'automazione (`kMeterBase = 1000`): ingresso L e R (picco in dB, scala −60..+6), uscita L e R, riduzione del compressore (0..21 dB), inviluppo della voce (dB, livello RMS del segnale lineare mono d'ingresso, quello del Rider), guadagno del Rider (dB, ±range), probabilità del respiro (0..1, uscita del rilevatore), clip ingresso e clip uscita (booleani), più `kClearClips` (trigger dalla UI, riconosciuto dal processor).

L'engine accumula questi valori senza allocare (massimo del periodo per i picchi, la voce, il respiro e la riduzione del compressore; minimo per il guadagno del Rider; ultimo valore per gli altri) e un limitatore di velocità ne consente la scrittura ogni circa 16 ms di audio (`DisplayRateLimiter` di Pillow Talk come modello). Il processor li scrive come punti di parametro in uscita; il controller li registra con i flag `kIsReadOnly | kIsHidden`.

Lo SCOPE sta sulla linea temporale dell'**ingresso**: il buffer circolare dell'editor tiene 292 punti (una colonna di pixel ciascuno, un punto per ogni giro del timer dell'editor da 30 ms, quindi circa 8,8 s; il processor scrive ogni 16 ms e l'editor legge l'ultimo valore); la linea del guadagno del Rider è quella calcolata per l'audio ritardato, quindi appare circa D (85 ms) prima rispetto alla voce disegnata: è solo estetico, scritto nei limiti noti del README.

Scala dello SCOPE come la preview (decisa nel Task 9): la voce va da −64 dB (in basso, 2 px sopra il bordo) a −10 dB (94 % dell'altezza); il guadagno del Rider ±6 dB occupa l'85 % della mezza altezza attorno alla linea tratteggiata di metà (0 dB), così nessuna traccia tocca il bordo. Filtri nell'editor, come la preview (il motore riporta i valori grezzi): il respiro (spia BREATH e banda rossa dello SCOPE) si mostra solo con Breath IN acceso e senza Bypass; con il Bypass la riga GR è vuota (con Comp IN spento il motore riporta già 0, perché la GR è scalata dalla rampa di Comp IN). Il filtro sta nell'editor: il motore continua a riportare la probabilità grezza del rilevatore.

## 6. Stato v2 e preferenze

Stato v2 (little endian): v1 (int32 versione, 25 `double`), poi `uint32` seme, 4 × `float32` angoli delle viti, `uint32` numero di testi, per ogni testo `uint32` lunghezza e byte. La v1 si carica con i default del decor (seme 0 = "nessuno ancora": lo genera `ensureSeed` fuori dal thread audio e si salva con il prossimo `getState`). Tutto ciò che si legge è sanificato come in Pillow Talk (angoli finiti, testi ASCII stampabili, al massimo 14 caratteri e un testo per posto di nastro); uno stato corrotto è rifiutato intero. `kStateVersion` passa a 2.

Preferenze: Wear (Off, Light, Heavy), notes, tapeFont, valuesFont; lette all'apertura dell'editor, scritte in modo atomico; un problema qualsiasi dà i default. Le impostazioni dello stato dell'istanza (seme, viti, testi) vanno al decor.

## 7. Tabella unica per modalità

`src/mode_table.h`, header-only: per ognuna delle tre modalità (Speak, Sing, Puke), in `constexpr`: tipo di rilevatore, range massimo e pendenza del Rider (`mode_presets.h` è stato rimosso: i blocchi DSP e l'engine leggono direttamente `modeRow`), HPF e taglio del fango dell'EQ, scala del de-esser (1, 1, 0.15), rapporto e Dist in AUTO del compressore, guadagni del Tone a +100% (mud, presenza, aria). I blocchi DSP e l'engine leggono da qui al posto delle costanti locali. I test dei blocchi restano invariati e verdi; nuovi test fissano i valori della tabella ai numeri della spec del plugin.

Dalla stessa tabella una funzione pura (`back_modes.h`) produce le righe della scheda MODES.

## 8. Retro

Template fisso (quello di `stripper_rack.html`, vedi `DESIGN_SYSTEM.md`; il retro non ha viti, come il template condiviso): titolo della scheda in corsivo 38 px, "FAIRY TAPES" sotto, in alto a destra "DEEP THROAT · VOCAL CHANNEL STRIP VST3", link alle schede nell'ordine SETTINGS, MODES, ROUTING, CREDITS e **← BACK**; flip 3D come negli altri plugin (`PanelFlip`, circa 0,8 s).
- **SETTINGS:** APPEARANCE (Wear, Tape font, Values font, LOOK con RE-ROLL; salvate per l'utente) e NOTES; RE-ROLL dà all'istanza un nuovo seme (viti e nastri azzerati). Implementata con il template fisso condiviso (`fairy_ui/back_view.h`), non come la preview: a destra il riquadro BEHAVIOUR vuoto (Deep Throat non ha impostazioni di comportamento), NOTES sotto con 4 righe invece del riquadro da 12 righe della preview; le righe segnaposto della preview (NOTES LAYER, METER HOLD/RELEASE) non ci sono. Vedi §11.
- **MODES:** "WHAT THE MODE BUTTONS DO", generata dalla tabella del §7, con la colonna della modalità attiva in giallo.
- **ROUTING:** schema fisso trascritto dalla preview con `RoutingBuilder` (riquadri SIGNAL FLOW e LEGEND); l'ordine è quello del motore audio. La LEGEND sta dove la mette il template condiviso, non esattamente dove la mette la preview.
- **CREDITS:** nome, versione (`src/version.h`, letta anche da `plug.cpp`) e licenza nel riquadro DEEP THROAT del template condiviso (in più rispetto alla preview); COMPONENTS AND LICENSES: VST 3 SDK (Steinberg, MIT), VSTGUI (Steinberg, BSD-3-Clause), Zeyada (OFL-1.1), Rock Salt e Homemade Apple (Apache-2.0); NOTICES con l'avviso sul marchio VST. Nessun altro componente di terzi (il DSP è scritto da noi).

## 9. Test e verifiche

- **Layout (puro):** ogni ID dei 25 parametri compare in un solo controllo; ogni rettangolo sta dentro il pannello e dentro il proprio riquadro; nessuna sovrapposizione tra controlli; IN in `(x+w−24, y+38)` salvo le inserzioni dichiarate (BREATH, DE-ESS); regole di attenuazione per modulo e per Bypass.
- **Scritte dinamiche (pure):** per ogni modalità e valore di Amount/Tone le stringhe di EQ, Tone, Ratio, Dist, sigla del rilevatore e `MAX ±` coincidono con la tabella.
- **Tabella per modalità:** valori fissati alla spec; i blocchi DSP la usano (i loro test esistenti passano senza modifiche).
- **Stato:** v1 e v2 round trip, v1 carica con decor di default, rifiuto di dati corrotti, sanificazione di angoli e testi.
- **Dati di visualizzazione:** mappatura dB → normalizzato, accumulatori senza allocazioni (contatore di `operator new`), limitatore di velocità, `kClearClips`.
- **Scope:** buffer circolare e conversione in punti (pura).
- **Editor:** il test della factory crea la vista, la apre e la chiude senza finestra e controlla il conteggio dei riferimenti (come Pillow Talk); i parametri nascosti non compaiono nell'automazione. Il comportamento dell'editor (controlli che seguono l'host, modifiche dell'utente, meter, scope, nastri, viti, flip) si prova in `deep_throat_editor_tests` (`tests/test_editor.cpp`), con un `Controller` reale come host e una struct amica `EditorTest`, senza finestra: **solo Linux** (dipende dalla factory VSTGUI di Linux, come il tool di render). Stato v2 e messaggi del decor sul modulo reale: `deep_throat_state_module_test` (`tests/test_state_module.cpp`, classi host dell'SDK).
- **Visivo:** il tool di render produce fronte e le quattro schede in PNG; il confronto con la preview è a occhio e va dichiarato come tale, non come test automatico. I colori e il flip 3D non sono verificabili senza un host: si dichiara cosa non è stato provato.
- Build: solo i target `deep_throat`, `deep_throat_tests`, `deep_throat_factory_test`, `deep_throat_state_module_test`, `deep_throat_ui_tests`, `deep_throat_editor_tests` e `deep_throat_render`, `-j2`, uno alla volta quelli con VSTGUI, in una cartella fuori dal repo. La compilazione su Windows e macOS non è verificata in locale: la verifica la CI al merge verso `master`.

## 10. Rischi e fuori scopo

- La prima compilazione di VSTGUI sul target `deep_throat` è lenta e pesante in memoria: `-j2`, un solo target alla volta.
- Il bundle cresce (VSTGUI e kit); la CI deve già installare le dipendenze di VSTGUI su Linux (lo fa per gli altri plugin).
- Il collegamento di meter e scope dipende dai parametri nascosti: un host che li mostri nell'automazione sarebbe un difetto del controller (flag da verificare nel test).
- Fuori scopo: gli altri plugin, l'estrazione in `shared/`, preset, ridimensionamento della finestra (dimensione fissa con fattore di scala del contenuto, come gli altri).

## 11. Decisioni aperte

Da decidere con l'utente (emerse durante l'implementazione):

- **Retro contro preview:** SETTINGS, ROUTING e CREDITS seguono il template fisso condiviso e non la preview `deep_throat_rack.html`: BEHAVIOUR vuoto a destra, NOTES di 4 righe sotto (la preview ha un riquadro NOTES di 12 righe), nessuna vite sul retro, LEGEND di ROUTING nella posizione del template, riquadro in più con versione e licenza in CREDITS. Allineare la preview al template o il template (condiviso) alla preview.
- **Riga SIZE:** `DESIGN_SYSTEM.md` (riga 53, decisione del 2026-10-24 per tutti i plugin) chiede in APPEARANCE la riga SIZE (75 / 90 / 100 / 125 %, preferenza dell'utente); né questa spec né l'editor la hanno. Da aggiungere con un task dedicato (preferenza `size`, dimensione della finestra moltiplicata per il fattore dell'host).
- **Righe segnaposto tolte:** NOTES LAYER e METER HOLD/RELEASE della preview non esistono nell'editor (nessun comportamento dietro). Confermare o definirle.
- **Inserzione dell'IN:** l'editor usa 24 px dal bordo destro (preview approvata), `DESIGN_SYSTEM.md` dice 29 px. Scegliere quale dei due vale.
- **Copie da Pillow Talk:** `state_stream.h`, `ensureSeed`/`newRandomSeed`, `messages.h` e `rerolledDecor` (quarta copia) sono copie quasi alla lettera di Pillow Talk; con prefs, decor e nastri (tre copie) vanno spostati in `shared/` da un'altra chat.
- **Linea del Rider sotto Bypass:** nella preview la linea del guadagno del Rider torna a 0 dB col Bypass (e con RIDER IN spento); nel motore il Rider continua a girare sotto Bypass e l'editor disegna `kDispRider` così com'è (con RIDER IN spento il valore va a 0 dB per la rampa dell'IN), quindi col Bypass la linea continua a muoversi. Decidere se filtrarla nell'editor come la spia BREATH. L'inviluppo della voce è invece lo stesso della preview (livello dopo il guadagno d'ingresso), salvo la finestra RMS di 200 ms del Rider al posto della media della preview.
- **Punto di misura IN METER:** la scheda ROUTING (come la preview e la spec del plugin §10) disegna IN METER e CLIP IN dopo lo stadio INPUT, ma il motore misura i picchi d'ingresso e il clip **prima** del guadagno d'ingresso (piano, Task 2). Scegliere quale dei due correggere.
- **Windows e macOS:** la compilazione dell'editor su queste piattaforme non è verificata (la fa la CI al merge `pre-release` → `master`); i test dell'editor e il tool di render girano solo su Linux.

Restano dalla prima stesura:

- Nessun controllo "display window" come in Pillow Talk: finestra dello scope fissa a 292 punti. Da rivedere se sembra corta.
- Il confronto con la preview è manuale: se serve un confronto automatico (immagini di riferimento) va deciso a parte.

## 12. Decisioni prese durante l'implementazione (2026-10-24 → 2026-10-31)

- Lavoro direttamente su `pre-release` nel checkout condiviso, senza worktree.
- Test di stato, decor e messaggi in `deep_throat_state_module_test` (non in `test_factory.cpp`, che apre solo il modulo); comportamento dell'editor in `deep_throat_editor_tests` (Task 7).
- Bypass globale: attenua tutto tranne il proprio pulsante, IN compresi (come la preview; la vecchia frase del §4 "e IN/OUT dei clip" era imprecisa).
- IN a 24 px dal bordo destro come la preview (BREATH 34, DE-ESS 36), non a 29 come `DESIGN_SYSTEM.md` (vedi §11).
- Listen di BREATH e DE-ESS si attenua con l'IN del proprio modulo, come la preview.
- Scritte stampate: si attenuano come nella preview (18 testi con `dimsWithBypass`).
- Stringhe dei valori nell'host uguali a quelle del pannello tramite `src/value_format.h` (prima l'host mostrava "+0.0 dB", "%" e "6501 Hz"); tolleranza del giro andata e ritorno di De-ess Freq 0,014 (metà dello scatto di 0,1 kHz a 4 kHz).
- Controlli a scatti: mostrano lo scatto del DSP anche per valori fuori griglia.
- GR scalata dalla rampa di Comp IN nel motore (Comp IN spento → GR 0).
- Spia BREATH e banda dei respiri accese solo con Breath IN acceso e senza Bypass; riga GR vuota col Bypass; filtri nell'editor, il motore riporta i valori grezzi.
- Scala dello SCOPE come la preview (voce al 94 % dell'altezza con 2 px di margine in basso, Rider ±6 dB all'85 % della mezza altezza); timer dell'editor a 30 ms (292 punti ≈ 8,8 s).
- Spia BREATH sul livello più alto (scritte e spia separate nella `ReadoutView`).
- Pannello con Wear Off e seme fisso 0x51 nei test dei pixel; la vista del nastro usa il riquadro del modello del decor (margine 4 px), così posizionamento e disegno coincidono.
- SETTINGS, ROUTING e CREDITS con il template fisso condiviso (vedi §8 e §11); link alle schede circa 10 px a destra della preview.
- `deepThroatRouting()` restituisce un `BackRouting` con lo schema in `.flow`; versione del plugin in `src/version.h`.
