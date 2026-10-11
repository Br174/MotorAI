# MotorAI Seed021R3R12 — Goal7 bilanciato e guardia per categoria

Operation ID: MOTORAI-R3R12-GOAL7-GUARDED-20261011
Sessione centrale MotorLab r34; motore originale MotorAI-Brain mantenuto.

## Stato prima del lavoro

Diagnostica reale Android R3R11 11/10/2026 ore 11:46:41:
- Fondamenta L5; Goal 10 del CURRENT 10/10, percorso Mini-AI 100%.
- R3R11 20 prove nuove 15/20 → 17/20, vecchie sei 3/6 → 4/6.
- Goal1 validation 97,5% invariata; TEST 95% invariato.
- Goal7 validation 93,8% invariata; TEST 100% → 87,5% (regressione).
- Copia R3R11 respinta, non promossa; CURRENT/PREVIOUS/autobaseline OK, pausa SOS attiva.

## Correzione R3R12
- Mantiene la separazione dei percorsi Goal1 e Goal7 e gli snapshot migliori.
- Goal7 usa esempi bilanciati di ripasso **solo per TRAIN nella copia**.
- 24 esempi indipendenti di validazione supplementare (sei chat, sei memoria,
  sei calcolatrice e sei ricerca) monitorati separatamente: la candidata
  Goal7 non viene selezionata se peggiora in una categoria.
- 20 ulteriori esempi blind, mai usati nel loop di allenamento; controllo
  disgiunzione testuale rispetto al training nativo e delle nuove varianti.
- Goal7 può rimanere sui pesi originali se nessuna variazione è qualificata;
  Goal1 può comunque migliorare indipendentemente.
- TEST originale Goal1/Goal7 misurato dopo la selezione una volta, SOLO
  per accettazione finale e non per ottimizzare checkpoint.
- Non qualifica una copia se peggiora il TEST, anche quando validazione e
  nuove domande migliorano.
- Verifica Goal10 10/10 e competenze residue non alterate prima di dichiarare
  qualificata la copia; candidata sempre separata dal modello attivo.
- Pausa SOS, checkpoint CURRENT/PREVIOUS/auto-baseline e backup esterno
  non modificati dall'esperimento.
- Nessuna promozione automatica né allenamento o cambio punteggi del motore live.
- Android aggiornabile versionCode 38, versione 0.21.15-seed021r3r12,
  stesso applicationId e stessa firma stabile LAB.
- ZIP senza APK nascosto: copia dell'APK nella cartella principale.

## Accettazione sul dispositivo

Installa sopra R3R11 senza disinstallare, conserva la pausa SOS e il backup
esterno. Apri Diagnostica, esegui il pulsante R3R12 una sola volta, incolla
tutta la diagnostica qui. La risposta può essere nuovamente NON QUALIFICATA:
è corretto fermarsi e preservare l'originale. Il punteggio alto su un gruppo
limitato non dimostra intelligenza generale.

## Rischi e limiti

La validazione supplementare è un test di sviluppo, non un nuovo TEST
indipendente. Le 20 richieste finali non misurano tutta la generalizzazione
linguistica. Dati di training e benchmark sono sintetici.
