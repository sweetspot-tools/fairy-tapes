# Pillow Talk — Controlli (parametri)

Elenco di **tutti i parametri** esposti all'host. Il plugin ha un editor proprio (480 x 560, vedi sotto); i parametri restano comunque visibili e automatizzabili dalla lista standard dell'host. Ordine **per ID**. Lo stato (versione + 16 valori normalizzati) si salva e si ricarica con il progetto.

**Totale: 16 parametri** (ID 0…15).

| ID | Simbolo | Titolo (host) | Range | Default | Note |
|----|---------|---------------|-------|---------|------|
| 0 | `kBypass` | Bypass | Off / On | Off | Bypass globale (flag VST3 `kIsBypass`). Il mix scende a 0 in ~20 ms e resta il segnale dry. |
| 1 | `kTimeSync` | Time Sync | Off / On | Off | On: il tempo segue **Time Note** e il BPM dell'host. |
| 2 | `kTimeMs` | Time | 20 … 1200 ms (log) | 155 ms | Tempo del delay quando Time Sync è Off. Il cambio è in glide (pitch-bend da nastro, nessun click). |
| 3 | `kTimeNote` | Time Note | 1/16, 1/8, 1/4, 1/2 | 1/4 | Usato con Time Sync On. Ritardo massimo del nastro 2 s. |
| 4 | `kFeedback` | Feedback | 0 … 95 % | 38 % | Ripetizioni. Limitato a 95 % per evitare autoscillazione. |
| 5 | `kTone` | Tone | 800 … 12000 Hz (log) | 4062 Hz | Passa-basso nel loop: ogni ripetizione è più scura. |
| 6 | `kSaturation` | Saturation | 0 … 100 % | 30 % | Saturazione `tanh` con bias, oversampling 2x. |
| 7 | `kWowFlutter` | Wow/Flutter | 0 … 100 % | 20 % | Modulazione lenta (wow) e veloce/casuale (flutter) del tempo. |
| 8 | `kMix` | Mix | 0 … 100 % | 30 % | `out = dry·(1−mix) + wet·gain·mix` (gain = ducking). Smussato. Su un bus di ritorno usare 100 %. |
| 9 | `kThreshold` | Threshold | −60 … 0 dB | −34.8 dB | Soglia del rilevatore vocale (passa-banda 120 Hz – 5 kHz). |
| 10 | `kDuckDepth` | Ducking Depth | 0 … 60 dB (60 = Mute) | 36 dB | Riduzione del ritorno wet mentre la voce canta. 0 dB = delay normale. |
| 11 | `kHoldSync` | Hold Sync | Off / On | Off | On: Hold segue **Hold Note** e il BPM dell'host. |
| 12 | `kHoldMs` | Hold | 50 … 1000 ms (log) | 224 ms | Mantiene il wet abbassato dopo l'ultimo istante sopra soglia. |
| 13 | `kHoldNote` | Hold Note | 1/16, 1/8, 1/4, 1/2 | 1/8 | Usato con Hold Sync On. |
| 14 | `kRelease` | Release | 50 … 2000 ms (log) | 316 ms | Risalita esponenziale del wet dopo l'hold. |
| 15 | `kSidechain` | Sidechain | Off / On | Off | On: il rilevatore ascolta il bus sidechain, se attivo e collegato; altrimenti l'ingresso principale. |

## Parametri di display (nascosti)

Tre parametri di sola lettura e nascosti (`kIsReadOnly | kIsHidden`), fuori dalla tabella persistente e non salvati nello stato. Il processore li scrive circa ogni 16 ms, l'editor li legge ogni 30 ms.

| ID | Simbolo | Valore normalizzato | Default |
|----|---------|---------------------|---------|
| 1000 | `kDispVoice` | picco della voce, −60 … 0 dB | 0 |
| 1001 | `kDispSend` | gain di ducking applicato al wet, lineare 0 … 1 | 1 |
| 1002 | `kDispEcho` | picco del ritorno wet, −60 … 0 dB | 0 |

## Editor

- Layout: intestazione (titolo, Sidechain, Bypass), display del ducking (voce grigia, eco ambra, linea verde della mandata, linea tratteggiata della soglia), sezione Delay (6 manopole + Sync e note del Time), sezione Auto Duck (4 manopole + Sync e note dell'Hold).
- Gesti: trascinamento verticale, **Maiusc** = fine, **doppio clic** = default.
- Sync On attenua la manopola ms; Sync Off attenua i pulsanti nota. I controlli attenuati **rispondono comunque**.
- Se l'host non inoltra i parametri di display al controller, il display resta fermo; il plugin funziona normalmente.
- Linux: solo embedding X11 (Wayland nativo rifiutato; Reaper usa X11/XWayland).
- Grafiche: [UI_ASSETS.md](UI_ASSETS.md).

## Verifica manuale

- [ ] Aprire e chiudere l'editor 50 volte.
- [ ] Muovere ogni controllo: automazione e undo corretti.
- [ ] Sync On/Off durante l'automazione di Time e Hold.
- [ ] Display con voce reale e con sidechain.
- [ ] Finestra HiDPI.
- [ ] Pulsanti nota attenuati cliccabili (voluto).
- [ ] Linux: X11/XWayland in Reaper.

## Comportamento

- La riduzione si applica **all'uscita wet**: il delay registra sempre la frase, la coda continua a decadere nella linea e si sente quando il gate si riapre dopo Hold e Release. Con Feedback basso e Hold/Release lunghi la parte iniziale della coda decade mentre il gate è ancora chiuso.
- Senza BPM dall'host i valori in note usano **120 BPM**.
- Sidechain collegato ma silenzioso: il delay resta aperto.
- Cambi di sample rate o dimensione del blocco: i buffer si ricalcolano all'attivazione.

## Sidechain negli host

- **Reaper:** il sidechain è sui **canali 3/4** del plugin. Impostare la traccia (o il bus di ritorno) a 4 canali, creare una mandata dalla voce verso i canali 3/4 della traccia con Pillow Talk, poi attivare **Sidechain**.
- **Cubase:** attivare il sidechain dal plugin (pulsante sidechain nell'intestazione della finestra del plugin), instradare la voce sull'ingresso sidechain, poi attivare **Sidechain**.
- Come insert sulla voce non serve sidechain: lasciare **Sidechain** Off.
