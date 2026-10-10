# Fairy Tapes — regole di progetto per chat e agent

Suite di plugin VST3 (C++20, VST3 SDK, VSTGUI4). Più chat e più agent lavorano sullo stesso repo: queste regole servono a non pestarsi i piedi. Leggile prima di toccare codice, branch o documenti.

## 1. Branch e flusso

- **`master`** è la linea rilasciata. Non si lavora e non si pusha mai direttamente su `master`: ci arriva solo un merge di `pre-release`.
- **`pre-release`** è il raccoglitore e il branch di lavoro: di norma si lavora direttamente lì. I branch `feat/...` **non sono obbligatori**: se ne crea uno solo quando serve (lavoro lungo o rischioso, esperimento da poter scartare, più chat sullo stesso plugin nello stesso momento). Quando esiste un branch, si sgancia da `pre-release` (non da `master`), si mergia dentro `pre-release` e le sue PR hanno come destinazione `pre-release`.
- La CI (`.github/workflows/build.yml`) parte solo su push e PR verso `master`: così la build gira una volta sola, al merge `pre-release` → `master`, e non a ogni merge di un plugin. Non aggiungere `pre-release` ai trigger senza chiedere.
- Nome dei branch, quando servono: `feat/<plugin>` (es. `feat/the-stripper`), `fix/<argomento>`, `docs/<argomento>`.
- Se si usa un branch, prima di mergiarlo in `pre-release` si riportano nel branch le novità di `pre-release` (merge di `pre-release` nel branch) e si risolvono lì i conflitti.

## 2. Più chat insieme: un worktree ciascuna

Più chat sullo stesso checkout si rovinano a vicenda (cambi di branch sotto i piedi, file modificati da altri). Se lavora una chat sola, si lavora su `pre-release` nel checkout principale. Se ne lavorano più insieme:

- Ogni chat lavora nel **proprio worktree**, su un proprio branch (qui il branch serve, perché due worktree non possono avere lo stesso):
  `git worktree add ../fairy-tapes-wt-<nome> -b feat/<nome> pre-release`
  poi, nel nuovo worktree: `git submodule update --init --recursive`.
- Se un checkout è condiviso, **non cambiare branch** e non fare `git checkout`, `reset --hard`, `stash` o `clean` su file che non hai creato tu.
- Prima di ogni commit: `git branch --show-current` e `git status`. Metti in stage **solo i file tuoi, per nome** (mai `git add -A` o `git add .`). Un commit finito sul branch sbagliato si sposta con cherry-pick, non lasciandolo lì.
- Dopo un'operazione lunga (build, ricerca) ricontrolla il branch: un'altra chat potrebbe averlo cambiato.

## 3. Commit e push

- Messaggi di commit in inglese, nello stile `tipo(plugin): descrizione` (`feat`, `fix`, `docs`, `build`, `refactor`, `test`). Chiudi con la riga di co-autore richiesta dall'ambiente.
- Push solo quando l'utente lo chiede ("committa e pusha"): del proprio branch oppure di `pre-release`. Mai `--force` sui branch condivisi.
- Non dichiarare un lavoro finito senza averlo verificato: build e test eseguiti, oppure dichiarare con precisione cosa non è stato verificato.

## 4. Build e risorse

- Il disco `/srv/http` è piccolo: **costruisci in una cartella scratch fuori dal repo** (es. `/tmp/.../build`), non dentro la copia di lavoro.
- Configura con `-DCMAKE_BUILD_TYPE=Release -DGPU_NAM_DISABLE_CUDA=ON`, usa `-j2` e compila **solo i target che servono** (`the_stripper`, `the_stripper_tests`, …): una build di tutto è stata uccisa per memoria. CUDA si verifica a parte, su una macchina con driver NVIDIA.
- I test devono restare verdi prima e dopo ogni modifica. Il nuovo codice nasce con un test che prima fallisce.

## 5. Submodule

- I submodule in `vendors/` sono di terzi e fissati a un commit dal repo: `vst3sdk`, `NeuralAmpModelerCore`, `airwindows`, `dusk-audio-plugins` (più i loro annidati: non sono nostri). Il codice nostro sta in `src/` e `shared/`.
- **Non aggiornare, sostituire o cambiare i commit dei submodule senza dirlo all'utente.** Un `git pull` dentro un submodule rompe la build di tutti (è già successo con NAM). Se serve una nuova versione: branch dedicato, adeguare le liste dei sorgenti dei CMake, test, commit del nuovo puntatore.
- `git status` mostra ` M vendors/...` o `+` quando il checkout differisce dal commit fissato: non è una modifica tua, non committarla per sbaglio.

## 6. Documenti sempre allineati

- Ogni plugin ha una spec in `docs/superpowers/specs/` e un piano in `docs/superpowers/plans/`. **Ogni decisione presa in chat entra nella spec e nel piano nello stesso commit** (o subito dopo): lo stato dei compiti (`[ ]` / `[x]`) si aggiorna quando un compito finisce.
- Le decisioni aperte stanno nella sezione "Decisioni aperte" della spec, non solo nella chat.
- Documentazione e spec in italiano; identificatori, commenti nel codice e messaggi di commit in inglese.

## 7. Grafica comune

Le regole visive condivise sono in **`docs/DESIGN_SYSTEM.md`** (pannello rack, livelli, controlli, usura, nastro, viti, retro con Settings e Credits, lingua). Materiale dei controlli e texture del pannello sono **uguali per tutti i plugin** (quelli di The Stripper, vedi lì). Le preview HTML di riferimento stanno in `src/<plugin>/docs/mockups/`; la preview di The Stripper (`stripper_rack.html`) è il riferimento del linguaggio comune. Tutto il testo visibile nell'editor è **in inglese**.

## 8. Puntatori utili

- Stato e decisioni di The Stripper: `docs/superpowers/specs/2026-10-09-the-stripper-completion-design.md` e `.../plans/2026-10-09-the-stripper-completion.md`.
- Pillow Talk: `docs/superpowers/specs/2026-10-08-pillow-talk-design.md`; Doggystyle: `2026-10-09-doggystyle-design.md`.
- Componenti condivisi: `shared/gpu_nam`, `shared/nam_drive`.
