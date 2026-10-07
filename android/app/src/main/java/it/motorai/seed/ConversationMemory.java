package it.motorai.seed;

import android.content.Context;
import android.content.SharedPreferences;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.Locale;

public final class ConversationMemory {
    private static final String PREFS = "motorai_conversation_memory";
    private static final int MAX_TURNS = 20;

    private ConversationMemory() {}

    private static SharedPreferences p(Context c) {
        return c.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    public static void put(Context c, String slot, String value) {
        if (slot == null || value == null) return;
        String key = slot.trim().toLowerCase(Locale.ITALY);
        String v = cleanValue(value);
        if (key.isEmpty() || v.isEmpty()) return;
        p(c).edit()
                .putString("slot_" + key, v)
                .putLong("slot_" + key + "_updated", System.currentTimeMillis())
                .apply();
    }

    public static String get(Context c, String slot) {
        if (slot == null) return "";
        return p(c).getString("slot_" + slot.trim().toLowerCase(Locale.ITALY), "");
    }

    public static void appendTurn(Context c, String role, String text) {
        try {
            JSONArray arr = historyArray(c);
            JSONObject o = new JSONObject();
            o.put("role", role == null ? "" : role);
            o.put("text", text == null ? "" : text);
            o.put("time", System.currentTimeMillis());
            arr.put(o);

            JSONArray kept = new JSONArray();
            int from = Math.max(0, arr.length() - MAX_TURNS);
            for (int i = from; i < arr.length(); i++) kept.put(arr.getJSONObject(i));
            p(c).edit().putString("turns_json", kept.toString()).apply();
        } catch (Exception ignored) {
        }
    }

    public static JSONArray historyArray(Context c) {
        try {
            return new JSONArray(p(c).getString("turns_json", "[]"));
        } catch (Exception e) {
            return new JSONArray();
        }
    }

    public static String extractValue(String slot, String original) {
        if (slot == null || original == null) return "";
        String lower = normalize(original);
        String[] markers;
        switch (slot) {
            case "name":
                markers = new String[]{"mi chiamo ", "mio nome e ", "nome e ", "nome "};
                break;
            case "city":
                markers = new String[]{"vivo a ", "abito a ", "mia citta e ", "citta dove vivo e ", "citta dove abito e ", "citta di "};
                break;
            case "color":
                markers = new String[]{"colore preferito e ", "preferisco il colore ", "colore e ", "preferisco "};
                break;
            case "pet":
                markers = new String[]{"animale domestico e ", "mio animale e ", "ho un ", "ho una "};
                break;
            default:
                return "";
        }

        for (String marker : markers) {
            int idx = lower.indexOf(marker);
            if (idx >= 0) {
                String v = lower.substring(idx + marker.length());
                return cleanValue(v);
            }
        }
        return "";
    }

    private static String cleanValue(String value) {
        String v = value == null ? "" : value.trim();
        while (!v.isEmpty() && ".,!?;:".indexOf(v.charAt(v.length() - 1)) >= 0) {
            v = v.substring(0, v.length() - 1).trim();
        }
        if (v.length() > 80) v = v.substring(0, 80).trim();
        return v;
    }

    private static String normalize(String s) {
        String out = s.toLowerCase(Locale.ITALY)
                .replace('à', 'a').replace('á', 'a')
                .replace('è', 'e').replace('é', 'e')
                .replace('ì', 'i').replace('í', 'i')
                .replace('ò', 'o').replace('ó', 'o')
                .replace('ù', 'u').replace('ú', 'u');
        return out.replaceAll("\\s+", " ").trim();
    }
}
