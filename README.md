# MotorAI

MotorAI è un progetto di intelligenza artificiale **from scratch**: tokenizer, Transformer, pesi iniziali casuali, training, checkpoint e benchmark sono costruiti dal progetto senza usare modelli linguistici preaddestrati come cervello iniziale.

## Stato corrente

- Release di lavoro: **MotorAI Seed 006**
- Motore di sviluppo: **MotorLab r23**
- Tutore dell'apprendimento: **MotorLab-Training**
- Piattaforma iniziale: **Android**
- Primo benchmark controllato: apprendimento/generalizzazione verificati da pesi casuali
- Build Android: GitHub Actions, zero-config per APK debug
- Cloudflare: non richiesto nella fase iniziale

Vedi `MOTORAI_SPEC_V1.md`, `MOTORAI_ARCHITECTURE_V1.md` e `MOTORAI_SEED_003_REPORT.md` per i dettagli.

> Nota: il repository contiene il codice e benchmark tecnici. Pesi personali futuri, dataset privati, credenziali e checkpoint sensibili non devono essere pubblicati.


## Firma LAB

Le build Seed/LAB usano una chiave di firma **pubblica e deliberatamente non-production** per mantenere aggiornamenti installabili tra build di laboratorio senza configurare GitHub Secrets. Non deve mai essere usata per una futura app di produzione o distribuzione fidata; la produzione richiederà una chiave privata separata.


## Curriculum Level 1

Seed 006 keeps the accepted Seed 005 neural baseline and introduces an adaptive second curriculum stage. The accepted Level-1 task is reversal of unseen two-symbol sequences with controlled replay of Level 0. The pre-APK gate requires at least 90% new-task accuracy and 90% Level-0 retention; the accepted deterministic host gate reached 100% / 100%. Real-device acceptance remains required.
