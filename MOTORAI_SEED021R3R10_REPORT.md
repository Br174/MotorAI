# MotorAI R3R10 — miglioramento di comprensione e strumenti in COPIA ISOLATA

Operation ID: MOTORAI-R3R10-PHASE2-SHADOW-20261011

## Evidenze
R3R9 dispositivo (11/10/2026 10:53:13): 17/20, 85%; errori 2 saluto->azione
(0.2509), 3 ricerca->calcolo (0.2427), 19 calcolatrice->search (0.2812).
CURRENT/PREVIOUS/autotrain-baseline intatti, Mini-AI Goal10 10/10.
Nessun errore runtime nelle prove.

## Implementazione
- Solo copia Engine caricata da CURRENT sotto il mutex native e del checkpoint.
- Nuovi esempi di TRAIN diversificati di Goal1 e Goal7; validazione e TEST originali
  non sono alterati.
- 16 frasi ulteriori non testualmente presenti nel TRAIN e diverse dalle 20 R3R9.
- La scelta del punto di arresto guarda solo la validazione originale,
  non le 16 nuove frasi.
- Valutazione finale una volta di 16 frasi, sei vecchi casi Goal1/Goal7 e
  TEST originale, con restrizioni non regressione.
- Una copia candidata e' salvata SOLTANTO se valida; il controllo JNI
  indipendente Goal10 deve confermare 10/10 e altre nove competenze invariate.
- Nessuna promozione su CURRENT ne' allenamento in g_engine; zero modifiche ai
  punteggi o ai Goal completati.
- Esecuzione manuale una volta da Diagnostica; pausa SOS attiva obbligatoria,
  richiesta conferma esplicita sul telefono.
- Nessuna eliminazione dei checkpoint.
- I tre errori noti NON vengono inseriti letteralmente tra gli esempi di training.
- 16 frasi di test limitate (non un benchmark generale); la confidenza softmax
  non deve essere scambiata per certezza calibrata.

## Piano di collaudo
- Native/CI: rollback e Goal10, training in copia senza chiamate di train/save
  del modello attivo, comparazione SHA256 e stato nativo.
- Android: LAB R3R10 installabile sopra R3R9, stessa firma del ramo laboratorio.
- Non confondere candidate_ready con promozione reale: quest'ultima NON fa parte
  di R3R10 e richiederà nuova autorizzazione.
- Il 10/10 originale, il 100% Mini-AI e la pausa SOS sono invarianti.
- Stato: sviluppo in corso; accettazione su telefono PENDING.
