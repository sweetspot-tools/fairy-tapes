# Pillow Talk — Controlli (parametri)

Elenco di **tutti i parametri** esposti all'host (lista standard, nessuna UI custom, come The Stripper). Ordine **per ID**. Lo stato (versione + 16 valori normalizzati) si salva e si ricarica con il progetto.

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
| 8 | `kMix` | Mix | 0 … 100 % | 30 % | `out = dry·(1−mix) + wet·mix`. Smussato. Su un bus di ritorno usare 100 %. |
| 9 | `kThreshold` | Threshold | −60 … 0 dB | −34.8 dB | Soglia del rilevatore vocale (passa-banda 120 Hz – 5 kHz). |
| 10 | `kDuckDepth` | Ducking Depth | 0 … 60 dB (60 = Mute) | 36 dB | Riduzione della mandata mentre la voce canta. 0 dB = delay normale. |
| 11 | `kHoldSync` | Hold Sync | Off / On | Off | On: Hold segue **Hold Note** e il BPM dell'host. |
| 12 | `kHoldMs` | Hold | 50 … 1000 ms (log) | 224 ms | Mantiene la mandata abbassata dopo l'ultimo istante sopra soglia. |
| 13 | `kHoldNote` | Hold Note | 1/16, 1/8, 1/4, 1/2 | 1/8 | Usato con Hold Sync On. |
| 14 | `kRelease` | Release | 50 … 2000 ms (log) | 316 ms | Risalita esponenziale della mandata dopo l'hold. |
| 15 | `kSidechain` | Sidechain | Off / On | Off | On: il rilevatore ascolta il bus sidechain, se attivo e collegato; altrimenti l'ingresso principale. |

## Comportamento

- La riduzione si applica **all'ingresso del delay**: le code già emesse decadono in modo naturale.
- Senza BPM dall'host i valori in note usano **120 BPM**.
- Sidechain collegato ma silenzioso: il delay resta aperto.
- Cambi di sample rate o dimensione del blocco: i buffer si ricalcolano all'attivazione.

## Sidechain negli host

- **Reaper:** il sidechain è sui **canali 3/4** del plugin. Impostare la traccia (o il bus di ritorno) a 4 canali, creare una mandata dalla voce verso i canali 3/4 della traccia con Pillow Talk, poi attivare **Sidechain**.
- **Cubase:** attivare il sidechain dal plugin (pulsante sidechain nell'intestazione della finestra del plugin), instradare la voce sull'ingresso sidechain, poi attivare **Sidechain**.
- Come insert sulla voce non serve sidechain: lasciare **Sidechain** Off.
