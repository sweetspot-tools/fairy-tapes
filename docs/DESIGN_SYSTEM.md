# Fairy Tapes — Linguaggio grafico comune

Regole visive condivise da tutti i plugin della suite. Materiale dei controlli e texture del pannello sono **uguali per tutti i plugin** (sezione "Materiale e texture"); anche tutto il resto è comune. Riferimento vivo: `src/the-stripper/docs/mockups/stripper_rack.html` (preview), con `stripper_controls_lab.html` (materiali) e `stripper_model_browser.html` (popup di scelta). Il dettaglio di The Stripper è nella sua spec (§4).

## Pannello

- Stile pannello rack, orizzontale; riquadri con bordo bianco e titolo sul bordo (il bordo è tracciato con un'interruzione per il titolo, mai coperto da un rettangolo). **Il titolo dei riquadri del pannello frontale sta sul bordo BASSO, mai su quello alto** (nelle preview: `box(...,'bottom')`); solo i riquadri del retro (Settings, Credits e simili) hanno il titolo sul bordo alto. Regola ribadita dall'utente il 2026-10-15 perché sbagliata più volte nelle nuove grafiche. LED senza alone; nessun gradiente lucido. **Colore dei LED dei pulsanti:** i pulsanti **accendi/spegni** (IN, Bypass, Sync, Sidechain) hanno il LED **rosso**; i **selettori di valore o di modo** (Chan/Bus, Voice/Drums/Classic, NAM, note 1/16-1/2) lo hanno **giallo** (deciso 2026-10-14; regola estesa a Sync e Sidechain di Pillow Talk).
- Testo visibile **in inglese**; nessun testo selezionabile con il mouse, salvo i due campi modificabili (valore sotto una manopola, scritta sul nastro).
- Nessun nome o marchio delle fonti d'ispirazione nella UI, **tranne le sigle dei blocchi** (sezione "Sigle dei blocchi").
- Doppio clic su una manopola: valore predefinito del parametro, come modifica unica annullabile.

## Livelli (dal fondo)

1. sfondo e grana · 2. bordi e titoli dei riquadri · 3. serigrafia e valori dei controlli (scale, nomi, valori correnti) · 4. usura (graffi, aloni, macchie, nastro, residui) · 5. viti · 6. manopole, pulsanti e LED.

L'usura lascia passare i clic; solo il nastro li cattura (per scriverci sopra). La serigrafia si attenua con il suo modulo e con il bypass globale.

## Sigle dei blocchi (motore esterno attivo)

Regola per tutti i plugin (ribadita dall'utente il 2026-10-15), definita su The Stripper (spec, "Serigrafia dei blocchi", 2026-10-14; preview `boxTag`):

- Ogni riquadro il cui DSP viene da un plugin esterno (o lo richiama) ha **in alto a sinistra**, dentro il bordo, una piccola sigla che cita il motore **attivo in quel momento**; la sigla cambia con i selettori (modi, motori). Esempi di The Stripper: Compression `OPTO-2A` / `FET-76` / `VCA-160` / `4K-G`; Drive `NAM` / `4K-G` / `4K-E`; Limiter `B-PARTY`; Gate `D-NOISE`.
- **Posizione:** per un riquadro `box(x,y,…)` la sigla parte da `(x+16, y+22)`, allineata a sinistra; il contenuto del riquadro (schermi, controlli) lascia libero quell'angolo.
- **Aspetto:** 7 px, grigio chiaro (`#c9c9c2`, lo stesso di "FAIRY TAPES"), **molto sbiadita** (opacità circa 10%): si intuisce senza saltare all'occhio.
- **Livello:** serigrafia (sotto l'usura); si attenua con il modulo e con il bypass globale.
- Niente sigla nei riquadri senza motore esterno (Input, Output, Level, meter, motori scritti da noi).
- **Testo:** sigle abbreviate, **scelte dall'utente**, che richiamano il modello senza scriverlo per esteso; rischio marchi accettato dall'utente, da ricontrollare prima del rilascio. Nei Credits i componenti restano citati per esteso, con autore e licenza.

## Moduli, IN e bypass

Ogni modulo ha un pulsante **IN** **vicino all'angolo in alto a destra del suo riquadro**, dentro il bordo (nelle preview il centro del pulsante sta circa 29 px a sinistra del bordo destro e 38 px sotto il bordo alto, con l'etichetta IN sopra; per esempio riquadro `box(x,y,w,h,…)` → IN in `(x+w−29, y+38)`), mai a metà altezza, in basso o fuori dal riquadro; i **selettori di modo o di preset** del modulo stanno **in basso a destra** del riquadro, in fila verso sinistra a partire dalla colonna dell'IN (come CHAN/BUS nell'OUTPUT di The Stripper; regola dell'utente, 2026-10-16); eccezione già decisa: NAM/4K del Drive di The Stripper, accanto all'IN (vedi la sua spec). Regola ribadita dall'utente il 2026-10-15. L'IN è acceso di default; a IN spento i controlli del modulo si attenuano (35%). Il **bypass globale** attenua tutto e fa passare il segnale identico. Solo l'interfaccia inverte la polarità dei parametri di bypass (ID e valori restano).

## Usura (un seme per istanza)

Tutto il casuale deriva da un **seme** salvato nello stato: il layout si genera una volta e non cambia a ogni apertura. Impostazione Wear (Off, Light, Heavy) nelle preferenze dell'utente. Elementi: graffi (chiari sul nero, scuri sul bianco), aloni appena percettibili, **macchie opache a bordo netto** (niente aloni sfocati, niente ombre), nastro di carta con scritte e residui di colla, viti a taglio o a croce con angolo casuale (ruotabili con il mouse, una può mancare con probabilità bassa). Le scritte stanno sopra a tutto il nastro; nastro, graffi e macchie coprono anche i valori gialli.

## Controlli

Disegnati dal codice. Usura guidata dalla forma: bordo consumato sul lato del dito (in basso a destra), sporco alla base e negli spazi dei pulsanti, puntini di due tipi (sporco in bruni, vernice scalfita in grigi). Texture e usura dei controlli semitrasparenti (circa 25%) sopra il colore di base. Indice (tacchetta) chiaro con usura leggera.

## Testo

- **Font del nastro** (open): Zeyada, Rock Salt, Homemade Apple, a scelta dell'utente. In VSTGUI come **tracciati vettoriali** (nessun font da installare), con piccola variazione per lettera dal seme. Licenze da includere nei credits.
- **Font dei valori**: Standard o TAPE FONT (default TAPE FONT), con dimensione normalizzata per font (altezza delle cifre uguale a quella standard, ×1,3).
- Nome del modello o preset su una **striscia di nastro fissa**, troncata per larghezza misurata, senza estensione.

## Retro del pannello

**Obbligatorio per tutti i plugin della suite** (regola dell'utente, 2026-10-15): ogni editor ha il retro con almeno le schede **SETTINGS** e **CREDITS**, anche nelle preview, fin dalla prima bozza. **Il retro ha un template grafico fisso, quello di `stripper_rack.html`; cambiano solo i contenuti:** titolo grande = scheda corrente (corsivo, 38 px, in alto a sinistra) con "FAIRY TAPES" sotto; in alto a destra la riga "NOME PLUGIN · DESCRIZIONE"; sulla riga di "FAIRY TAPES" i link alle schede (SETTINGS, BUTTONS, CREDITS: attiva in bianco, le altre in grigio) e **← BACK**; riquadri con il titolo sul bordo alto. CREDITS: riquadro col nome del plugin (versione e licenza), COMPONENTS AND LICENSES (nome, autore, licenza in ambra, righe separate), NOTICES. SETTINGS: APPEARANCE a sinistra (saved for you, in every project; righe e controlli segmentati uguali per tutti), BEHAVIOUR a destra (saved with the project; contenuti del plugin), NOTES sotto. BUTTONS: WHAT THE BUTTONS DO. Non si inventa un retro nuovo (regola dell'utente, 2026-10-15). Una sola voce **BACK →** davanti; il retro (flip 3D) ha le schede **SETTINGS** e **CREDITS** e **← BACK**. Preferenze di aspetto per l'utente (file globale), impostazioni di comportamento nel progetto. Credits con le licenze di tutti i componenti di terze parti e gli avvisi di legge.

I pulsanti che **impongono una configurazione o cambiano un modello** (modalità, dispositivi, preset) sono spiegati sul retro, in una scheda dedicata: cosa fanno e **quali valori impongono**. La scheda è generata dalla stessa tabella usata dal codice, così non diverge. Primo caso: la scheda DEVICES di Hotline; The Stripper (VOICE, DRUMS, CLASSIC) va allineato.

## Materiale e texture (uguali per tutti i plugin)

Deciso dall'utente il 2026-10-10, per semplificare: tutti i plugin della suite usano lo stesso materiale e la stessa texture di The Stripper. Sostituisce le scelte per plugin precedenti (wrinkle e plastica invecchiata di Pillow Talk).

- **Controlli (manopole e pulsanti):** alluminio tornito con finitura nera anodizzata, come in The Stripper.
- **Pannello:** sfondo `#161617` con grana finissima (tessera 256 × 256 da script, `src/the-stripper/tools/gen_panel_grain.py`); materiale e texture fissi (non casuali).
- Ciò che distingue un plugin dall'altro è il layout, il titolo e i moduli, non il materiale.
- Il "kit" di interfaccia comune vive in `shared/ui` (namespace `FairyTapes::Ui`, dal 2026-10-14): usura (`fairy_ui/wear.h`, generatore con seme e `WearLevel`; `WearView`), serigrafia (`fairy_ui/silkscreen.h` e `SilkscreenView`, guidata dall'elenco degli elementi stampati del plugin), controlli rack (`RackKnob`, `RackToggle`, `RackChoice`, `rack_geometry.h`) e `draw_util` (colori, `drawTitledBox`). Pillow Talk lo usa già; The Stripper ha ancora le sue copie in `src/the-stripper/src/ui/` e la sua chat migrerà al kit comune (`docs/superpowers/notes/stripper-migration-to-shared-ui.md`). Dal 2026-10-21 anche il nastro scrivibile: font vettoriali (`fairy_ui/tape_font.h`, glifi generati da `shared/ui/tools/gen_tape_fonts.py`, licenze in `shared/ui/assets/fonts/`), regole del testo (`tape_label.h`) e corpo di carta (`tape_paper.h`, `tape_draw.h`); posizioni e testi restano nei plugin. Viti e retro restano per ora nei plugin. Ogni modifica comune si fa in `shared/ui` e si riporta nella preview e in questo documento.
