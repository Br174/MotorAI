# MotorAI Seed021R3R13 — diagnostica per singola domanda, SOLA LETTURA

Operation ID: MOTORAI-PHASE2-R3R13-PER-QUESTION-READONLY-20261011

## Baseline su telefono
Diagnostica R3R12, 11 ottobre 2026 12:13:03:
- Modello principale Mini-AI 10/10; CURRENT/PREVIOUS/autobaseline OK; SOS attivo.
- Copia R3R12 candidate_rejected: 15/20→15/20, miglioramento nullo.
- Goal1 TEST 95→95%, Goal7 TEST 100→100%.
- 24 guardie per classe invariate: chat 4/6, memoria 4/6,
  calcolatrice 5/6, ricerca 6/6.
- 6 prove note 3/6→4/6; nessuna promozione del modello.

## Modifica autorizzata
R3R13 legge sul modello attivo, senza allenare:
- 20 domande precedenti di R3R12 (non chiamarle nuove o "blind" attuali);
- 24 domande di validazione supplementare R3R12;
- 6 domande già note dal collaudo R3R9.

Per ciascuna domanda: categoria Goal1/Goal7, testo, atteso, effettivo,
confidenza numerica, PASS/FAIL. Include riepilogo per sezione, per classe,
confusioni atteso→effettivo e numero dei casi a bassa confidence.
Verifica SHA-256 di CURRENT, PREVIOUS e backup locali, stato nativo e SOS
prima/dopo. Nessuna scrittura, training, cambio Goal, promozione o rete.
Il pulsante R3R12 di allenamento precedente è disabilitato.
Il nuovo testo viene copiato con l'intera diagnosi esistente.

**Limite:** dato che la candidata R3R12 fu respinta senza salvataggio,
NON è possibile leggere retroattivamente le singole risposte "dopo" quel
vecchio allenamento. Non si simula alcun vecchio training.
Questi esempi sono diagnostici già esposti; NON devono diventare il prossimo
TEST indipendente e NON devono essere usati per ottimizzare nuovi pesi.
Il prossimo gruppo holdout va preparato separatamente in una LAB futura
solo quando sarà autorizzato un esperimento di training.

## Versione e consegna
Update-in-place package it.motorai.seed, versionCode 39,
versionName 0.21.16-seed021r3r13, firma LAB precedente invariata.
Grafica principale intatta, solo sezione diagnostica aggiunta.
APK nel livello principale dello ZIP semplificato; ZIP completo con sorgenti.

## Collaudo richiesto sul dispositivo
Installare R3R13 sopra R3R12 senza disinstallare, SOS attivo, apertura
Diagnostica → R3R13 · Analizza le 50 risposte (senza allenare).
Copiare la diagnostica, inviarla alla chat. Nessun nuovo allenamento.
