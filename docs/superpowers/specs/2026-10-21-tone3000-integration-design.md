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

- **HTTP:** la proposta è **libcurl** (come il plugin ufficiale di TONE3000, licenza tipo MIT/X) come sottomodulo di `vendors/` fissato a un commit, compilato statico con TLS del sistema (Schannel su Windows, Secure Transport su macOS, OpenSSL/mbedTLS su Linux) — **da decidere** (§9): in alternativa lo scarico con le API native di ogni sistema (WinHTTP, NSURLSession, ...) senza nuove dipendenze, con più codice per sistema. Aggiungere un sottomodulo va detto all'utente e va in un commit dedicato.
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

- Libcurl contro API native per l'HTTP (§7).
- URI di ritorno (loopback o flusso del dispositivo), dalla risposta del supporto (§4, §8).
- Cartella dei toni scaricati: predefinita o scelta dall'utente (§5).
- Posizione grafica della scheda e dell'avanzamento dello scarico: da definire con un mockup in `src/the-stripper/docs/mockups/stripper_model_browser.html` (aggiungere la scheda TONE3000).
- Marchio "Powered by TONE3000" nella schermata CREDITS (già prevista, spec Stripper §4): da rendere secondo le linee guida.

## 10. Prove

- Test automatici senza rete: PKCE con i vettori RFC 7636; costruzione dell'URL di autorizzazione (campi, codifica); parsing delle risposte con file di esempio (tono, modelli, elenchi, errore 401/429/5xx) e con risposte malformate; macchina a stati dell'accesso con `HttpClient` e browser finti (successo, `state` sbagliato, annullamento, scadenza); server locale su porta effimera con una richiesta vera in locale; limitatore di velocità con orologio finto; pulizia dei nomi dei file; scrittura atomica e rinomina; file laterale (scrittura, lettura, campi mancanti); un test che fallisce se un token compare in un messaggio di log.
- Test con mutazioni sulle parti che garantiscono sicurezza (verifica di `state`, pulizia dei percorsi, limite di dimensione).
- **Non verificabile senza l'app registrata e un account:** il flusso vero con TONE3000, il ritorno su loopback, lo scarico di un modello vero, i limiti di velocità reali. Si dichiara in ogni resoconto.

## 11. Piano di massima (da trasformare in piano in `docs/superpowers/plans/`)

1. PKCE, limitatore, pulizia dei nomi, file laterale (puro, con test).
2. `HttpClient` + finto, `Tone3000Api` con i parsing (test su file di esempio).
3. Macchina a stati OAuth + server locale + apertura del browser (finti).
4. `TokenStore`, `Worker`.
5. Sottomodulo libcurl e `http_curl.cpp` (decisione §9; da dire all'utente).
6. Scheda TONE3000 nel popup e mockup aggiornato, con badge di licenza e "Powered by TONE3000".
7. Credits e impostazioni (cartella, SIGN OUT) nel retro; documentazione.
8. Prova con l'app registrata (a cura dell'utente) e verifica del flusso vero.
