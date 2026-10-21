# Integrazione TONE3000 — spec (bozza, 2026-10-21)

Spec separata richiesta da `2026-10-09-the-stripper-completion-design.md` §3.1 e §10. Parte dai fatti verificati lì (termini API del 7 luglio 2026, documentazione API v1); **non si ripetono qui**: leggere prima §10 di quella spec.

**Decisione presa (2026-10-21):** i plugin sono gratuiti e non commerciali (GPL-3.0), quindi si usa il **piano gratuito** dell'API. Il piano gratuito comprende solo i flussi OAuth `select_tone` e `load_tone` e gli elenchi limitati (preferiti, scaricati, creati, di tendenza, ultimi); la ricerca (`/tones/search`) **non** c'è.

## 1. Obiettivo e perimetro

- Dalla scheda **TONE3000** del popup dei modelli (spec Stripper §3.1) l'utente accede al proprio account, sceglie un tono sulla pagina di TONE3000 o dagli elenchi permessi, scarica **un modello alla volta, su sua richiesta**, e lo carica nel Drive. Il file resta nel suo computer come un `.nam` locale.
- **Fuori perimetro:** ricerca interna, caricamento (upload) di toni, preferiti/commenti dal plugin, scarico dello zip del tono (riservato ai partner), cache o copia del catalogo, modelli dentro il binario, accordo commerciale.
- Il componente è **comune**: codice in `shared/tone3000/` (nessuna dipendenza da VSTGUI né dall'SDK VST3), la scheda del popup è dello Stripper; Doggystyle e Hotline potranno riusarlo con la propria scheda. Per ora lo usa solo The Stripper.

## 2. Flusso dell'utente

1. **Prima volta:** la scheda dice "Sign in to TONE3000" con il pulsante **SIGN IN**. Il clic apre nel browser di sistema la pagina di autorizzazione (OAuth 2.0 + PKCE); l'utente accede e approva. Il plugin riceve il codice, lo scambia con il token e mostra il nome dell'utente. Il token è per quell'utente e si usa solo per lui.
2. **Scelta del tono:** pulsante **CHOOSE ON TONE3000** (flusso `select_tone`): si apre il browser sulla pagina di scelta di TONE3000, l'utente sceglie, la callback restituisce il `tone_id` e il plugin passa direttamente alla scelta del modello. In alternativa gli elenchi **FAVORITES, DOWNLOADED, CREATED, TRENDING, LATEST** (a schede piccole o menu), con pagine da 25.
3. **Scelta del modello:** per il tono scelto si mostra l'elenco dei suoi modelli (`/models?tone_id=`), ognuno con taglia (`standard`, `lite`, `feather`, `nano`, `custom`), architettura (A1/A2), formato; i modelli non `nam` e quelli che il core non sa leggere sono attenuati e non selezionabili.
4. **DOWNLOAD & LOAD:** scarica il file con il `model_url` e il token, lo salva nella cartella locale dei toni (§5), poi lo carica come qualsiasi modello locale (percorso asincrono, spec Stripper §3). Il file compare anche nella scheda LOCAL (la cartella è una sorgente aggiunta automaticamente).
5. **SIGN OUT:** cancella il token e i dati dell'utente tenuti dal plugin (richiesto dai termini quando l'utente revoca l'accesso o lo chiede).

Ogni riga e il pannello del modello mostrano **nome del tono, autore** (`display_name`, segno di verifica) **e licenza** (`license`, in un badge: `t3k`, `CC BY`, `CC BY-NC`, `CC0`, ...). Un tono con licenza che richiede attribuzione la mostra anche nel suggerimento e nei metadati salvati (§5). In fondo alla scheda c'è sempre **"Powered by TONE3000"** (marchio come da linee guida, collegamento a tone3000.com) e un link "Browse the full catalog" che apre il sito.

Nessuna chiamata di rete a popup chiuso, né all'apertura del plugin. Le liste si scaricano quando si apre la scheda o si cambia elenco; vivono **solo in memoria** per la sessione (nessun file di cache del catalogo).

## 3. Architettura

```
shared/tone3000/
  pkce.h/.cpp            code_verifier, code_challenge (SHA-256, base64url), state casuale
  oauth_flow.h/.cpp      macchina a stati: Idle → WaitingBrowser → Exchanging → SignedIn / Failed
  loopback_server.h/.cpp server HTTP minimo su 127.0.0.1:<porta> che riceve ?code=&state= e risponde con una pagina "You can close this tab"
  http.h                 interfaccia HttpClient (GET/POST, intestazioni, corpo, scarico su file con avanzamento, annullabile)
  http_curl.cpp          implementazione con libcurl (vedi §7)
  api.h/.cpp             Tone3000Api: tone(id), models(tone_id), lists, download(model) — JSON con nlohmann/json
  rate_limiter.h        finestra scorrevole, 60 richieste/min (il limite è 100)
  token_store.h/.cpp     lettura/scrittura atomica del token (§6)
  metadata.h/.cpp        file laterale `.t3k.json` del modello scaricato
  worker.h/.cpp          un thread con coda di comandi e risultati; l'interfaccia legge lo stato a scadenze regolari
src/the-stripper/src/ui/tone3000_tab.{h,cpp}   la scheda nel popup (usa Worker, come ModelBrowser con la scansione)
```

- **Thread:** tutta la rete e il disco passano dal `Worker` (un thread dedicato, mai il thread audio né quello dell'interfaccia). L'interfaccia legge uno snapshot dello stato (mutex) con il timer che già usa il popup, senza callback sul thread di rete.
- **Interfacce iniettabili:** `HttpClient` e il browser (apertura dell'URL) sono interfacce, così i test usano finti e il codice non conosce libcurl.
- **`ModelSource`:** la scheda è una seconda sorgente dietro l'interfaccia comune dello Stripper (elenco, risoluzione del percorso locale del file); la differenza è che la risoluzione può scaricare. Il caricamento nel Drive resta quello locale.
- **Chiavi:** nel client c'è solo la chiave **pubblicabile** (`client_id`), in una costante di compilazione. La chiave segreta (`t3k_cs_...`) non entra mai nel repository né nel binario. Il flusso PKCE non la richiede.

## 4. Flusso OAuth nel dettaglio

- Apertura del browser: `xdg-open` (Linux), `ShellExecuteW` (Windows), `open` (macOS), con l'URL `/api/v1/oauth/authorize?client_id=...&redirect_uri=...&code_challenge=...&code_challenge_method=S256&state=...&prompt=select_tone` (omesso per l'accesso base).
- Ritorno: server locale su `127.0.0.1`, porta libera scelta dal sistema, aperto **solo** durante l'accesso (timeout 5 minuti, chiuso a fine flusso o con ANNULLA). Verifica di `state`, una sola richiesta accettata, risposta senza contenuti esterni. **Dipende dalla risposta del supporto:** la documentazione cita i deep link di app native; l'URI di ritorno deve essere registrato per l'app e potrebbe non ammettere `http://127.0.0.1:<porta>`.
- Piano B se il ritorno su loopback non è ammesso: **flusso del dispositivo** (RFC 8628): il plugin mostra un codice, l'utente lo approva su `tone3000.com/activate`. Ma il flusso del dispositivo **supporta solo l'accesso base**: la scelta del tono si farebbe dagli elenchi (preferiti, scaricati, ...) e da un campo per incollare il collegamento/ID del tono, non da `select_tone`.
- Token: scambio su `/api/v1/oauth/token`, rinnovo con `grant_type=refresh_token`; se il rinnovo fallisce si riparte dall'accesso. Il token si usa solo per le chiamate dell'utente che lo ha concesso.

## 5. File scaricati

- Cartella predefinita: `<cartella delle preferenze di Fairy Tapes>/tone3000/` (modificabile dalle impostazioni del retro più avanti), sottocartella per tono: `<tone_id>-<nome sicuro>/<model_id>.nam`. Nomi ripuliti (niente `..`, separatori, caratteri di controllo; lunghezza massima); scrittura in un file temporaneo e rinominato a scarico finito; dimensione massima ragionevole (es. 64 MB) per rifiutare risposte anomale.
- Accanto a ogni file: `<model_id>.t3k.json` con `tone_id`, `model_id`, titolo, autore, `license`, URL della pagina su TONE3000, data di scarico, taglia e architettura. Serve a **non perdere autore e licenza** (obbligo dei termini) anche quando il file viene mostrato dalla scheda LOCAL, dove se il file laterale esiste si aggiunge il badge di licenza.
- Il file è dell'utente, scaricato da lui, su richiesta, con il suo account: non è una copia del catalogo. **Il termine "cache" non è chiarito nei termini**: va confermato al supporto (§8). Non c'è nessuno scarico preventivo né in blocco.
- Un file già scaricato non si riscarica: se la scheda riconosce `model_id` nella cartella lo carica subito (nessuna chiamata per il file, così funziona anche offline una volta scaricato — sempre da confermare).

## 6. Sicurezza e privacy

- Token in un file nella cartella delle preferenze con permessi `0600` (Linux/macOS) o nella cartella dell'utente con permessi del sistema (Windows), scritto in modo atomico; il portachiavi del sistema è un miglioramento successivo. Non si scrivono token nei log né nelle tracce di errore.
- Solo HTTPS verso `tone3000.com`; certificati verificati, nessuna opzione per disattivare il controllo. I reindirizzamenti si seguono solo verso lo stesso dominio.
- Dati dell'utente: solo quelli necessari (id, nome mostrato, token). SIGN OUT li cancella.
- Il server locale ascolta solo su loopback, accetta una richiesta e si chiude; `state` obbligatorio; nessuna pagina serve file.
- Il testo che arriva dall'API (nomi, descrizioni) è **dato**: lunghezza limitata, controllo UTF-8, mai interpretato; nessun HTML.
- Limite di velocità: finestra scorrevole a 60 richieste/min nel client, riprova con attesa crescente su 429 e errori di rete, mai più di 3 tentativi per comando.

## 7. Decisioni e dipendenze

- **HTTP: libcurl (deciso 2026-10-21).** Sottomodulo `vendors/curl` fissato alla release **8.23.0** (clone superficiale, 36 MB; licenza "curl", tipo MIT), compilato come libreria statica con solo HTTP/HTTPS (protocolli, zlib, brotli, zstd, libpsl, libssh, nghttp2, IDN disattivati). **TLS:** Windows con Schannel (nessuna libreria da distribuire); **Linux con OpenSSL**; **macOS con OpenSSL**, perché curl ha **rimosso Secure Transport nella 8.15**: su macOS e Linux OpenSSL è quindi una dipendenza (collegamento statico o libreria in bundle da decidere al momento dell'impacchettamento, §9). Senza OpenSSL (non Windows) il client non viene compilato e la configurazione continua. Il sottomodulo è del progetto "di terzi": non si aggiorna senza dirlo (regole del repository, §5).
- **Host dell'API: `www.tone3000.com`.** `tone3000.com` risponde con un redirect 308 a `www.tone3000.com`; la regola "solo stesso host" è giusta e il client deve usare direttamente l'host con `www`.
- **JSON:** nlohmann/json, già nei sottomoduli.
- **SHA-256 e casualità per PKCE:** funzioni piccole interne (o quelle del TLS scelto); test con i vettori noti (RFC 7636 appendice B).
- **Registrazione dell'app:** serve un'app registrata su TONE3000 con `client_id` e URI di ritorno; la fa **l'utente** con il proprio account (l'agente non crea account). Finché manca, la scheda resta nel modo "TONE3000 not configured" (attenuata) e tutto si prova con i finti.
- **Compatibilità dei modelli:** `architecture_version` A1/A2 e il core NAM fissato (`vendors/NeuralAmpModelerCore`, di settembre 2026, ha i moduli slimmable): **da verificare con un modello A2 vero** che il core lo carichi; il flag del modello nella scheda dipende da questo.

## 8. Da chiedere al supporto (support@tone3000.com)

1. URI di ritorno ammessi per un plugin desktop: `http://127.0.0.1:<porta>` (porta variabile) va bene? In caso contrario, l'alternativa raccomandata.
2. Il file scaricato dall'utente e tenuto in una cartella locale (anche per l'uso offline) è consentito?
3. Per `load_tone` e `select_tone`: la scheda può mostrare anche gli elenchi (preferiti, scaricati, ...) nel piano gratuito, come dice la documentazione?
4. Linee guida del marchio e del logo (dimensioni minime, colori) e dove si trovano i file ufficiali.
5. Frequenza di campionamento dei modelli: ci sono metadati oltre a quelli del file?

La bozza della richiesta si prepara a parte; **non si contatta il supporto da qui**: la invia l'utente.

## 9. Decisioni aperte

- Come distribuire OpenSSL su Linux e macOS (statico o in bundle). **Intanto il client è opt-in** (`-DFAIRY_ENABLE_TONE3000=ON`, default OFF): collegato com'è, il plugin chiede `libssl.so.3` e `libcrypto.so.3` all'apertura e non si carica se mancano (verificato con `ldd`); spento, nessuna dipendenza. Le licenze di curl e OpenSSL sono già nei CREDITS.
- URI di ritorno (loopback o flusso del dispositivo), dalla risposta del supporto (§4, §8).
- Cartella dei toni scaricati: predefinita o scelta dall'utente (§5).
- Posizione grafica della scheda e dell'avanzamento dello scarico: da definire con un mockup in `src/the-stripper/docs/mockups/stripper_model_browser.html` (aggiungere la scheda TONE3000).
- Marchio "Powered by TONE3000" nella schermata CREDITS (già prevista, spec Stripper §4): da rendere secondo le linee guida.

## 10. Prove

- Test automatici senza rete: PKCE con i vettori RFC 7636; costruzione dell'URL di autorizzazione (campi, codifica); parsing delle risposte con file di esempio (tono, modelli, elenchi, errore 401/429/5xx) e con risposte malformate; macchina a stati dell'accesso con `HttpClient` e browser finti (successo, `state` sbagliato, annullamento, scadenza); server locale su porta effimera con una richiesta vera in locale; limitatore di velocità con orologio finto; pulizia dei nomi dei file; scrittura atomica e rinomina; file laterale (scrittura, lettura, campi mancanti); un test che fallisce se un token compare in un messaggio di log.
- Test con mutazioni sulle parti che garantiscono sicurezza (verifica di `state`, pulizia dei percorsi, limite di dimensione).
- **Non verificabile senza l'app registrata e un account:** il flusso vero con TONE3000, il ritorno su loopback, lo scarico di un modello vero, i limiti di velocità reali. Si dichiara in ogni resoconto.

## 11. Piano di massima e stato (2026-10-21)

Tutto in `shared/tone3000/` (nessuna dipendenza da VSTGUI), 5 test eseguibili (`tone3000_*_tests`), compilazione e prova solo su Linux.

1. **Fatto.** PKCE (SHA-256 con i vettori noti, esempio RFC 7636), limitatore di richieste, pulizia dei nomi dei file, scrittura atomica (anche privata, 0600), file laterale `.t3k.json`.
2. **Fatto.** `Tone3000Api` (tono, modelli, elenchi, utente, scarico) e i lettori JSON su risposte di esempio con i campi documentati; ID e testi da fuori trattati come dati.
3. **Fatto.** Flusso OAuth: `OAuthFlow`, ricevitore su `127.0.0.1` (`LoopbackServer`: solo loopback, controllo dell'intestazione Host, `state` verificato in tempo costante, le richieste con `state` sbagliato non chiudono il login), URL, risposta del token, apertura del browser solo sulla pagina di login. Provato su un socket vero con un browser finto, compreso il controllo che il verificatore inviato al token sia quello della sfida nell'URL.
4. **Fatto.** `TokenStore` (login in un file 0600, cancellato dal SIGN OUT) e `Tone3000Client` (thread di lavoro, snapshot a versioni, rinnovo del token prima della scadenza e su 401, scarico in `.part` con controllo "sembra un NAM" e file laterale, un modello già scaricato non si riscarica).
5. **Fatto.** libcurl (sottomodulo 8.23.0) e `http_curl.cpp`.
6. **Fatto, in attesa dell'app registrata.** Scheda TONE3000 nel popup dei modelli (`ui/tone3000_tab`, testi puri in `ui/tone3000_view.h` con test; `Controller::tone3000Actions()` e `Tone3000Service` creano il client alla prima apertura). Stati: non configurato, accesso, in attesa del browser, elenchi (preferiti, scaricati, creati, di tendenza, ultimi, con pagine), tono con i suoi modelli (autore e licenza sempre visibili, avviso per le licenze non commerciali, impulsi non caricabili), scarico con avanzamento, errori. Il client si compila solo dove c'è OpenSSL; senza o senza `kTone3000ClientId` (`src/tone3000_config.h`, vuoto) la scheda dice "TONE3000 is not set up in this build" e non parte nessuna richiesta. Anteprima `stripper_model_browser.html` aggiornata (piano gratuito: niente ricerca). Il LOCAL scansiona anche la cartella dei toni scaricati (`<preferenze>/FairyTapes/tone3000/models`) senza doverla aggiungere. **Manca:** il badge di licenza nell'elenco LOCAL leggendo i file `.t3k.json` (i file ci sono già).
7. **Fatto.** CREDITS (libcurl, OpenSSL, avviso "Powered by TONE3000") e nel retro il pulsante TONE3000 → OPEN... che apre il popup sulla scheda TONE3000. **Resta:** cartella dei toni e SIGN OUT tra le impostazioni (SIGN OUT c'è nella scheda).
8. **A cura dell'utente.** Prova con l'app registrata e verifica del flusso vero.

**Scoperte durante la scrittura (da tenere presenti):**
- I campi dell'API sono quelli della documentazione (`Tone`, `Model`, `User`, pagine `data/page/page_size/total/total_pages`); **gli elenchi `trending` e `latest` non hanno pagine**; il corpo degli errori non è documentato, per questo il client mostra solo testi propri.
- `model_url` richiede il token: il client lo manda **solo se l'host è quello dell'API**; un reindirizzamento verso un altro host https (un deposito di file) si segue **senza** `Authorization` né `Cookie`. Se `model_url` fosse su un altro host, si scarica senza token: da verificare con una prova vera.
- Il piano gratuito nomina solo i flussi OAuth e gli elenchi; gli esempi della documentazione per `select_tone` chiamano anche `GET /tones/{id}` e `GET /models`, che i termini non nominano: **va chiesto al supporto** (domanda 3, §8).
- Con `cancel` si ferma il comando in corso e si scartano quelli in coda; un login annullato non è un errore.
