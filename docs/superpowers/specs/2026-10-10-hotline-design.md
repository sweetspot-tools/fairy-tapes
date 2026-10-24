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

### 2.1 Controlli utente e valori iniziali (fissati dal piano)

- Drive, Noise e Room moltiplicano il valore del dispositivo per `2·n` (n = 0,5 → ×1). Tone sposta l'LPF di `(n−0,5)·2` ottave e l'HPF di `−(n−0,5)` ottave. Gain = `−20 + 40·n` dB. Mix è dry/wet dell'intero dispositivo.
- Default: Device 0, Morph 1, Gain 0,5, Mono 0, Drive 0,5, Tone 0,5, Noise 0,5, Room 0,5, Mix 1, IN 1, Bypass 0.
- **IN di CHARACTER spento:** Drive, Tone, Noise, Room e Mix sono ignorati (restano banda, cono, crush e compander del dispositivo, Mix = 1). Risolve la decisione aperta su Mix.
- Valori iniziali dei sei dispositivi: tabella nel piano (`docs/superpowers/plans/2026-10-10-hotline.md`), da regolare a orecchio.

### Dispositivi

Megaphone, Telephone, Walkie-talkie, AM radio, Station PA, Cheap speaker. I valori di ogni tabella si regolano a orecchio durante l'implementazione.

## 3. Parametri (ID fissati da subito, tabella congelata come The Stripper)

Device, Morph, Gain, Mono, Bypass, Drive, Tone, Noise, Room, Mix, IN di CHARACTER. I controlli sono gli stessi per tutti i dispositivi; cambiano i valori e, in alcuni casi, un blocco escluso (il feedback a tromba serve solo al megafono).

## 4. Interfaccia

Pannello rack orizzontale 1000 × 428 nel linguaggio comune. Materiale e texture sono quelli di The Stripper, uguali per tutti i plugin (decisione del 2026-10-10). Tutto il testo è in inglese. Preview di riferimento: `src/hotline/docs/mockups/hotline_rack.html` (derivata da `stripper_rack.html`).

- **Riga 1:** DEVICE (sei pulsanti su due righe con il nome sulla faccia: il dispositivo attivo ha la faccia ambra, nessun LED — regola comune dei tre tipi di pulsante del 2026-10-21; pulsanti un po' più stretti, preview 2026-10-21; nessun nastro), LEVEL (meter IN e OUT stereo e LED di clip IN, DRV, OUT: tenuti, deciso il 2026-10-24), OUTPUT (Gain, Bypass in alto a destra, Mono).
- **Riga 2:** MORPH (manopola grande) e un solo blocco CHARACTER con Drive, Tone, Noise, Room e Mix (manopole un po' più ravvicinate e IN del blocco appena più in basso, preview 2026-10-21; editor da allineare).
- **IN:** un solo pulsante IN per CHARACTER, che attenua tutti e cinque i controlli (Mix compreso). Il Bypass globale attenua tutto e fa passare il segnale identico, bit per bit.
- Usura, viti e retro (SETTINGS, DEVICES, ROUTING, CREDITS) come da design system. La preview ha SETTINGS, ROUTING e CREDITS, ma i crediti sono ancora quelli di The Stripper e manca la scheda DEVICES.
- **Retro, scheda DEVICES (deciso):** oltre a SETTINGS e CREDITS, il retro ha una scheda **DEVICES** che spiega cosa fa ciascun pulsante di Device e **quali valori impone** (frequenze di HPF e LPF, drive, risonanza del cono, crush, compander, rumore, room e relative note). La scheda è generata dalla stessa tabella dei dispositivi usata dal motore: i valori mostrati non possono divergere da quelli applicati.
- **Retro, scheda ROUTING (regola comune del 2026-10-23, preview fatta):** schema a blocchi fisso della catena del §2, nello stile di `docs/DESIGN_SYSTEM.md`: IN → meter IN → MONO (somma L+R, disegnata col deviatore in posizione spento) → presa del dry → BAND → DRIVE (clip DRV) → CONE → CRUSH → COMPANDER → somma del NOISE → ROOM → MIX (dry dalla presa dopo MONO) → Gain di OUTPUT → BYPASS globale (dall'ingresso) → meter OUT; DEVICE · MORPH come linea di controllo tratteggiata verso i blocchi del dispositivo; CHARACTER e il suo IN spiegati nella legenda (non cambiano il percorso). Ordine dei link: SETTINGS, DEVICES (quando ci sarà nella preview), ROUTING, CREDITS. Da confermare col codice: punto in cui si preleva il dry del MIX (assunto dopo la somma mono, prima della BAND).

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
- La scheda DEVICES elenca tutti i dispositivi della tabella con i valori effettivi (nessun dispositivo mancante, nessun valore scritto a mano).
- Rumore identico a parità di seme.

## 8. Decisioni aperte

- **Altezza del pannello e retro:** il pannello è 1000 × 428, ma i template del retro in `shared/ui/include/fairy_ui/back_panel.h` esistono solo per 628 (`kTall`) e 470 (`kShort`). Con 428 serve un terzo template (modifica comune, in `shared/`) oppure si porta Hotline a 470 come Pillow Talk. Da decidere prima del Task 8.
- Punto da cui si preleva il dry del MIX (assunto dopo la somma mono, prima della BAND): si conferma nel Task 5 e, se cambia, si aggiorna lo schema ROUTING.

Decise: meter LEVEL tenuti (2026-10-24); valori iniziali dei dispositivi nel piano, da affinare a orecchio durante l'implementazione; crediti e licenze di Hotline scritti nel Task 8 (la preview ha ancora quelli di The Stripper).

## 9. Processo

Si lavora su `pre-release` (nessun branch dedicato, regola di `CLAUDE.md` §1). Piano: `docs/superpowers/plans/2026-10-10-hotline.md`; poi codice con test. Il kit grafico comune è in `shared/ui` (usura, viti, nastro, retro, controlli rack, routing).
