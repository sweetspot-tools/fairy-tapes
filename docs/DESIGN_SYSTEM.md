# Fairy Tapes — Linguaggio grafico comune

Regole visive condivise da tutti i plugin della suite. Materiale dei controlli e texture del pannello sono **uguali per tutti i plugin** (sezione "Materiale e texture"); anche tutto il resto è comune. Riferimento vivo: `src/the-stripper/docs/mockups/stripper_rack.html` (preview), con `stripper_controls_lab.html` (materiali) e `stripper_model_browser.html` (popup di scelta). Il dettaglio di The Stripper è nella sua spec (§4).

## Pannello

- Stile pannello rack, orizzontale; riquadri con bordo bianco e titolo sul bordo (il bordo è tracciato con un'interruzione per il titolo, mai coperto da un rettangolo); LED senza alone; nessun gradiente lucido. **Colore dei LED dei pulsanti:** i pulsanti **accendi/spegni** (IN, Bypass, Sync, Sidechain) hanno il LED **rosso**; i **selettori di valore o di modo** (Chan/Bus, Voice/Drums/Classic, NAM, note 1/16-1/2) lo hanno **giallo** (deciso 2026-10-14; regola estesa a Sync e Sidechain di Pillow Talk).
- Testo visibile **in inglese**; nessun testo selezionabile con il mouse, salvo i due campi modificabili (valore sotto una manopola, scritta sul nastro).
- Nessun nome o marchio delle fonti d'ispirazione nella UI.
- Doppio clic su una manopola: valore predefinito del parametro, come modifica unica annullabile.

## Livelli (dal fondo)

1. sfondo e grana · 2. bordi e titoli dei riquadri · 3. serigrafia e valori dei controlli (scale, nomi, valori correnti) · 4. usura (graffi, aloni, macchie, nastro, residui) · 5. viti · 6. manopole, pulsanti e LED.

L'usura lascia passare i clic; solo il nastro li cattura (per scriverci sopra). La serigrafia si attenua con il suo modulo e con il bypass globale.

## Moduli, IN e bypass

Ogni modulo ha un pulsante **IN** in alto a destra, acceso di default; a IN spento i controlli del modulo si attenuano (35%). Il **bypass globale** attenua tutto e fa passare il segnale identico. Solo l'interfaccia inverte la polarità dei parametri di bypass (ID e valori restano).

## Usura (un seme per istanza)

Tutto il casuale deriva da un **seme** salvato nello stato: il layout si genera una volta e non cambia a ogni apertura. Impostazione Wear (Off, Light, Heavy) nelle preferenze dell'utente. Elementi: graffi (chiari sul nero, scuri sul bianco), aloni appena percettibili, **macchie opache a bordo netto** (niente aloni sfocati, niente ombre), nastro di carta con scritte e residui di colla, viti a taglio o a croce con angolo casuale (ruotabili con il mouse, una può mancare con probabilità bassa). Le scritte stanno sopra a tutto il nastro; nastro, graffi e macchie coprono anche i valori gialli.

## Controlli

Disegnati dal codice. Usura guidata dalla forma: bordo consumato sul lato del dito (in basso a destra), sporco alla base e negli spazi dei pulsanti, puntini di due tipi (sporco in bruni, vernice scalfita in grigi). Texture e usura dei controlli semitrasparenti (circa 25%) sopra il colore di base. Indice (tacchetta) chiaro con usura leggera.

## Testo

- **Font del nastro** (open): Zeyada, Rock Salt, Homemade Apple, a scelta dell'utente. In VSTGUI come **tracciati vettoriali** (nessun font da installare), con piccola variazione per lettera dal seme. Licenze da includere nei credits.
- **Font dei valori**: Standard o TAPE FONT (default TAPE FONT), con dimensione normalizzata per font (altezza delle cifre uguale a quella standard, ×1,3).
- Nome del modello o preset su una **striscia di nastro fissa**, troncata per larghezza misurata, senza estensione.

## Retro del pannello

Una sola voce **BACK →** davanti; il retro (flip 3D) ha le schede **SETTINGS** e **CREDITS** e **← BACK**. Preferenze di aspetto per l'utente (file globale), impostazioni di comportamento nel progetto. Credits con le licenze di tutti i componenti di terze parti e gli avvisi di legge.

I pulsanti che **impongono una configurazione o cambiano un modello** (modalità, dispositivi, preset) sono spiegati sul retro, in una scheda dedicata: cosa fanno e **quali valori impongono**. La scheda è generata dalla stessa tabella usata dal codice, così non diverge. Primo caso: la scheda DEVICES di Hotline; The Stripper (VOICE, DRUMS, CLASSIC) va allineato.

## Materiale e texture (uguali per tutti i plugin)

Deciso dall'utente il 2026-10-10, per semplificare: tutti i plugin della suite usano lo stesso materiale e la stessa texture di The Stripper. Sostituisce le scelte per plugin precedenti (wrinkle e plastica invecchiata di Pillow Talk).

- **Controlli (manopole e pulsanti):** alluminio tornito con finitura nera anodizzata, come in The Stripper.
- **Pannello:** sfondo `#161617` con grana finissima (tessera 256 × 256 da script, `src/the-stripper/tools/gen_panel_grain.py`); materiale e texture fissi (non casuali).
- Ciò che distingue un plugin dall'altro è il layout, il titolo e i moduli, non il materiale.
- Il "kit" di interfaccia comune vive in `shared/ui` (namespace `FairyTapes::Ui`, dal 2026-10-14): usura (`fairy_ui/wear.h`, generatore con seme e `WearLevel`; `WearView`), serigrafia (`fairy_ui/silkscreen.h` e `SilkscreenView`, guidata dall'elenco degli elementi stampati del plugin), controlli rack (`RackKnob`, `RackToggle`, `RackChoice`, `rack_geometry.h`) e `draw_util` (colori, `drawTitledBox`). Pillow Talk lo usa già; The Stripper ha ancora le sue copie in `src/the-stripper/src/ui/` e la sua chat migrerà al kit comune (`docs/superpowers/notes/stripper-migration-to-shared-ui.md`). Nastro, viti e retro restano per ora nei plugin. Ogni modifica comune si fa in `shared/ui` e si riporta nella preview e in questo documento.
