# MotorAI Seed021R3R11 — copie Goal 1 e Goal 7 indipendenti

Operation ID: MOTORAI-R3R11-INDEPENDENT-BEST-20261011
CORE: MotorLab r34, motore MotorAI-Brain preservato.

## Punto di ripresa
R3R10 su Android (11 ottobre 2026 alle 11:16:58): candidata non qualificata.
Goal1 val 97,5→97,5%; TEST 95→95%.
Goal7 val 93,8→87,5%; TEST 100→87,5%.
Vecchie 6 prove Goal1/7: 3→5; blind16: 7→8.
R3R10 ha interrotto l'allenamento a 200 passi per ciascuna copia,
senza conservare la migliore fase precedente indipendente.
Il modello principale resta 10/10; CURRENT, PREVIOUS e SOS rimangono protetti.

## Correzione
- Un unico coordinatore MotorAIPhase2ShadowTrial con ingresso R3R11;
  il precedente ingresso R3R10 è deprecato nella Diagnostica.
- Engine temporanei indipendenti per Goal1 e Goal7: ognuno viene ricaricato
  da CURRENT per provare tassi 0,003/0,007/0,012 e checkpoint intermedi
  10/20/40/80/160 passi, con limite temporale.
- La scelta si basa *solo* su validazione originale (accuratezza e perdita);
  le fasi peggiori vengono scartate e il migliore resta nella copia.
- Solo i pesi del ramo approvato dal selettore sono trasferiti al modello
  combinato temporaneo. CURRENT e il motore nativo g_engine non sono modificati.
- Ripetizione dei 6 vecchi casi R3R9 (prova di regressione nota).
- 20 nuovi casi distinti, con controllo che non si trovino testualmente
  nei dati nativi o nei nuovi esempi di training; controllo dopo selezione,
  non usati per decidere tasso, checkpoint o fermata.
- Test Goal1 e Goal7 originali valutati una sola volta sul migliore
  combinato, insieme ai test di non regressione, Goal10 10/10,
  altre competenze invariate, SOS e fingerprint dei checkpoint.
- Una candidata è conservata separatamente solo se supera tutti
  i criteri applicabili. Nessuna promozione automatica o allenamento live.
- Un test positivo è una misura limitata, non prova di intelligenza generale.
- Versione Android code 37, nome 0.21.14-seed021r3r11, stessa identità
  e chiave LAB. APK direttamente nella radice dello ZIP.

## Accettazione
La pipeline GitHub verifica sintassi, test regressione precedenti,
sicurezza degli snapshot indipendenti, compilation APK e archivio.
Il collaudo effettivo dei pesi già addestrati deve avvenire sul telefono:
mantenere SOS, installare sopra R3R10 senza disinstallare,
aprire Diagnostica → R3R11 copie indipendenti, inviare rapporto.
Nessuna promozione senza autorizzazione e ulteriori verifiche.
