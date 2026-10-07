package it.motorai.seed;

import android.content.Context;
import android.content.SharedPreferences;

import java.util.Locale;

/**
 * Roadmap separata verso una piccola AI assistente.
 *
 * Le fondamenta neurali L0-L5 NON fanno parte di questo punteggio: sono prerequisiti.
 * I 10 macro-obiettivi partono da zero e avanzano solo quando benchmark dedicati
 * forniscono evidenza misurabile. Ogni obiettivo pesa il 10% del totale.
 */
public final class MiniAiGoals {
    private static final String PREFS = "motorai_mini_ai_goals";
    private static final int VERSION = 1;

    public static final String[] TITLES = new String[] {
            "Capire una richiesta normale",
            "Rispondere con frasi sensate",
            "Ricordare il contesto della conversazione",
            "Ragionare in più passaggi",
            "Riconoscere quando non sa qualcosa",
            "Cercare informazioni",
            "Usare strumenti",
            "Fare un piano e portarlo avanti",
            "Controllare e correggere il proprio risultato",
            "Completare il ciclo da mini-assistente"
    };

    public static final String[] DESCRIPTIONS = new String[] {
            "Comprendere semplici richieste in italiano e capire cosa viene chiesto.",
            "Produrre risposte brevi, pertinenti e non preprogrammate.",
            "Tenere conto dei messaggi precedenti senza ripartire da zero.",
            "Scomporre un problema semplice in passaggi e arrivare a una conclusione.",
            "Distinguere ciò che sa da ciò che deve verificare, evitando di inventare.",
            "Formulare una ricerca, leggere una fonte controllata ed estrarre l'informazione utile.",
            "Scegliere e usare funzioni come calcolo, ricerca o lettura di dati quando servono.",
            "Decidere una sequenza di azioni, eseguirla e controllarne l'esito.",
            "Verificare la propria risposta, individuare errori evidenti e correggerli.",
            "Capire, ragionare, ricordare, cercare o usare strumenti quando serve e dare una risposta finale utile."
    };

    private MiniAiGoals() {}

    private static SharedPreferences prefs(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    public static void seedIfNeeded(Context context) {
        SharedPreferences p = prefs(context);
        if (p.getInt("roadmap_version", 0) >= VERSION) return;
        SharedPreferences.Editor e = p.edit().putInt("roadmap_version", VERSION);
        for (int i = 0; i < TITLES.length; i++) {
            if (!p.contains(keyPercent(i))) e.putInt(keyPercent(i), 0);
            if (!p.contains(keyEvidence(i))) {
                e.putString(keyEvidence(i), "Non ancora misurato con il benchmark dedicato.");
            }
        }
        e.apply();
    }

    public static int percent(Context context, int index) {
        seedIfNeeded(context);
        return clamp(prefs(context).getInt(keyPercent(index), 0));
    }

    public static String evidence(Context context, int index) {
        seedIfNeeded(context);
        return prefs(context).getString(keyEvidence(index),
                "Non ancora misurato con il benchmark dedicato.");
    }

    public static void updateGoal(Context context, int index, int percent, String evidence) {
        if (index < 0 || index >= TITLES.length) return;
        seedIfNeeded(context);
        prefs(context).edit()
                .putInt(keyPercent(index), clamp(percent))
                .putString(keyEvidence(index), evidence == null || evidence.trim().isEmpty()
                        ? "Aggiornato da benchmark dedicato." : evidence.trim())
                .putLong(keyUpdated(index), System.currentTimeMillis())
                .apply();
    }

    public static double totalPercent(Context context) {
        seedIfNeeded(context);
        double sum = 0.0;
        for (int i = 0; i < TITLES.length; i++) sum += percent(context, i);
        return sum / TITLES.length;
    }

    public static int completedCount(Context context) {
        seedIfNeeded(context);
        int n = 0;
        for (int i = 0; i < TITLES.length; i++) if (percent(context, i) >= 100) n++;
        return n;
    }

    public static int activeGoalIndex(Context context) {
        seedIfNeeded(context);
        for (int i = 0; i < TITLES.length; i++) {
            if (percent(context, i) < 100) return i;
        }
        return TITLES.length - 1;
    }

    public static String status(Context context, int index) {
        int p = percent(context, index);
        if (p >= 100) return "completato";
        if (p > 0) return "in corso";
        return "non iniziato";
    }

    public static String summary(Context context) {
        return String.format(Locale.ITALY,
                "Percorso Mini-AI: %.1f%% · %d/10 obiettivi completati",
                totalPercent(context), completedCount(context));
    }

    private static int clamp(int value) {
        return Math.max(0, Math.min(100, value));
    }

    private static String keyPercent(int index) { return "goal_" + index + "_percent"; }
    private static String keyEvidence(int index) { return "goal_" + index + "_evidence"; }
    private static String keyUpdated(int index) { return "goal_" + index + "_updated_ms"; }
}
