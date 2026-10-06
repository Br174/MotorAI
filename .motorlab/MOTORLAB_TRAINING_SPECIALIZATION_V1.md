# MotorLab-Training V1

MotorLab-Training è il tutore/orchestratore di MotorAI, non il cervello.

Responsabilità: Training Factory, curriculum, provenance/licenze dataset, split train/validation/test, checkpoint, benchmark, regressioni, resume, confronto versioni, selezione esercizi successivi, controllo self-training e crescita del modello.

## Pollicino Training Record

Conservare solo stato tecnico utile: checkpoint, versione pesi, dataset/hash, step, loss, metriche, errori, stato training, ultimo progresso valido. Non conservare chain-of-thought privata.

## Sicurezza

Mai sovrascrivere l'unico checkpoint buono. Ogni sessione mantiene precedente valido + corrente + config + metriche/hash. Self-training futuro richiede verifica deterministica o campione indipendente prima che un output generato da MotorAI diventi verità di training.
