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
- **Tone scalato dal Morph (deciso nel Task 5):** lo spostamento è di `tone·morph` ottave (LPF `+tone·morph`, HPF `−tone·morph/2`), con `tone = 2·(n−0,5)`. A Morph 1 vale esattamente la regola sopra; a Morph 0 la banda resta neutra (20 Hz / 20 kHz) qualunque sia Tone. Senza la scala, Tone a −1 lascerebbe un LPF a 10 kHz con Morph 0, e la catena non sarebbe più trasparente.
- **Dissolvenza della banda:** l'uscita della banda è `x + w·(banda(x) − x)`, con `w` = distanza in ottave delle frequenze di taglio da 20 Hz / 20 kHz divisa per 0,5 (limitata a 1). A `w = 0` la banda è saltata; quando si riaccende riparte con lo stato azzerato. Nasconde la partenza a freddo dei filtri quando la banda lascia il neutro (scatto fino a 0,3–0,6 su un seno forte durante un'automazione lenta del Morph) e rende la banda neutra a qualunque sample rate.
- **Limite di uscita:** il percorso elaborato è limitato a ±16 (+24 dBFS), per esempio Mix 0 con Gain +20 dB su un ingresso a ±10. La copia esatta del Bypass non è limitata.
- **Bypass e campioni non finiti:** a regime il Bypass copia l'ingresso bit per bit, ma i campioni NaN o infiniti escono come 0 (come in tutto il motore).
- **Blocco della continua dopo il Drive:** il waveshaper asimmetrico aggiunge continua. Subito dopo il Drive c'è un passa-alto a un polo a circa 10 Hz, attivo solo con drive' > 0 (saltato esattamente a drive' = 0, stato azzerato alla riaccensione).
- **Tetto del rumore:** il livello del rumore è limitato a −20 dB (i picchi del crackle crescono con √fs). Noise moltiplica l'ampiezza: `noiseDb' = noiseDb + 20·log10(2n)`; spento a n = 0 o se il fondo del dispositivo morfato è già a −120 dB.
- **Room additivo e spread stereo:** `y = x + amount·wet`, con il wet dei comb a guadagno 0,125. Il Room del canale R ha uno spread stereo (stile Freeverb: `round(23·fs/44100)` campioni in più su ogni ritardo), così le code di L e R sono decorrelate anche con una sorgente al centro; con Mono attivo le uscite restano identiche.
- **Rumore fermo quando è muto:** a −120 dB il generatore restituisce esattamente 0 e non avanza.

### Dispositivi

Megaphone, Telephone, Walkie-talkie, AM radio, Station PA, Cheap speaker. I valori di ogni tabella si regolano a orecchio durante l'implementazione.

## 3. Parametri (ID fissati da subito, tabella congelata come The Stripper)

Device, Morph, Gain, Mono, Bypass, Drive, Tone, Noise, Room, Mix, IN di CHARACTER. I controlli sono gli stessi per tutti i dispositivi; cambiano i valori e, in alcuni casi, un blocco escluso (il feedback a tromba serve solo al megafono).

## 4. Interfaccia

Pannello rack orizzontale 1000 × 470 (altezza di Pillow Talk, così il retro usa il template `kShort` del kit; deciso il 2026-10-24 dopo averla vista in preview, contro i 428 iniziali) nel linguaggio comune. Materiale e texture sono quelli di The Stripper, uguali per tutti i plugin (decisione del 2026-10-10). Tutto il testo è in inglese. Preview di riferimento: `src/hotline/docs/mockups/hotline_rack.html` (derivata da `stripper_rack.html`).

- **Riga 1:** DEVICE (sei pulsanti su due righe con il nome sulla faccia: il dispositivo attivo ha la faccia ambra, nessun LED — regola comune dei tre tipi di pulsante del 2026-10-21; pulsanti un po' più stretti, preview 2026-10-21; nessun nastro), LEVEL (meter IN e OUT stereo e LED di clip IN, DRV, OUT: tenuti, deciso il 2026-10-24), OUTPUT (Gain, Bypass in alto a destra, Mono).
- **Riga 2:** MORPH (manopola grande) e un solo blocco CHARACTER con Drive, Tone, Noise, Room e Mix (manopole un po' più ravvicinate e IN del blocco appena più in basso, preview 2026-10-21; editor da allineare).
- **IN:** un solo pulsante IN per CHARACTER, che attenua tutti e cinque i controlli (Mix compreso). Il Bypass globale attenua tutto e fa passare il segnale identico, bit per bit.
- Usura, viti e retro (SETTINGS, DEVICES, ROUTING, CREDITS) come da design system. **Viti** ai quattro angoli del pannello 470 (y 18 e y 452) e **usura** scalata al pannello 470; sei posizioni fisse per i nastri di carta (`kTapeSlots` in `ui/layout.h`), mai sopra i controlli. Le viti ci sono anche sul retro e condividono con il fronte le rotazioni salvate (scelta dell'implementatore: in Pillow Talk il retro non ne ha).
- **Preview:** `src/hotline/docs/mockups/hotline_rack.html` ha ancora il retro di 628 di altezza derivato da The Stripper e manca la scheda DEVICES; il riferimento attuale del retro è la resa dell'editor (`hotline_render`). Decisione aperta in §8.
- **Retro, SETTINGS (realizzato):** APPEARANCE con WEAR (Off / Light / Heavy) e TAPE FONT (Zeyada / Rock Salt / Homemade Apple), nel file delle preferenze; il riquadro **BEHAVIOUR è vuoto**, perché Hotline non ha ancora impostazioni di comportamento (nessun controllo inerte). NOTES LAYER, VALUES FONT e RE-ROLL non sono offerti (Hotline non ha strato di note né valori scritti a mano).
- **Retro, scheda DEVICES (deciso):** oltre a SETTINGS e CREDITS, il retro ha una scheda **DEVICES** che spiega cosa fa ciascun pulsante di Device e **quali valori impone** (frequenze di HPF e LPF, drive, risonanza del cono, crush, compander, rumore, room e relative note). La scheda è generata dalla stessa tabella dei dispositivi usata dal motore: i valori mostrati non possono divergere da quelli applicati.
- **Retro, scheda ROUTING (regola comune del 2026-10-23, preview fatta):** schema a blocchi fisso della catena del §2, nello stile di `docs/DESIGN_SYSTEM.md`: IN → meter IN → MONO (somma L+R, disegnata col deviatore in posizione spento) → presa del dry → BAND → DRIVE (clip DRV) → CONE → CRUSH → COMPANDER → somma del NOISE → ROOM → MIX (dry dalla presa dopo MONO) → Gain di OUTPUT → BYPASS globale (dall'ingresso) → meter OUT; DEVICE · MORPH come linea di controllo tratteggiata verso i blocchi del dispositivo; CHARACTER e il suo IN spiegati nella legenda (non cambiano il percorso). Ordine dei link: SETTINGS, DEVICES, ROUTING, CREDITS. Nell'editor (altezza 470, template `kShort`) lo schema ha le stesse voci e lo stesso ordine della preview ma con le righe compresse; la legenda sta in **una sola riga** (il riquadro LEGEND di `kShort` ne ha una), e il segno di somma del NOISE è disegnato in locale (cerchio con un più), perché il kit non ha il segno né `LegendSymbol::Sum`: si suggerisce `RoutingBuilder::sum` / `LegendSymbol::Sum` in `shared/ui` (richiesta a chi ha in carico `shared/`). Presa del dry del MIX: dopo la somma mono e prima della BAND (confermata dal motore nel Task 5).

## 5. Stato e preferenze

- Stato nel progetto, **versione 2**: il payload v1 (`int32` versione + 11 `double` normalizzati) seguito da seme dell'usura (`uint32`), 4 angoli delle viti (`float32`, gradi: la rotazione dell'utente sommata a quella del seme) e i testi dei nastri (conteggio, poi lunghezza + byte; ASCII stampabile, al più 14 caratteri, uno per posizione di nastro: sei). Stesso schema del v2 di Pillow Talk (copiato, non condiviso). Il v1 si carica ancora (decor ai default, seme 0 = ne viene generato uno fuori dal thread audio); versione sconosciuta, dati corti o parametri non finiti: stato ignorato.
- Preferenze di aspetto nel file globale dell'utente `FairyTapes/Hotline.prefs` (Linux `$XDG_CONFIG_HOME` se assoluto, altrimenti `~/.config`; macOS `~/Library/Application Support`; Windows `%APPDATA%`), righe `chiave=valore`, chiavi `wear` (Off/Light/Heavy), `notes`, `tapeFont` (Zeyada/RockSalt/HomemadeApple), `valuesFont`; `notes` e `valuesFont` sono salvate ma Hotline non le usa. Valori non validi: default campo per campo; scrittura atomica.

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

- Significato del LED di clip DRV: oggi si accende quando il waveshaper è sul fondo scala e, con drive' = 0, quando l'uscita della banda arriva a |x| ≥ 1. Soglia da decidere; proposta: |uscita del waveshaper| ≥ 0,9, solo con drive' > 0.
- **Posizioni dei sei nastri di carta:** scelte dall'implementatore (intestazione, fascia tra LEVEL e CHARACTER, LEVEL a destra dei LED, due su CHARACTER, fascia sotto i riquadri), verificate solo da test (mai sopra controlli, meter, LED, viti o «BACK →»). **Da confermare con l'utente** guardando il render.
- **Preview del retro:** aggiornare `hotline_rack.html` al layout 470 con le quattro schede (SETTINGS, DEVICES, ROUTING, CREDITS), oppure lasciare la resa dell'editor come riferimento.
- **Segno di somma nel kit:** `RoutingBuilder::sum` / `LegendSymbol::Sum` in `shared/ui` (richiesta al proprietario di `shared/`; oggi disegnato in locale). Analogamente un parametro di spostamento del centro per `RackKnob` (oggi `BigRackKnob` locale per la manopola del Morph).
- **CI:** Hotline non è nel workflow `.github/workflows/build.yml`; aggiungerlo è una decisione dell'utente.

Note tecniche rinviate (una riga ciascuna):
- LED DRV: vedi la prima decisione aperta qui sopra.
- `band.h` giudica il neutro dopo il limite `0,45·fs`: sotto circa 43,3 kHz di sample rate la banda non risulta mai neutra; il motore lo aggira con il peso di banda (`engine.cpp`).

Decise: pannello alto 470 (2026-10-24); meter LEVEL tenuti (2026-10-24); valori iniziali dei dispositivi nel piano, da affinare a orecchio durante l'implementazione; crediti e licenze di Hotline scritti nel Task 8 (la preview ha ancora quelli di The Stripper).

## 9. Processo

Si lavora su `pre-release` (nessun branch dedicato, regola di `CLAUDE.md` §1). Piano: `docs/superpowers/plans/2026-10-10-hotline.md`; poi codice con test. Il kit grafico comune è in `shared/ui` (usura, viti, nastro, retro, controlli rack, routing).
