package it.motorai.seed;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;

public final class MiniAiWebSearch {
    public static final class Result {
        public final boolean ok;
        public final String title;
        public final String extract;
        public final String source;
        public final String url;
        public final String error;

        Result(boolean ok, String title, String extract, String source, String url, String error) {
            this.ok = ok;
            this.title = title;
            this.extract = extract;
            this.source = source;
            this.url = url;
            this.error = error;
        }
    }

    private MiniAiWebSearch() {}

    public static Result wikipedia(String query) {
        String q = query == null ? "" : query.trim();
        if (q.isEmpty()) return new Result(false,"","","Wikipedia","","Query vuota.");

        try {
            String encoded = URLEncoder.encode(q, StandardCharsets.UTF_8.name());
            String searchUrl = "https://it.wikipedia.org/w/api.php?action=query&list=search"
                    + "&srsearch=" + encoded + "&srlimit=1&format=json&utf8=1";
            JSONObject search = getJson(searchUrl);
            JSONArray hits = search.getJSONObject("query").getJSONArray("search");
            if (hits.length() == 0) {
                return new Result(false,"","","Wikipedia","","Nessun risultato.");
            }

            JSONObject hit = hits.getJSONObject(0);
            long pageId = hit.getLong("pageid");
            String title = hit.getString("title");

            String extractUrl = "https://it.wikipedia.org/w/api.php?action=query&prop=extracts"
                    + "&exintro=1&explaintext=1&exsentences=4&pageids=" + pageId
                    + "&format=json&utf8=1";
            JSONObject detail = getJson(extractUrl);
            JSONObject pages = detail.getJSONObject("query").getJSONObject("pages");
            JSONObject page = pages.getJSONObject(String.valueOf(pageId));
            String extract = page.optString("extract","").trim();
            if (extract.length() > 1000) extract = extract.substring(0,1000).trim() + "…";
            if (extract.isEmpty()) {
                return new Result(false,title,"","Wikipedia","","Pagina trovata ma testo introduttivo assente.");
            }

            String pageUrl = "https://it.wikipedia.org/wiki/"
                    + URLEncoder.encode(title.replace(' ','_'), StandardCharsets.UTF_8.name())
                    .replace("+","%20");
            return new Result(true,title,extract,"Wikipedia",pageUrl,"");
        } catch (Exception e) {
            String msg = e.getMessage();
            if (msg == null || msg.trim().isEmpty()) msg = e.getClass().getSimpleName();
            return new Result(false,"","","Wikipedia","",msg);
        }
    }

    private static JSONObject getJson(String url) throws Exception {
        HttpURLConnection c = (HttpURLConnection) new java.net.URL(url).openConnection();
        c.setConnectTimeout(8000);
        c.setReadTimeout(10000);
        c.setRequestMethod("GET");
        c.setRequestProperty("Accept","application/json");
        c.setRequestProperty("User-Agent","MotorAI/0.17 Android local learning assistant");
        int code = c.getResponseCode();
        if (code < 200 || code >= 300) throw new IllegalStateException("HTTP " + code);

        StringBuilder b = new StringBuilder();
        try (BufferedReader r = new BufferedReader(new InputStreamReader(
                c.getInputStream(), StandardCharsets.UTF_8))) {
            String line;
            while ((line = r.readLine()) != null) b.append(line);
        } finally {
            c.disconnect();
        }
        return new JSONObject(b.toString());
    }
}
