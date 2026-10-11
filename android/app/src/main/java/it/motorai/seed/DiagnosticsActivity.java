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
    private TextView goal10Details;
    private TextView goal10Inspector;
    private TextView goal1Trial;
    private TextView candidateCertification;
    private TextView promotionOutcome;
    private TextView generalizationReport;
    private TextView phase2ShadowReport;

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

        root.addView(text("🎯 Goal 10 — verifiche dettagliate", 18, true));
        goal10Details = text("Certificazione ancora da misurare.", 14, false);
        root.addView(goal10Details);
        root.addView(text("🔬 Goal 10 — Prove reali in sola lettura", 18, true));
        goal10Inspector = text("Tocca il pulsante per leggere le dieci risposte, senza modificare alcun dato.", 14, false);
        root.addView(goal10Inspector);
        Button inspect = button("🔬 Verifica Goal 10 (sola lettura)");
        root.addView(inspect);
        root.addView(text("🧪 Goal 1 — Prova in copia isolata", 18, true));
        goal1Trial = text("Cervello attivo invariato. Prova non ancora avviata.",14,false);
        root.addView(goal1Trial);
        Button preview = button("🧪 Prova recupero Goal 1 (copia isolata)");
        root.addView(preview);
        root.addView(text("🔐 Goal 10 — Certificazione della candidata isolata", 18, true));
        candidateCertification = text("Non ancora verificata; non applicherà alcun peso.",14,false);
        root.addView(candidateCertification);
        Button candidateCheck = button("🔐 Verifica candidata Goal 10 (sola lettura)");
        root.addView(candidateCheck);
        promotionOutcome=text("Promozione reale non ancora eseguita. Pausa SOS richiesta.",14,false);
        root.addView(promotionOutcome);
        Button promote=button("🛡️ Promuovi candidata Goal 10 (protetto)");
        root.addView(promote);

        root.addView(text("🧭 Fase 2 — 20 richieste nuove (sola lettura)",18,true));
        generalizationReport=text("La Fase 2 non è stata ancora misurata. Pausa SOS richiesta.",14,false);
        root.addView(generalizationReport);
        Button probe20=button("🧪 Esegui 20 prove nuove (senza allenamento)");
        root.addView(probe20);
        root.addView(text("🧪 R3R10 — Miglioramento in copia isolata",18,true));
        phase2ShadowReport=text("Prova non ancora avviata. Resta obbligatoria la pausa SOS.",14,false);
        root.addView(phase2ShadowReport);
        Button phase2Shadow=button("🛡️ R3R10 · Allena soltanto la copia (Goal1/Goal7)");
        root.addView(phase2Shadow);

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
        inspect.setOnClickListener(v -> {
            inspect.setEnabled(false);
            goal10Inspector.setText("Controllo senza allenamento in corso…");
            new Thread(() -> {
                String outcome;
                try { outcome = MiniAiGoal10Inspector.inspect(); }
                catch (Throwable e) { outcome = "Diagnosi interrotta: " + e.getClass().getSimpleName(); }
                final String textResult = outcome;
                runOnUiThread(() -> {
                    goal10Inspector.setText(textResult);
                    inspect.setEnabled(true);
                });
            }, "Goal10-ReadOnly").start();
        });
        preview.setOnClickListener(v -> {
            preview.setEnabled(false);
            goal1Trial.setText("Allenamento soltanto nella copia temporanea…");
            new Thread(() -> {
                String result;
                try { result=MotorAIGoal10ShadowTrial.preview(this); }
                catch(Throwable e){result="Prova interrotta: "+e.getClass().getSimpleName()
                    +" — cervello attivo non allenato.";}
                final String shown=result;
                runOnUiThread(() -> {goal1Trial.setText(shown);preview.setEnabled(true);});
            },"Goal1-Shadow-Preview").start();
        });
        candidateCheck.setOnClickListener(v -> {
            candidateCheck.setEnabled(false);
            candidateCertification.setText("Verifica isolata in corso, pesi originali invariati…");
            new Thread(() -> {
                String outcome;
                try { outcome=MiniAiGoal10CandidateInspector.inspect(this); }
                catch(Throwable e){ outcome="Verifica isolata interrotta: "+e.getClass().getSimpleName()
                    +". Nessun peso applicato."; }
                final String value=outcome;
                runOnUiThread(() -> {
                    candidateCertification.setText(value);
                    candidateCheck.setEnabled(true);
                });
            },"Goal10-Candidate-ReadOnly").start();
        });
        promote.setOnClickListener(v -> new android.app.AlertDialog.Builder(this)
            .setTitle("Promozione protetta Goal 10")
            .setMessage("Applica la candidata solo dopo ricertificazione sul modello attivo. Richiede pausa SOS, conserva CURRENT originale e PREVIOUS, non esegue allenamento. Il backup SOS esterno va mantenuto.")
            .setNegativeButton("Annulla", (dialog, which) -> {})
            .setPositiveButton("Verifica e applica", (dialog, which) -> {
                promote.setEnabled(false);
                promotionOutcome.setText("Transazione protetta in corso…");
                new Thread(() -> {
                    String result;
                    try { result=MotorAIGoal10Promotion.promote(this); }
                    catch(Throwable t) { result="⛔ Promozione interrotta: "+t.getClass().getSimpleName(); }
                    final String shown=result;
                    runOnUiThread(() -> {
                        promotionOutcome.setText(shown);
                        promote.setEnabled(true);
                        load();
                    });
                },"Goal10-Guarded-Promotion").start();
            }).show());
        probe20.setOnClickListener(v -> {
            probe20.setEnabled(false);
            generalizationReport.setText("Sondaggio delle 20 richieste in corso…");
            new Thread(() -> {
                String output;
                try {output=MotorAIGeneralizationProbe.run(this);}
                catch(Throwable e){output="⛔ Prova interrotta: "+e.getClass().getSimpleName();}
                final String shown=output;
                runOnUiThread(() -> {
                    generalizationReport.setText(shown);
                    probe20.setEnabled(true);
                });
            },"MotorAI-20-Unseen-ReadOnly").start();
        });
        phase2Shadow.setOnClickListener(v -> {
            new android.app.AlertDialog.Builder(this)
                .setTitle("R3R10 — solo copia isolata")
                .setMessage("Conserva la pausa SOS e il backup esterno. Non tocca CURRENT, PREVIOUS o i 10 Goal. Solo la copia R3R10 verrà allenata; nessuna promozione automatica.")
                .setNegativeButton("Annulla",(dialog,which)->{})
                .setPositiveButton("Esegui prova isolata",(dialog,which)->{
                    phase2Shadow.setEnabled(false);
                    phase2ShadowReport.setText("Allenamento isolato Goal1/Goal7 in corso…");
                    new Thread(()->{
                        String result=MotorAIPhase2ShadowTrial.run(this);
                        runOnUiThread(()->{
                            phase2ShadowReport.setText(result);
                            phase2Shadow.setEnabled(true);
                        });
                    },"MotorAI-Phase2-Shadow").start();
                }).show();
        });
        copy.setOnClickListener(v -> {
            String all = report.getText().toString() + "\n\n🎯 GOAL 10\n" + goal10Details.getText() + "\n\n🔬 PROVE REALI NON INVASIVE\n" + goal10Inspector.getText() + "\n\n🧪 PROVA GOAL1 ISOLATA\n" + goal1Trial.getText() + "\n\n🔐 CERTIFICAZIONE CANDIDATA ISOLATA\n" + candidateCertification.getText() + "\n\n🛡️ PROMOZIONE REALE R3R8\n" + promotionOutcome.getText() + "\n\n🧭 FASE 2 · 20 PROVE NUOVE\n" + generalizationReport.getText() + "\n\n🛡️ R3R10 COPIA ISOLATA\n" + phase2ShadowReport.getText() + "\n\n⚠️ PROBLEMI RILEVATI\n" +
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
        SharedPreferences gp=getSharedPreferences("motorai_goal10_cert", MODE_PRIVATE);
        promotionOutcome.setText(MotorAIGoal10Promotion.last(this));
        goal10Details.setText(gp.getString("last_evidence",
                "Goal 10 non ancora sottoposto a certificazione integrata."));
    }
}
