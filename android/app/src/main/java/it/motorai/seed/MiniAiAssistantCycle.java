package it.motorai.seed;

import android.content.Context;
import android.content.SharedPreferences;

import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Goal 10 is an end-to-end certification layer, not a second router.
 * It reuses the nine accepted capabilities and checks that they cooperate.
 */
public final class MiniAiAssistantCycle {
    public static final class Result {
        public final int passed;
        public final int total;
        public final int percent;
        public final String evidence;
        public final List<String> failed;

        Result(int passed, int total, String evidence) {
            this(passed,total,evidence,new ArrayList<>());
        }
        Result(int passed, int total, String evidence, List<String> missing) {
            this.failed = new ArrayList<>(missing);
            this.passed = passed;
            this.total = total;
            this.percent = total <= 0 ? 0 : Math.max(0, Math.min(100,
                    (int)Math.round((passed * 100.0) / total)));
            this.evidence = evidence;
        }
    }

    private MiniAiAssistantCycle() {}

    public static Result evaluate(Context context) {
        List<String> ok = new ArrayList<>();
        List<String> ko = new ArrayList<>();

        if (MiniAiGoals.completedCount(context) < 9) {
            return new Result(0, 10,
                    "Prerequisito: completare prima gli Obiettivi 1–9.");
        }

        check(ok, ko, "comprensione", () -> {
            JSONObject j = new JSONObject(MainActivity.nativeGoal1Classify("quanto fa sette piu otto"));
            return "calcolo".equals(j.optString("intent")) && j.optDouble("confidence", 0.0) >= 0.50;
        });

        check(ok, ko, "risposta", () -> {
            JSONObject j = new JSONObject(MainActivity.nativeGoal2Respond("ciao"));
            return !j.optString("reply", "").trim().isEmpty();
        });

        check(ok, ko, "memoria", () -> {
            JSONObject j = new JSONObject(MainActivity.nativeGoal3Classify("come mi chiamo"));
            return "recall".equals(j.optString("action"))
                    && "name".equals(j.optString("slot"))
                    && j.optDouble("confidence", 0.0) >= 0.50;
        });

        check(ok, ko, "ragionamento", () -> {
            JSONObject j = new JSONObject(MainActivity.nativeGoal4Solve(
                    "parto da 10 aggiungo 3 poi tolgo 4"));
            return j.optBoolean("valid", false)
                    && "add_sub".equals(j.optString("plan"))
                    && j.optLong("result", Long.MIN_VALUE) == 9L;
        });

        check(ok, ko, "incertezza", () -> {
            JSONObject j = new JSONObject(MainActivity.nativeGoal5Classify("che tempo fa oggi"));
            return "verify".equals(j.optString("decision"))
                    && j.optDouble("confidence", 0.0) >= 0.55;
        });

        check(ok, ko, "ricerca", () -> {
            JSONObject wiki = new JSONObject(MainActivity.nativeGoal6Plan("chi e michelangelo"));
            JSONObject live = new JSONObject(MainActivity.nativeGoal6Plan("che temperatura c e adesso"));
            return "wikipedia".equals(wiki.optString("source"))
                    && "live".equals(live.optString("source"))
                    && !wiki.optString("query", "").trim().isEmpty();
        });

        check(ok, ko, "strumenti", () -> {
            JSONObject calc = new JSONObject(MainActivity.nativeGoal7Route("quanto fa diciassette piu quattro"));
            JSONObject search = new JSONObject(MainActivity.nativeGoal7Route("chi e dante"));
            return "calculator".equals(calc.optString("tool"))
                    && "search".equals(search.optString("tool"));
        });

        check(ok, ko, "piano", () -> {
            JSONObject j = new JSONObject(MainActivity.nativeGoal8Plan(
                    "quanto fa sette piu otto e poi cerca informazioni su marte"));
            return "sequence".equals(j.optString("mode"))
                    && !j.optString("first", "").isEmpty()
                    && !j.optString("second", "").isEmpty();
        });

        check(ok, ko, "autocontrollo-calcolo", () -> {
            JSONObject j = new JSONObject(MainActivity.nativeGoal9Review(
                    "quanto fa diciassette piu quattro", "17 + 4 = 22"));
            return "calculation".equals(j.optString("check"))
                    && j.optDouble("confidence", 0.0) >= 0.50;
        });

        check(ok, ko, "autocontrollo-fonte", () -> {
            JSONObject j = new JSONObject(MainActivity.nativeGoal9Review(
                    "chi e michelangelo", "Michelangelo era un artista. Fonte: Wikipedia"));
            return "source".equals(j.optString("check"))
                    && j.optDouble("confidence", 0.0) >= 0.50;
        });

        String evidence = String.format(Locale.ITALY,
                "Ciclo mini-assistente: %d/%d prove superate. OK: %s%s",
                ok.size(), ok.size() + ko.size(),
                ok.isEmpty() ? "nessuna" : join(ok),
                ko.isEmpty() ? "" : " · Da correggere: " + join(ko));
        SharedPreferences p=context.getSharedPreferences("motorai_goal10_cert", Context.MODE_PRIVATE);
        p.edit().putString("last_evidence", evidence)
                .putString("failed_names", join(ko))
                .putInt("passed", ok.size())
                .putInt("total", ok.size()+ko.size())
                .putLong("updated_ms", System.currentTimeMillis()).apply();
        return new Result(ok.size(), ok.size() + ko.size(), evidence, ko);
    }

    private interface Check { boolean run() throws Exception; }

    private static void check(List<String> ok, List<String> ko, String name, Check c) {
        try {
            if (c.run()) ok.add(name); else ko.add(name);
        } catch (Throwable t) {
            ko.add(name);
        }
    }

    private static String join(List<String> xs) {
        StringBuilder b = new StringBuilder();
        for (String x : xs) {
            if (b.length() > 0) b.append(", ");
            b.append(x);
        }
        return b.toString();
    }
}
