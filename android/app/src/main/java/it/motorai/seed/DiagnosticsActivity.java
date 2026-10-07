package it.motorai.seed;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Typeface;
import android.os.Bundle;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

public class DiagnosticsActivity extends Activity {
    private TextView report;
    private TextView failures;

    private TextView text(String value, int sp, boolean bold) {
        TextView v = new TextView(this);
        v.setText(value);
        v.setTextSize(sp);
        v.setPadding(22, 10, 22, 10);
        if (bold) v.setTypeface(Typeface.DEFAULT_BOLD);
        return v;
    }

    private Button button(String value) {
        Button b = new Button(this);
        b.setText(value);
        b.setAllCaps(false);
        return b;
    }

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);

        ScrollView scroll = new ScrollView(this);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(20, 28, 20, 28);
        scroll.addView(root);

        root.addView(text("🩺 Diagnostica MotorAI", 25, true));
        root.addView(text(
                "Snapshot automatico circa ogni 20 minuti mentre MotorAI è attiva. " +
                "Errori, rollback e blocchi importanti vengono registrati subito.", 14, false));

        report = text("Caricamento diagnosi…", 14, false);
        root.addView(report);

        root.addView(text("⚠️ Problemi rilevati", 18, true));
        failures = text("Nessun problema registrato.", 14, false);
        root.addView(failures);

        Button refresh = button("↻ Rileggi pagina");
        Button copy = button("📋 Copia diagnosi");
        Button close = button("← Torna a MotorAI");
        root.addView(refresh);
        root.addView(copy);
        root.addView(close);

        refresh.setOnClickListener(v -> load());
        copy.setOnClickListener(v -> {
            String all = report.getText().toString() + "\n\n⚠️ PROBLEMI RILEVATI\n" +
                    failures.getText().toString();
            ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
            cm.setPrimaryClip(ClipData.newPlainText("Diagnostica MotorAI", all));
            Toast.makeText(this, "Diagnostica copiata", Toast.LENGTH_SHORT).show();
        });
        close.setOnClickListener(v -> finish());

        setContentView(scroll);
        load();
    }

    @Override protected void onResume() {
        super.onResume();
        load();
    }

    private void load() {
        SharedPreferences p = getSharedPreferences("motorai_diagnostics", MODE_PRIVATE);
        String body = p.getString("last_report",
                "Nessuno snapshot disponibile. Torna a MotorAI e apri di nuovo Diagnostica.");
        String errs = p.getString("failures", "");
        report.setText(body);
        failures.setText(errs.isEmpty() ? "✅ Nessun problema registrato." : errs);
    }
}
