package it.motorai.seed;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.graphics.Typeface;
import java.io.File;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.List;

/** Explicit owner-controlled checkpoint recovery screen, intentionally no training. */
public final class MotorAIRecoveryActivity extends Activity {
    private static final int EXPORT=5401,IMPORT=5402;
    private TextView detail;
    private TextView status;
    private boolean exported=false;
    private MotorAIRecoveryStore.ImportData staged;

    private TextView text(String s,int sp,boolean bold) {
        TextView v=new TextView(this);v.setText(s);v.setTextSize(sp);
        v.setPadding(18,12,18,12);
        if(bold)v.setTypeface(Typeface.DEFAULT_BOLD);
        return v;
    }
    private Button button(LinearLayout root,String s,Runnable action){
        Button b=new Button(this);b.setAllCaps(false);b.setText(s);
        root.addView(b);b.setOnClickListener(v->action.run());return b;
    }
    private void runIo(Task work){
        status.setText("Operazione in corso…");
        new Thread(()->{
            try{
                String result=work.run();
                runOnUiThread(()->{status.setText(result);showState();});
            }catch(Exception ex){
                runOnUiThread(()->status.setText("Operazione non completata: "+ex.getMessage()));
            }
        },"MotorAI-Recovery-IO").start();
    }
    private interface Task {String run() throws Exception;}
    @Override public void onCreate(Bundle b) {
        super.onCreate(b);
        MotorAIRecoveryMode.pause(this);
        ScrollView scroll=new ScrollView(this);
        LinearLayout root=new LinearLayout(this);root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(18,22,18,22);scroll.addView(root);
        root.addView(text("🛟 MotorAI — Recupero d'emergenza",24,true));
        root.addView(text("Allenamento SOSPESO. Qui nessuna verifica modifica pesi o checkpoint. Prima esportare lo ZIP su una cartella esterna, poi valutare il ripristino.",15,false));
        detail=text("",14,false);root.addView(detail);
        status=text("Nessuna modifica eseguita.",15,true);root.addView(status);
        button(root,"🔎 Esamina i checkpoint (sola lettura)",this::showState);
        button(root,"📦 Salva checkpoint ZIP fuori dall'app",()->{
            Intent i=new Intent(Intent.ACTION_CREATE_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);i.setType("application/zip");
            i.putExtra(Intent.EXTRA_TITLE,"MotorAI-SOS-checkpoint.zip");
            startActivityForResult(i,EXPORT);
        });
        button(root,"📁 Seleziona ZIP esterno per controllo",()->{
            Intent i=new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);i.setType("application/zip");
            startActivityForResult(i,IMPORT);
        });
        button(root,"↩️ Ripristina checkpoint PREVIOUS (dopo backup)",()->{
            if(!exported){status.setText("Prima esportare uno ZIP con successo.");return;}
            File previous=new File(getFilesDir(),"motorai/checkpoints/previous");
            MotorAIRecoveryStore.Checkpoint match=null;
            for(MotorAIRecoveryStore.Checkpoint cp:MotorAIRecoveryStore.inspect(this))
                if("previous".equals(cp.name))match=cp;
            if(match==null||!match.valid){status.setText("Previous non utilizzabile.");return;}
            final String what=match.label();
            new AlertDialog.Builder(this)
               .setTitle("Conferma recupero Previous")
               .setMessage("Checkpoint scelto:\n"+what+"\n\n"
                    +"Il current attuale verrà conservato nella cartella di sicurezza. "
                    +"L'allenamento rimane fermo. Vuole procedere?")
               .setNegativeButton("Annulla",null)
               .setPositiveButton("Ripristina", (dialog,which)->
                    runIo(()->MotorAIRecoveryStore.restore(this,previous,null))).show();
        });
        button(root,"📥 Ripristina ZIP verificato (dopo backup)",()->{
            if(!exported){status.setText("Prima esportare l'attuale stato in ZIP.");return;}
            if(staged==null){status.setText("Prima selezionare e verificare uno ZIP esterno.");return;}
            final MotorAIRecoveryStore.ImportData data=staged;
            new AlertDialog.Builder(this).setTitle("Conferma importazione")
             .setMessage("Il file contiene:\n"+data.label+"\n\n"
               +"Checkpoint attuale conservato in sicurezza. Le preferenze dei Goal contenute "
               +"nello ZIP saranno ripristinate. Proseguire?")
             .setNegativeButton("Annulla",null)
             .setPositiveButton("Ripristina", (dialog,which)->runIo(()->
               MotorAIRecoveryStore.restore(this,
                 new File(data.stage,"checkpoints/current"),data.stage))).show();
        });
        button(root,"▶️ Consenti nuovamente l'allenamento",()->{
            new AlertDialog.Builder(this).setTitle("Uscire dalla protezione?")
              .setMessage("L'allenamento automatico sarà nuovamente consentito. "
               +"Si consiglia di esportare prima uno ZIP e verificare i checkpoint.")
              .setNegativeButton("Mantieni protezione",null)
              .setPositiveButton("Sblocca", (dialog,which)->{
                 MotorAIRecoveryMode.allowTraining(this);
                 MotorAIBackgroundJobService.schedule(this);
                 status.setText("Protezione disattivata. Può tornare a Laboratorio.");
                 showState();
              }).show();
        });
        button(root,"← Torna a MotorAI",this::finish);
        setContentView(scroll);
        showState();
    }
    private void showState() {
        List<MotorAIRecoveryStore.Checkpoint> cps=MotorAIRecoveryStore.inspect(this);
        StringBuilder summary=new StringBuilder();
        summary.append(MotorAIRecoveryMode.paused(this)?
            "🔒 ALLENAMENTO IN PAUSA PROTETTIVA\n":"⚠️ Allenamento sbloccato\n");
        for(MotorAIRecoveryStore.Checkpoint cp:cps)summary.append("\n").append(cp.label()).append("\n");
        summary.append("\nZIP esportato in questa sessione: ").append(exported?"SÌ":"NO");
        summary.append("\nCheckpoint PREVIOUS non viene promosso automaticamente.");
        detail.setText(summary.toString());
    }
    @Override protected void onActivityResult(int req,int result,Intent data){
        super.onActivityResult(req,result,data);
        if(result!=RESULT_OK||data==null||data.getData()==null)return;
        final Uri uri=data.getData();
        if(req==EXPORT) {
            runIo(()->{
                try(OutputStream output=getContentResolver().openOutputStream(uri,"w")){
                    if(output==null)throw new java.io.IOException("Destinazione non scrivibile");
                    MotorAIRecoveryStore.exportZip(this,output);
                }
                exported=true;
                return "ZIP salvato. Conservi una copia su Drive o SSD.";
            });
        } else if(req==IMPORT) {
            runIo(()->{
                if(staged!=null){MotorAIRecoveryStore.discard(staged);staged=null;}
                try(InputStream input=getContentResolver().openInputStream(uri)){
                    if(input==null)throw new java.io.IOException("Impossibile aprire ZIP");
                    staged=MotorAIRecoveryStore.inspectZip(this,input);
                }
                return "ZIP verificato: "+staged.label+" — nessun dato modificato.";
            });
        }
    }
    @Override protected void onDestroy(){
        super.onDestroy();
        if(staged!=null){MotorAIRecoveryStore.discard(staged);staged=null;}
    }
}
