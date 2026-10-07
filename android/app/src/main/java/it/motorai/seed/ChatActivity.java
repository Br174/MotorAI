package it.motorai.seed;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import org.json.JSONObject;

import java.io.File;

import java.util.Locale;

public class ChatActivity extends Activity {
    private LinearLayout messages;
    private EditText input;
    private TextView status;
    private ScrollView scroll;
    private volatile boolean brainReady = false;

    private int dp(int v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }

    private GradientDrawable bubble(int color, float radius) {
        GradientDrawable d = new GradientDrawable();
        d.setColor(color);
        d.setCornerRadius(dp((int) radius));
        return d;
    }

    private TextView text(String value, int sp, boolean bold) {
        TextView v = new TextView(this);
        v.setText(value);
        v.setTextSize(sp);
        v.setTextColor(Color.rgb(30, 40, 65));
        if (bold) v.setTypeface(Typeface.DEFAULT_BOLD);
        return v;
    }

    private Button button(String label) {
        Button b = new Button(this);
        b.setText(label);
        b.setAllCaps(false);
        return b;
    }

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Color.rgb(247,249,253));
        root.setPadding(dp(14), dp(18), dp(14), dp(12));

        LinearLayout header = new LinearLayout(this);
        header.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout titleBox = new LinearLayout(this);
        titleBox.setOrientation(LinearLayout.VERTICAL);
        titleBox.addView(text("🧠 MotorAI", 27, true));
        status = text("Mini-AI · caricamento…", 13, false);
        titleBox.addView(status);
        header.addView(titleBox, new LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1));

        Button training = button("🏋️ Allenamento");
        Button lab = button("🧪 Laboratorio");
        header.addView(training);
        header.addView(lab);
        root.addView(header);

        TextView preview = text("Chat MotorAI", 15, true);
        preview.setPadding(dp(12),dp(9),dp(12),dp(9));
        preview.setBackground(bubble(Color.rgb(237,245,255),14));
        root.addView(preview);

        scroll = new ScrollView(this);
        messages = new LinearLayout(this);
        messages.setOrientation(LinearLayout.VERTICAL);
        messages.setPadding(0,dp(12),0,dp(12));
        scroll.addView(messages);
        root.addView(scroll, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,0,1));

        LinearLayout composer = new LinearLayout(this);
        composer.setGravity(Gravity.CENTER_VERTICAL);
        input = new EditText(this);
        input.setHint("Scrivi a MotorAI…");
        input.setSingleLine(false);
        input.setMaxLines(4);
        input.setImeOptions(EditorInfo.IME_ACTION_SEND);
        input.setBackground(bubble(Color.WHITE,18));
        input.setPadding(dp(14),dp(10),dp(14),dp(10));
        Button send = button("Invia");
        composer.addView(input,new LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT,1));
        composer.addView(send,new LinearLayout.LayoutParams(dp(90),
                LinearLayout.LayoutParams.WRAP_CONTENT));
        root.addView(composer);

        setContentView(root);

        addAssistant("Ciao. Questa è la nuova Chat di MotorAI. In questo momento posso già capire il tipo di richiesta; la capacità di formulare risposte naturali viene costruita nel percorso Mini-AI.");
        loadBrainCheckpoint();

        training.setOnClickListener(v -> startActivity(new Intent(this, TrainingActivity.class)));
        lab.setOnClickListener(v -> startActivity(new Intent(this, MainActivity.class)));
        send.setOnClickListener(v -> sendMessage());
        input.setOnEditorActionListener((v,action,event) -> {
            if(action==EditorInfo.IME_ACTION_SEND){ sendMessage(); return true; }
            return false;
        });
        refreshStatus();
    }

    @Override protected void onResume() {
        super.onResume();
        MotorAIBackgroundJobService.schedule(this);
        if (brainReady) MotorAIBackgroundJobService.scheduleKick(getApplicationContext());
        refreshStatus();
    }

    private void loadBrainCheckpoint() {
        status.setText("MotorAI · caricamento cervello…");
        new Thread(() -> {
            boolean ready = true;
            try {
                File current = new File(getFilesDir(), "motorai/checkpoints/current");
                if (current.exists()) ready = MainActivity.nativeLoadCheckpoint(current.getAbsolutePath());
            } catch (Throwable e) {
                ready = false;
            }
            final boolean ok = ready;
            brainReady = ok;
            runOnUiThread(() -> {
                refreshStatus();
                if (!ok) addAssistant("Non sono riuscita a caricare il checkpoint corrente. Apri il Laboratorio per la diagnostica.");
                else MotorAIBackgroundJobService.scheduleKick(getApplicationContext());
            });
        }, "MotorAI-ChatLoad").start();
    }

    private void refreshStatus() {
        MiniAiGoals.seedIfNeeded(this);
        int active = MiniAiGoals.activeGoalIndex(this);
        status.setText(String.format(Locale.ITALY,
                "Mini-AI %.1f%% · Obiettivo %d/10: %s",
                MiniAiGoals.totalPercent(this), active + 1, MiniAiGoals.status(this, active)));
    }

    private void sendMessage() {
        String q=input.getText().toString().trim();
        if(q.isEmpty()) return;
        if(!brainReady) {
            addAssistant("Sto ancora caricando il cervello. Riprova tra un momento.");
            return;
        }
        input.setText("");
        addUser(q);

        new Thread(() -> {
            String reply;
            try {
                JSONObject classified = new JSONObject(MainActivity.nativeGoal1Classify(q));
                String intent = classified.optString("intent","sconosciuto");
                double confidence = classified.optDouble("confidence",0.0);
                int goal2 = MiniAiGoals.percent(this,1);

                if(goal2 >= 100) {
                    JSONObject generated = new JSONObject(MainActivity.nativeGoal2Respond(q));
                    String learned = generated.optString("reply", "").trim();
                    if (learned.isEmpty()) {
                        reply = "Ho capito che è una richiesta di tipo “" + friendlyIntent(intent)
                                + "”, ma non sono riuscita a formulare la risposta.";
                    } else {
                        reply = learned;
                    }
                } else {
                    reply = "Ho capito che è una richiesta di tipo “" + friendlyIntent(intent)
                            + "” (" + Math.round(confidence*100.0) + "% di sicurezza). "
                            + "Sto ancora allenando la capacità di formulare risposte naturali "
                            + "(Obiettivo 2/10: " + goal2 + "%).";
                }
            } catch(Exception e) {
                reply="Ho ricevuto il messaggio, ma il modulo di comprensione non è disponibile in questo momento.";
            }
            final String r=reply;
            runOnUiThread(() -> addAssistant(r));
        },"motorai-chat").start();
    }

    private String friendlyIntent(String raw) {
        switch(raw) {
            case "saluto": return "saluto";
            case "informazione": return "richiesta di informazioni";
            case "calcolo": return "calcolo";
            case "ricerca": return "ricerca";
            case "azione": return "azione da eseguire";
            default: return raw;
        }
    }

    private void addUser(String message) {
        addBubble(message,true);
    }

    private void addAssistant(String message) {
        addBubble(message,false);
    }

    private void addBubble(String message, boolean user) {
        TextView v=text(message,16,false);
        v.setPadding(dp(14),dp(11),dp(14),dp(11));
        v.setBackground(bubble(user ? Color.rgb(218,238,255) : Color.WHITE,18));
        LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,LinearLayout.LayoutParams.WRAP_CONTENT);
        lp.gravity=user ? Gravity.END : Gravity.START;
        lp.setMargins(user ? dp(48) : 0,dp(6),user ? 0 : dp(48),dp(6));
        messages.addView(v,lp);
        scroll.post(() -> scroll.fullScroll(View.FOCUS_DOWN));
    }
}
