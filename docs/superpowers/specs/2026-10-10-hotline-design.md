# Hotline — design

Plugin VST3 stereo di effetti "da dispositivo": megafono, telefono, walkie-talkie, radio AM, PA da stazione, altoparlante economico. Pensato soprattutto per la voce; usabile su altre sorgenti e sul master per le intro effettate. Suite Fairy Tapes, stile e regole in `docs/DESIGN_SYSTEM.md`.

## 1. Intento e criteri di riuscita

- **Uso principale:** voce (voice-over, cantato). **Secondario:** strumenti e master, per le intro effettate (dal suono pulito al dispositivo con una sola curva di automazione).
- **Riuscito se:** i dispositivi sono riconoscibili e credibili sulla voce; il Morph si automatizza senza scatti; sul master il plugin non introduce latenza né sporca il resto del mix; aggiungere un dispositivo costa una riga di tabella, non nuovo codice.

## 2. Architettura del motore (approccio A, deciso)

Una **catena unica parametrica**. Ogni dispositivo è una tabella di valori per i blocchi della catena; Morph interpola da "tutto neutro" a quella tabella. Scartati: classi DSP separate per dispositivo (Morph incoerente, codice moltiplicato) e convoluzione con IR (licenze, il drive non sta nella convoluzione).

Catena, in ordine:

1. Somma mono (solo con Mono attivo)
2. Banda: HPF e LPF biquad minimum-phase
3. Drive: waveshaper asimmetrico
4. Cono: peaking EQ e, per il megafono, risonanza a tromba
5. Crush: decimazione e bit depth (effetto codec)
6. Compander e squelch (walkie-talkie)
7. Rumore: filtrato più crackle
8. Room: riverbero corto (PA)
9. Mix e Gain di uscita

- **Morph = 0:** ogni blocco è neutro, la catena è trasparente. **Morph = 1:** i blocchi hanno i valori del dispositivo. L'interpolazione è per dominio: frequenze in scala logaritmica, livelli in dB.
- **Latenza zero:** la banda sta prima del drive, quindi il segnale che arriva al waveshaper è già limitato e non serve oversampling.
- **Mono:** tasto manuale, spento all'avvio, **non** legato al dispositivo (deciso). Con Mono attivo si somma L+R, si elabora e lo stesso segnale va su entrambe le uscite. Con Mono spento L e R sono elaborati separatamente, con rumore e risonanza decorrelati.
- **Cambio di Device:** crossfade di circa 40 ms tra i valori dei blocchi, senza scatti.

### Dispositivi

Megaphone, Telephone, Walkie-talkie, AM radio, Station PA, Cheap speaker. I valori di ogni tabella si regolano a orecchio durante l'implementazione.

## 3. Parametri (ID fissati da subito, tabella congelata come The Stripper)

Device, Morph, Gain, Mono, Bypass, Drive, Tone, Noise, Room, Mix, IN di CHARACTER. I controlli sono gli stessi per tutti i dispositivi; cambiano i valori e, in alcuni casi, un blocco escluso (il feedback a tromba serve solo al megafono).

## 4. Interfaccia

Pannello rack orizzontale 1000 × 428 nel linguaggio comune. Materiale e texture sono quelli di The Stripper, uguali per tutti i plugin (decisione del 2026-10-10). Tutto il testo è in inglese. Preview di riferimento: `src/hotline/docs/mockups/hotline_rack.html` (derivata da `stripper_rack.html`).

- **Riga 1:** DEVICE (sei pulsanti su due righe, un LED indica il dispositivo attivo; nessun nastro), LEVEL (meter IN e OUT stereo e LED di clip IN, DRV, OUT), OUTPUT (Gain, Bypass in alto a destra, Mono).
- **Riga 2:** MORPH (manopola grande) e un solo blocco CHARACTER con Drive, Tone, Noise, Room e Mix.
- **IN:** un solo pulsante IN per CHARACTER, che attenua tutti e cinque i controlli (Mix compreso). Il Bypass globale attenua tutto e fa passare il segnale identico, bit per bit.
- Usura, viti, retro con SETTINGS e CREDITS come da design system. La preview ha ancora il retro di The Stripper, non adattato.

## 5. Stato e preferenze

- Stato nel progetto: parametri, seme dell'usura, disposizione di viti e nastro. Parte dalla versione 1; una versione più recente carica i default.
- Preferenze di aspetto (Wear, font) nel file globale dell'utente.

## 6. Robustezza

- Parametri smussati; automazione del Morph senza scatti.
- Rumore da seme per istanza (riproducibile nei test). Nessuna allocazione nel thread audio, protezione dai denormali.
- Coefficienti ricalcolati a ogni sample rate; LPF limitato sotto Nyquist.
- Input NaN o infinito diventa 0 e azzera lo stato dei filtri. Parametri fuori range vengono limitati.

## 7. Test (scritti prima del codice, devono prima fallire)

- Morph a 0: uscita uguale all'ingresso.
- Bypass: uscita identica bit per bit.
- Mono attivo: L e R identici.
- Telephone: attenuazione misurata sotto 300 Hz e sopra 3,4 kHz.
- Parametri a fondo scala e input estremi: niente NaN, segnale limitato.
- Latenza zero: un impulso nel primo campione esce nel primo campione.
- Salvataggio e ripristino dello stato: stessi parametri.
- Rumore identico a parità di seme.

## 8. Decisioni aperte

- Valori delle tabelle dei sei dispositivi (da regolare a orecchio).
- Se Mix debba restare attivo a IN di CHARACTER spento (ora si attenua con gli altri).
- Se tenere i meter LEVEL (aggiunti per coerenza con The Stripper, non nei parametri iniziali).
- Retro: crediti e licenze di Hotline da scrivere (la preview ha ancora quelli di The Stripper).
- Nessun supporto CUDA: Hotline non usa NAM.

## 9. Processo

Branch `feat/hotline`, sganciato da `pre-release`, in un worktree dedicato. Prossimi passi: piano di implementazione in `docs/superpowers/plans/2026-10-10-hotline.md`, poi codice con test.
