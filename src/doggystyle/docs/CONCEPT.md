# Doggystyle — Concept & notes *(working title)*

**Status:** Idea / parentesi. Ci ritorneremo più avanti. Nome non definitivo.

---

## 1. What it is

- **Second plugin** in the Fairy Tapes suite.
- **Dual-stage compression** with two **different** compression types.
- **Use case:** Drum groups, bass+guitar groups — rock, metal, grindcore. Plugin da mettere su gruppi per **“prendere a schiaffi l’ascoltatore”** (punch, aggression, in-your-face).
- Names stay in the same **witty tone** as Fairy Tapes, The Stripper, etc.

---

## 2. Serial vs parallel (to decide)

- **Parallel:** Two compressors each get the same input; their outputs are **blended** (e.g. dry + crushed, or comp A + comp B). Classic for drums and “slap”: you get punch and body without killing the transient completely.
- **Serial:** One compressor feeds the other. Useful for “first comp = punch, second = glue” or different flavours in sequence.

**Option A — Only parallel:** Simpler. One workflow: blend two different comp characters. Very effective for the stated use.

**Option B — Switchable serial/parallel:** User can choose “serial” or “parallel”. More flexible, more UI and logic. Serial can make sense (e.g. FET first for punch, then opto for glue).

**Suggestion:** Start with **parallel only** for simplicity and “one job, one workflow”. If we later add a serial mode, we can; document the switch in the same doc.

*(Da decidere insieme quando si torna sul plugin.)*

---

## 3. Two compressor types for this job

For **drums / heavy rock–metal–grindcore** and “hit the listener hard”:

| Type | Why it fits |
|------|---------------------|
| **FET-style** (e.g. 1176-like) | Attack velocissimo, “grabby”, punch su kick/snare, aggressivo. Ideale per il “schiaffo” e i transienti. |
| **Opto** (e.g. LA-2A-like) | Attack più lento, release morbida, “glue”, corpo. In parallelo con il FET: il FET dà il punch, l’opto dà il body e il mix che si tiene insieme. |

**Alternative pairing:** FET + **VCA** (più neutro, controllo preciso) se vogliamo meno “colore” sul secondo stadio. Per un plugin “a schiaffi” **FET + opto** è un classico (punch + glue).

**Cose da fare (più avanti):** Scegliere algoritmi concreti (Airwindows? altri?) per “FET-style” e “opto-style” e documentarli qui.

---

## 4. NAM on GPU at the end?

- **Idea:** Uno stadio finale **NAM su GPU** (come in The Stripper per il Drive) dopo la compressione parallela.
- **Pro:** Colore aggiuntivo, saturation/amp-style sul segnale già compresso — utile in metal/grindcore dove spesso si cerca ancora più grit.
- **Contro:** Il plugin diventa “compression + NAM”; più complesso, più dipendenze. The Stripper è già “channel strip con NAM”; Doggystyle potrebbe restare **solo** doppia compressione parallela per chiarezza.

**Suggestion:** **Sì, possiamo metterlo.** Come stadio **opzionale** (bypassabile): “Dual parallel comp + optional NAM at the end”. Così chi vuole “comp + colore” lo ha; chi vuole solo i due comp può lasciare NAM in bypass. Coerente con la suite “suono vero, analogico” e con l’uso su batteria/bass-guitar dove un po’ di saturation in coda può funzionare bene.

*(Da confermare quando si torna sul plugin.)*

---

## 5. Nome “Doggystyle”

- **Ispirazione:** La posizione sessuale — rock n roll, verace, dominazione. Il nome fa sorridere, in linea con il tono della suite (Fairy Tapes, The Stripper): un po’ irriverente, schietto, “take no prisoners”.
- Non c’entra con Dr. Dre o altri riferimenti musicali; è volutamente quel doppio senso, quel sorriso.

---

## 6. Checklist (per quando ci si torna)

- [ ] Decidere: solo parallelo o anche seriale (switch).
- [ ] Scegliere due algoritmi concreti (FET-style, opto-style) e documentarli.
- [ ] Decidere: NAM su GPU in coda sì/no (proposta: sì, opzionale).
- [ ] Nome definitivo (Doggystyle o variante).
- [ ] Parametri da esporre per i due comp + blend + eventuale NAM.
