# Fairy Tapes — Linguaggio grafico comune

Regole visive condivise da tutti i plugin della suite. Ogni plugin sceglie **materiale dei controlli** e **texture del pannello** (sezione "Per plugin"); tutto il resto è comune. Riferimento vivo: `src/the-stripper/docs/mockups/stripper_rack.html` (preview), con `stripper_controls_lab.html` (materiali) e `stripper_model_browser.html` (popup di scelta). Il dettaglio di The Stripper è nella sua spec (§4).

## Pannello

- Stile pannello rack, orizzontale; riquadri con bordo bianco e titolo sul bordo (il bordo è tracciato con un'interruzione per il titolo, mai coperto da un rettangolo); LED senza alone; nessun gradiente lucido.
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

## Per plugin

- **The Stripper:** controlli in alluminio tornito nero anodizzato; sfondo `#161617` con grana finissima (tessera 256 × 256 da script); materiale e texture fissi (non casuali).
- **Pillow Talk (delay):** controlli in **plastica invecchiata** (ingiallita verso il bordo, micro-crepe); pannello con la finitura rugosa **"wrinkle"** (scelta dall'utente il 2026-10-10). Nella preview di The Stripper il wrinkle è il filtro `twrink` (opzione "Wrinkle (other plugins)" del menu Texture), con intensità ridotta allo stesso rapporto di visibilità della grana fine (alfa 0,047 contro 0,07 della grana fine); va regolato a occhio sul plugin vero.
- **Doggystyle:** da decidere.
- Il "kit" di interfaccia comune (usura, nastro, viti, retro, controlli) potrà vivere in `shared/` quando si inizia il secondo plugin con editor; fino ad allora ogni modifica comune si riporta nella preview e in questo documento.
