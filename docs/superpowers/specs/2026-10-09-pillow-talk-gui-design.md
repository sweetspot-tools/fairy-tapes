# Pillow Talk — Editor grafico (VSTGUI4)

**Data:** 2026-10-09
**Stato:** bozza per revisione
**Plugin:** Pillow Talk (`src/pillow-talk/`). Estende `2026-10-08-pillow-talk-design.md`, che non prevedeva una GUI.
**Branch:** `feat/pillow-talk-gui`, impilato su `feat/pillow-talk` (PR #1).

## 1. Scopo

Montare nell'host l'editor che abbiamo disegnato nel mockup: pannello scuro con manopole ad anello ambra, pulsanti piatti e un display che mostra in tempo reale voce, livello di mandata (ducking) ed eco. L'aspetto è codice-disegnato; le grafiche definitive arriveranno dopo e sostituiranno gli elementi uno alla volta senza toccare la logica.

Successo: aprendo il plugin in Reaper o Cubase si vedono tutti i 16 parametri controllabili dall'editor (con automazione e annullamento) e il display riflette il segnale reale.

Fuori scopo: grafiche definitive, ridimensionamento libero della finestra, un editor per Doggystyle.

## 2. Struttura

- **`shared/ui/`** (nuovo, libreria CMake `fairy_ui`): `knob_math.h` (matematica pura senza dipendenze), `controls.{h,cpp}` (`AnalogKnob`, `ToggleButton`, `ChoiceButtons`), `skin.{h,cpp}` (caricamento opzionale delle immagini). Usata da Pillow Talk ora e dallo Stripper in seguito; il piano di completamento dello Stripper va aggiornato per usarla al posto di `src/the-stripper/src/ui/`.
- **`src/pillow-talk/src/ui/`**: `editor.{h,cpp}` (layout e collegamento ai parametri), `duck_scope.{h,cpp}` (il display).
- Namespace `FairyTapes::Ui` per il codice condiviso, `FairyTapes::PillowTalk::Ui` per quello del plugin.

## 3. Integrazione VSTGUI4

Il CMake della radice attiva il supporto del SDK (`SMTG_ENABLE_VSTGUI_SUPPORT ON`), che definisce i target `vstgui` e `vstgui_support` (include `VSTGUIEditor`). Il plugin collega `vstgui_support`. Dipendenze di sistema su Linux: cairo, freetype2, fontconfig, pango, libxcb, xcb-util-cursor, libxkbcommon (già presenti sulla macchina di sviluppo); i pacchetti `apt` equivalenti vanno aggiunti al job Linux della CI. Su macOS e Windows si usano i framework e le librerie native che il SDK già configura.

## 4. Editor

- `class Editor : public Steinberg::Vst::VSTGUIEditor, public VSTGUI::IControlListener`; `Controller::createView("editor")` lo restituisce.
- Dimensioni fisse **480 × 560**; l'host scala la finestra. Le view sono costruite in codice, senza file `.uidesc`.
- Ogni controllo è legato a un `ParamID` (16 parametri) e supporta automazione, annullamento (`beginEdit`/`performEdit`/`endEdit`), doppio clic per il valore di default e modalità fine con Maiuscolo. Il testo del valore viene dal `Controller` (`getParamStringByValue`).
- Chiusura dell'editor: tutti i listener si rimuovono e le view si rilasciano; l'editor si può aprire e chiudere ripetutamente.

## 5. Layout

Dall'alto: intestazione (titolo, pulsanti Sidechain e Bypass) · display (432 × 130) · sezione Delay (Time, Feedback, Tone, Saturation, Wow/Flutter, Mix; pulsante Sync e quattro note 1/16, 1/8, 1/4, 1/2 per Time) · sezione Auto Duck (Threshold, Depth, Hold, Release; Sync e note per Hold). Con Sync attivo la manopola in millisecondi si attenua e le note sono attive; viceversa a Sync spento.

## 6. Controlli

- **Manopola:** arco del valore color ambra, corpo scuro, indicatore chiaro; angolo da 225° a -45° in senso orario; trascinamento verticale (200 px = tutto il range, 1/10 con Maiuscolo).
- **Pulsante:** piatto, ambra quando attivo; usato per Bypass, Sidechain e Sync.
- **Scelta:** pulsanti affiancati a selezione esclusiva per le note.
- **Skin opzionale:** ogni controllo cerca nella cartella `Resources` del bundle un'immagine con il nome concordato in `docs/UI_ASSETS.md` (sfondo del pannello, filmstrip della manopola, immagine on/off dei pulsanti); se manca, usa il disegno in codice. Il formato delle immagini viene fissato quando arrivano le grafiche definitive e documentato in `docs/UI_ASSETS.md`.

## 7. Display del ducking

Finestra scorrevole di circa 6 secondi, da destra a sinistra, con tre tracce: **Voice** (area grigia, livello del segnale che pilota il rilevatore), **Echo** (area ambra, livello dell'eco in uscita) e **Send level** (linea verde, guadagno del ducking da 0 a 1). Una linea tratteggiata mostra la soglia (Threshold). I dati sono quelli reali del plugin, non simulati.

**Trasporto dei dati:** tre parametri **in sola lettura**, nascosti all'host, in un intervallo di ID separato (`kMeterBase = 1000`): livello della voce, guadagno del ducking, picco dell'eco. Il processor li scrive in `outputParameterChanges` al massimo ogni 16 ms; non fanno parte di `kNumParams`, non si salvano nello stato e la tabella dei parametri persistenti non cambia. L'editor li legge con un timer a 30 ms e li accumula in un buffer circolare; non tocca mai il thread audio. L'`Engine` espone gli ultimi valori calcolati per blocco (nessuna allocazione).

## 8. Test

- `knob_math.h`: angolo agli estremi (0 → 225°, 1 → -45°, 0.5 → 90°), trascinamento con clamp e modalità fine, indice di scelta e ritorno, mappatura in dB dei meter.
- `Engine`: i tre valori di display sono zero in silenzio, la voce segue il segnale, il guadagno scende con la voce sopra soglia e il picco dell'eco compare dopo la frase.
- Parametri di meter: ID da `kMeterBase`, nessuna collisione con quelli persistenti, non presenti nello stato, al massimo una scrittura ogni 16 ms.
- Buffer circolare del display: ordine dei campioni e comportamento a riempimento.
- L'editor e le view si verificano a mano in Reaper e Cubase (apertura e chiusura ripetute, automazione, Sync, sidechain) e con la build su Linux/macOS/Windows della CI.

## 9. Rischi

- VSTGUI4 aggiunge dipendenze di sistema su Linux e richiede che la CI le installi; è la causa più probabile di build rosse.
- Il comportamento della finestra (DPI, scala dell'host) differisce tra Reaper e Cubase e non si può verificare headless.
- Il display dipende dai parametri di sola lettura: se un host non li inoltra al controller, il display resta fermo ma il plugin funziona.

## 10. Decisioni prese in implementazione

- **Traccia Voice:** mostra l'inviluppo del rilevatore (il segnale filtrato e mono che il ducker confronta con la soglia), non il picco grezzo. Un seno stazionario si legge circa 4 dB sotto il suo picco (media del segnale raddrizzato). Calibrare il ducker avrebbe spostato l'innesco di circa 4 dB.
- **Traccia Echo:** livello dell'uscita dopo ducking e Mix; con Bypass o Mix a 0 va al minimo.
- **Picchi tra le scritture:** il processor accumula massimi (voce, eco) e minimo (mandata) tra due scritture da 16 ms.
- **HiDPI:** l'editor implementa `IPlugViewContentScaleSupport`; la dimensione logica resta 480 × 560 e il frame si ingrandisce con il fattore dell'host (su macOS il fattore resta 1).
- **Linux:** solo incorporamento X11; le finestre Wayland native sono rifiutate.
- **`plug.cpp`:** senza `InitModule/DeinitModule` propri, così il SDK esegue `VSTGUI::init`.
- I controlli (`AnalogKnob`, `ToggleButton`, `ChoiceButtons`, `Skin`) vivono in `shared/ui`; la cache della `Skin` dura quanto l'`Editor`.

## 11. Decisioni aperte

- **Formato delle immagini della skin** (filmstrip delle manopole, pulsanti, @2x) e copia degli asset nel bundle: da fissare con le grafiche definitive. Il linguaggio visivo comune è in `docs/DESIGN_SYSTEM.md` e va allineato all'editor attuale (pannello rack, livelli, usura, retro con Settings e Credits, testo in inglese).
- Nota nei docs sul fatto che la traccia Voice si legge circa 4 dB sotto il picco (README e CONTROLS).
- Arrotondamento di 1 px in `attached()` con fattori di scala non esatti; `attached()` che riporta successo dopo un `open()` fallito; supporto alla rotellina del mouse.
- Polling a 30 ms contro scritture a 16 ms (alcuni estremi si perdono), reset dell'accumulatore alla riattivazione, visibilità dei simboli, `build.sh` su cartella pulita, dipendenze Linux nel README.
- Verifica manuale in Reaper e Cubase (apertura e chiusura ripetute, automazione, undo, Sync, HiDPI, display con voce vera) e build su Windows e macOS.
- **Preview del linguaggio grafico comune applicato a Pillow Talk:** `src/pillow-talk/docs/mockups/pillow_rack.html` (pannello rack orizzontale 1000 × 628: riquadro DUCKING con il display e Sidechain, riquadro OUTPUT con Mix e Bypass, riquadro DELAY con Time, Feedback, Tone, Saturation, Wow/Flutter e il gruppo Sync/note del Time, riquadro AUTO DUCK con Threshold, Depth, Hold, Release e il gruppo Sync/note dell'Hold; controlli in plastica invecchiata, texture wrinkle, retro con Settings e Credits). Sostituisce nelle intenzioni la disposizione verticale 480 × 560 dell'editor attuale: dimensioni e layout da confermare.
- **Pulsante IN per modulo:** `DESIGN_SYSTEM.md` lo prevede per ogni modulo, ma Pillow Talk ha solo il bypass globale (nessun parametro per attivare o disattivare Delay e Auto Duck). Nella preview non c'è. Da decidere: ometterlo oppure aggiungere parametri (per esempio "Duck on").

