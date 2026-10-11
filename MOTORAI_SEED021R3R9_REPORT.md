# MotorAI Seed021R3R9 — Fase 2, sondaggio di generalizzazione
Data di preparazione: 11 ottobre 2026
Operation ID: MOTORAI-PHASE2-R3R9-20-NOVEL-PROMPTS-20261011

## Punto di partenza immutabile

R3R8 sul dispositivo ha superato il Goal10 attivo 10/10, e la schermata
"Percorso Mini-AI" ha mostrato tutti i 10/10 obiettivi al 100% alle 10:29.
Fondamenta L0-L5 complete, CURRENT/PREVIOUS e SOS esterno conservati.
Non riscrivere i checkpoint sul telefono; nessuna nuova promozione.

## Modifica R3R9

- Solo una sezione aggiuntiva in Diagnostica.
- Venti richieste alternative distribuite:
  4 comprensione di intenti Goal1,
  4 classificazioni di richieste di memoria Goal3 (NON recupero di contenuti reali),
  4 ragionamenti e calcoli Goal4 con risultati numerici attesi,
  4 giudizi sul bisogno di verifica Goal5,
  2 selezioni fonti Goal6 e 2 selezioni strumenti Goal7.
- Le stringhe sono escluse per confronto esatto dal codice sorgente del corpus
  e dal vecchio ispettore Goal10; ciò NON prova una generale assenza di
  parafrasi, pattern comuni o concetti affini dai dati di addestramento.
- Risposta effettiva JSON, atteso predeterminato, PASS/FAIL per ogni prova.
- Punteggio informativo su 20, distinto dal percorso Mini-AI 100%.
- Pausa SOS e dieci Goal completati obbligatori, controllo SHA-256 di
  CURRENT, PREVIOUS, auto-baseline e backup speciali se presenti, confronto
  dello stato nativo prima/dopo.
- Nessuna chiamata a train, save, restore, reset, internet, aggiornamento
  delle preferenze o modifica dei pesi.
- In caso di problemi, mostrare i fallimenti senza auto-training/rollback.
- R3R9 è un aggiornamento in-place versioneCode 35, stesso applicationId
  it.motorai.seed e stessa chiave LAB stabile.
- ZIP nuovo: APK direttamente nella radice del pacchetto e sorgenti inclusi,
  evitando ZIP annidati poco leggibili.

## Collaudo dispositivo (PENDING)

Mantenere il backup SOS esterno e la pausa SOS. Installare R3R9 sopra R3R8
senza disinstallare. Aprire ⋮ → Diagnostica → Esegui 20 prove nuove.
Copiare la diagnosi e inviarla; il risultato può essere ben inferiore al 100%.
La Mini-AI resta 100% indipendentemente dal punteggio, a meno che non si
osservino regressioni o perdite reali (non previste dalla funzione).
