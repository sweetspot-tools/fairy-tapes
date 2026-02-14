# The Stripper — UI assets & dimensions

Bozze grafiche per l’editor del plugin. Rispetta queste specifiche per integrazione nel codice.

---

## 1. Dimensioni

### Pannello principale (unità di riferimento)

| Tipo        | Dimensioni consigliate (px) | Note |
|------------|-----------------------------|------|
| **Base 1x** | **480 × 720**               | Larghezza fissa da “strip”, altezza per le sezioni principali (EQ, Dynamics, Drive/NAM, Limiter, Gate). Gain staging: **solo modalità Channel**, in posizione non invadente (es. blocco compatto in alto o in basso); in Mix bus non presente. |
| **HiDPI 2x** | **960 × 1440**             | Stesso layout, risoluzione doppia per schermi retina/4K. |

- **Proporzioni:** ~2 : 3 (più alto che largo, da channel strip).
- Il plugin può esporre una **size fissa** (es. 480×720) e l’host fa lo scale; oppure due risoluzioni (1x e 2x) e il codice sceglie in base al DPI.
- Per le bozze puoi lavorare in **480×720** (o in 960×1440 se preferisci già in 2x). Se lavori in 2x, indica “2x” nei nomi file.

---

## 2. Componenti grafici da preparare

### 2.1 Sfondo e struttura

| Asset | Descrizione | Formato suggerito |
|-------|-------------|-------------------|
| **Panel background** | Sfondo principale del pannello (trama “analog”: console, metallo, vernice). Prevedi **2–3 varianti** per “per-instance texture variation” (es. più pulito, più consumato, serigrafia leggermente sbiadita). | PNG (o PSD con layer). Stesso size del pannello (480×720 o 960×1440). |
| **Section dividers** | Linee o bande che separano le 4 sezioni (EQ, Dynamics, Drive/NAM, Gate). Possono essere parte dello sfondo o elementi separati. | PNG con trasparenza, o inclusi nel background. |

### 2.2 Controlli

| Asset | Descrizione | Dimensioni indicative |
|-------|-------------|------------------------|
| **Knob** | Manopola rotante (gain, freq, threshold, ratio, ecc.). Più stati (frame) per l’animazione (es. 64–128 frame per un giro completo) oppure singola immagine ruotata in codice. | Es. **80×80** px (1x) o **160×160** (2x). Una versione “grande” e una “piccola” se servono due scale. |
| **Fader** | Fader verticale (opzionale, se usi fader invece di knob per alcuni parametri). Track + cap. | Es. track **24×120** px, cap **32×24** (1x). |
| **Button (bypass)** | Pulsante bypass on/off (due stati o una sola immagine con zona “pressed”). | Es. **40×24** o **48×28** (1x). |
| **Button (section)** | Eventuali pulsanti per bypass sezione o altre azioni. | Coerenti con il bypass (es. **32×32**). |

### 2.3 Letture e testo

| Asset | Descrizione | Note |
|-------|-------------|------|
| **VU / level meter** | Segmenti o barra per livello input/output (o per sezione). | Strip verticale o orizzontale; es. **8×60** o **100×12** (1x). |
| **Labels** | Titoli sezioni (“EQ”, “Dynamics”, “Drive”, “Gate”) e eventuali etichette parametri. | Puoi fornirli come immagini (PNG con trasparenza) oppure solo tipo/font e colore: il testo verrà renderizzato in codice. |

### 2.4 Extra (opzionale per le bozze)

- **Icone** per ogni sezione (EQ, Dynamics, Drive, Gate) se vuoi un’icona oltre al testo.
- **Overlay “brand”** (logo Fairy Tapes / The Stripper) per angolo o footer.

---

## 3. Checklist rapida

- [ ] **Background** pannello 480×720 (o 960×1440), 2–3 varianti texture.
- [ ] **Dividers** sezioni (o integrati nello sfondo).
- [ ] **Knob** 80×80 (e opzionale versione piccola ~48×48).
- [ ] **Fader** (opzionale) track + cap.
- [ ] **Button** bypass (on/off).
- [ ] **VU/level meter** (strip o barra).
- [ ] **Labels** (immagini o specifica font/colore).

---

## 4. Formato file e naming

- **Formato:** PNG con trasparenza dove serve (knob, button, meter). PSD accettabile per consegna con layer.
- **Naming suggerito:**  
  `panel_bg_v1.png`, `panel_bg_v2.png`, `knob_main.png`, `knob_small.png`, `button_bypass.png`, `meter_vu.png`, `divider.png`.  
  Se usi 2x: `panel_bg_v1_2x.png`, ecc.
- **Cartella:** le risorse finite possono stare in `src/the-stripper/resources/` (es. `resources/graphics/`).

Quando hai le prime bozze (anche solo background + un knob + un button), si può passare al passo successivo: integrazione nell’editor VST3 (createView, disegno e binding parametri).
