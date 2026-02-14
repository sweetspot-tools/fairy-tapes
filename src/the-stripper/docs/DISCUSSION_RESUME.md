# Resume della discussione (Neural Plugin Assistant)

Riassunto della discussione sulla suite **Fairy Tapes** e su **The Stripper**, ricavato dai documenti già creati e dalla chat “Neural Plugin Assistant”. Serve per ripartire senza perdere il filo.

---

## 1. Contesto

- **Suite:** Fairy Tapes — plugin VST3 con suono “vero”, in stile analogico, grazie a NAM (Neural Amp Modeler) e, dove serve, GPU.
- **Primo plugin:** The Stripper — channel strip **orientato al bus** (mix bus, subgroup, master), non solo singola traccia.
- **Ispirazioni (solo concetto, niente copia di codice/brand):**
  - **UI / idea:** stile channel strip e “carica NAM da dentro il plugin” (es. Chiarello/PreBrowser); fonti poche (video YouTube), citate in `.cursorrules`.
  - **Algoritmi (non-NAM):** Airwindows (MIT) — repo https://github.com/airwindows/airwindows/tree/master/plugins — usato per tecniche/algoritmi; nessun uso di nomi o branding Airwindows.

---

## 2. Criteri per la scelta degli algoritmi

**Non** usiamo solo Airwindows. Criteri: licenza compatibile (MIT/AGPL ok), uso **bus-oriented**, codice integrabile in VST3 (C++, auto-contenuto), niente nomi/brand della fonte in UI. Fonti ammesse: **Airwindows** (MIT), **NAM**, **in-house** (o altro open-source). Compressione: **ButterComp** (Airwindows, MIT) ✓. **ReaPlugs (Cockos) escluso** — licenza proprietaria. Dettaglio: [FEATURES.md §3.0](FEATURES.md#30-criteria-for-algorithm-choice-and-sources-summary).

## 3. Decisioni già prese (e dove sono scritte)

Tutto è documentato in [FEATURES.md](FEATURES.md) e sintetizzato in [OVERVIEW.md](OVERVIEW.md).

| Stage | Scelta | Fonte |
|-------|--------|--------|
| **LP/HP** | Biquad (Airwindows); **solo HP Freq + LP Freq stepped**, Q/slope fissi; bypass | §1.2b, §3.2, §3.3, UI_PARAMS |
| **EQ** | **Channel** = **4K-EQ** (4 parametrici + HP/LP). **Mix bus** = **Pultec (EQP-WDF-1A)**. §1.2c.1. | §1.1, §1.2b, §1.2c, §1.2c.1, §3.2, §3.3, UI_PARAMS |
| **Compression** | **Channel** = **ButterComp**; **Mix bus** = **Logical4** (SSL-style 2-buss). Entrambi Airwindows, MIT. §3.3.1, §3.4. | §3.2, §3.3, §3.3.1, §3.4 |
| **Gain/Drive** | **Core:** TapeHack2 (classic, CPU) **oppure** NAM (Tone3000 / file locale) **su GPU** (con fallback CPU) | §2, §3.2, §3.3, §6 |
| **Limiter** | **BlockParty** (Airwindows) ✓ scelto — impronta analogica, “real” | §3.2, §3.3 |
| **Gate/Expander** | Denoise (Airwindows) | §3.2, §3.3 |
| **Gain staging** | **Solo modalità Channel**; in-house (RMS + trim), bypassabile. In Mix bus non presente. §1.2. | §1.2 |

- **Airwindows:** non esiste un unico “channel strip”; si è scelto **un plugin per stage** (tabella sopra).
- **Ordine catena:** Gain staging (un estremo) → LP/HP → EQ → Compression → Drive → Limiter → Gate. Possiamo prendere **solo parti** degli algoritmi e **semplificare** (pochi parametri, “rock n roll”, easy to use) — FEATURES §1.2a.
- **Compressione:** **Channel** = **ButterComp**, **Mix bus** = **Logical4** (Airwindows, MIT) ✓ scelti. Licenza progetto rivalutabile (es. MIT) — [FEATURES.md §3.4](FEATURES.md#34-project-license-no-longer-agpl).

---

## 4. Cose ancora da decidere / da fare

- **Limiter:** **BlockParty** (Airwindows) scelto per carattere analogico e allineamento con “suonare il più vero possibile”.
- **Gain staging:** incluso **solo in modalità Channel**; in-house (RMS + trim). In Mix bus non presente. §1.2.
- **Frequenze a scatti:** Sì per **HP/LP** (HP Freq, LP Freq stepped; es. Neve/API/SSL style) e per **bande EQ** (centro freq stepped per ogni banda). Mixer di riferimento: Neve 1073/1084, API 550A/550B, SSL E/G — FEATURES §1.2b, UI_PARAMS.
- **EQ e compressione per modalità:** **Channel** = 4K-EQ (LP/HP e EQ) + ButterComp + gain staging. **Mix bus** = Pultec + Logical4; nessun gain staging. FEATURES §1.2c, §1.2c.1, §3.3.1, UI_PARAMS §3.
- **SSL 360 Link:** requisiti ufficiali da cercare e documentare; intanto: VST3 rigoroso, parametri stabili, nomi chiari (FEATURES.md §4).
- **Implementazione:** skeleton VST3 (pass-through) c’è; da fare: parametri/stato, NAM, Tone3000 in UI, UI “analog” + varianti texture per istanza (vedi [UI_ASSETS_SPEC.md](UI_ASSETS_SPEC.md)).

---

- **GPU:** Solo lo stadio **Drive (NAM)** è eseguito su GPU (modificato per gpuaudio-sdk); tutti gli altri stadi restano su CPU. Pipeline mista CPU/GPU: gestita con trasferimenti a blocchi, latenza totale segnalata al host, fallback CPU se GPU assente. Dettaglio: [FEATURES.md §6](FEATURES.md#6-gpu-execution-and-mixed-cpugpu-pipeline).

## 5. File utili nel repo

| Argomento | File |
|-----------|------|
| Requisiti, NAM, algoritmi, SSL 360 | [FEATURES.md](FEATURES.md) |
| Riepilogo stage e decisioni | [OVERVIEW.md](OVERVIEW.md) |
| UI, dimensioni, asset | [UI_ASSETS_SPEC.md](UI_ASSETS_SPEC.md) |
| Licenza | [LICENSE](../../../LICENSE) |
| Suite e build | [README.md](../../../README.md) (root) |
| Build e stato del plugin | [README.md](../README.md) (the-stripper) |
| Convenzioni progetto | [.cursorrules](../../../.cursorrules) (root) |

---

## 6. Da dove ripartire

- **Limiter** è chiuso: BlockParty (FEATURES.md §3.2/§3.3, OVERVIEW.md).
- Se vuoi **andare avanti con l’implementazione:** prossimi passi logici sono parametri/stato VST3, poi NAM, poi UI.
- Se vuoi **cercare requisiti SSL 360 Link:** si può fare ricerca e aggiungere una sottosezione in FEATURES.md §4.

Questo file può essere aggiornato quando si prendono altre decisioni o si completa un blocco di lavoro.
