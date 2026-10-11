package it.motorai.seed;

import android.content.Context;
import android.content.SharedPreferences;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.*;
import java.util.zip.*;

/** Non-destructive checkpoint inspection and SAF ZIP backup/import for emergency recovery.
 * File transactions share MotorAICheckpointStore's existing single-writer mutex.
 */
public final class MotorAIRecoveryStore {
    public static final String[] SLOTS={"current","previous","autotrain-baseline","previous-r2-safety"};
    private static final String[] PREFS={
        "motorai_mini_ai_goals","motorai_evolution","motorai_runtime",
        "motorai_training_status","motorai_goal10_recovery","motorai_goal10_cert"
    };
    private static final int MAX_FILE=32*1024*1024;
    private static final long MAX_TOTAL=112L*1024*1024;
    private MotorAIRecoveryStore() {}

    private static File root(Context c) {return new File(c.getFilesDir(),"motorai/checkpoints");}
    private static boolean contains(String[] xs,String v) {for(String x:xs) if(x.equals(v))return true;return false;}
    private static byte[] read(File f,int limit) throws IOException {
        if(!f.isFile()||f.length()<=0||f.length()>limit)throw new IOException("File non disponibile o troppo grande: "+f.getName());
        try(InputStream in=new FileInputStream(f);ByteArrayOutputStream out=new ByteArrayOutputStream()){
            byte[] chunk=new byte[8192];int n;
            while((n=in.read(chunk))!=-1){out.write(chunk,0,n);if(out.size()>limit)throw new IOException("Limite dimensione");}
            return out.toByteArray();
        }
    }
    private static void copy(InputStream in,OutputStream out,int limit)throws IOException {
        byte[] chunk=new byte[8192];int n,total=0;
        while((n=in.read(chunk))!=-1){total+=n;if(total>limit)throw new IOException("Archivio troppo grande");out.write(chunk,0,n);}
    }
    private static String sha(byte[] bytes)throws Exception{
        byte[] hash=MessageDigest.getInstance("SHA-256").digest(bytes);
        StringBuilder b=new StringBuilder();
        for(byte x:hash)b.append(String.format(Locale.US,"%02x",x&255));
        return b.toString();
    }
    public static final class Checkpoint {
        public final String name;public final boolean valid;public final int level;public final int step;
        public final double accuracy;public final int[] goalSteps;public final String fingerprint;public final String detail;
        Checkpoint(String n,boolean v,int l,int s,double a,int[] g,String h,String d){
            name=n;valid=v;level=l;step=s;accuracy=a;goalSteps=g;fingerprint=h;detail=d;
        }
        public String label(){
            if(!valid)return name+": NON valido ("+detail+")";
            StringBuilder g=new StringBuilder();
            for(int i=0;i<goalSteps.length;i++)if(goalSteps[i]>0)g.append(" G").append(i+1).append("=").append(goalSteps[i]);
            return name+" · L"+level+" · step "+step+" · validation "+
                    String.format(Locale.ITALY,"%.1f%%",accuracy*100.0)+g+" · SHA "+fingerprint.substring(0,12);
        }
    }
    private static Checkpoint scanDir(String slot,File dir) {
        try {
            byte[] header=read(new File(dir,"weights.bin"),MAX_FILE);
            byte[] text=read(new File(dir,"checkpoint.json"),128*1024);
            if(header.length<32||!new String(header,0,8,StandardCharsets.US_ASCII).equals("MOTAI020"))throw new IOException("Formato pesi sconosciuto");
            int ver=(header[8]&255)|((header[9]&255)<<8)|((header[10]&255)<<16)|((header[11]&255)<<24);
            if(ver!=14)throw new IOException("Versione pesi incompatibile: "+ver);
            JSONObject j=new JSONObject(new String(text,StandardCharsets.UTF_8));
            if(!"MOTORAI_CHECKPOINT_NATIVE_V2".equals(j.optString("format")))throw new IOException("Formato metadata incompatibile");
            int step=j.optInt("global_step",-1), level=j.optInt("curriculum_level",-1);
            double acc=j.optDouble("validation_answer_accuracy",Double.NaN);
            if(step<0||level<0||level>5||!Double.isFinite(acc)||acc<0||acc>1)throw new IOException("Metadata non validi");
            int[] goals=new int[9];
            for(int i=0;i<9;i++)goals[i]=j.optInt("goal"+(i+1)+"_step",0);
            return new Checkpoint(slot,true,level,step,acc,goals,sha(header),"OK");
        }catch(Exception ex){return new Checkpoint(slot,false,0,0,0,new int[9],"",ex.getMessage());}
    }
    public static List<Checkpoint> inspect(Context c) {
        synchronized(MotorAICheckpointStore.recoveryMutex()){
            List<Checkpoint> list=new ArrayList<>();
            for(String name:SLOTS)list.add(scanDir(name,new File(root(c),name)));
            return list;
        }
    }

    private static void put(ZipOutputStream zip,String name,byte[] bytes,JSONObject hashes)throws Exception{
        zip.putNextEntry(new ZipEntry(name));zip.write(bytes);zip.closeEntry();hashes.put(name,sha(bytes));
    }
    private static byte[] prefsJson(Context c,String name)throws Exception{
        JSONObject root=new JSONObject();
        for(Map.Entry<String,?> entry:c.getSharedPreferences(name,Context.MODE_PRIVATE).getAll().entrySet()){
            Object v=entry.getValue(),value=null;String type="";
            if(v instanceof String){type="s";value=v;}
            else if(v instanceof Integer){type="i";value=v;}
            else if(v instanceof Long){type="l";value=v;}
            else if(v instanceof Boolean){type="b";value=v;}
            else if(v instanceof Float){type="f";value=v;}
            else if(v instanceof Set){type="a";JSONArray arr=new JSONArray();
                for(Object t:(Set<?>)v)if(t instanceof String)arr.put(t);
                value=arr;
            }
            if(value!=null)root.put(entry.getKey(),new JSONObject().put("type",type).put("value",value));
        }
        return root.toString().getBytes(StandardCharsets.UTF_8);
    }
    public static void exportZip(Context c,OutputStream output)throws Exception{
        synchronized(MotorAICheckpointStore.recoveryMutex()){
            JSONObject hashes=new JSONObject();int count=0;
            ZipOutputStream zip=new ZipOutputStream(output);
            for(String name:SLOTS){
                File dir=new File(root(c),name);
                if(!scanDir(name,dir).valid)continue;
                for(String file:new String[]{"checkpoint.json","weights.bin"}){
                    put(zip,"checkpoints/"+name+"/"+file,
                            read(new File(dir,file),MAX_FILE),hashes);
                }
                count++;
            }
            if(count==0)throw new IOException("Non ci sono checkpoint validi da esportare");
            for(String name:PREFS)put(zip,"prefs/"+name+".json",prefsJson(c,name),hashes);
            JSONObject manifest=new JSONObject()
                    .put("format","MOTORAI_RECOVERY_ZIP_V1")
                    .put("created_ms",System.currentTimeMillis())
                    .put("application_id","it.motorai.seed")
                    .put("entries_sha256",hashes)
                    .put("slots",count);
            put(zip,"manifest.json",manifest.toString(2).getBytes(StandardCharsets.UTF_8),
                    new JSONObject());
            zip.finish();zip.flush();
        }
    }
    public static final class ImportData {
        public final File stage;
        public final String label;
        ImportData(File f,String l){stage=f;label=l;}
    }
    public static ImportData inspectZip(Context c,InputStream input)throws Exception{
        File stage=new File(c.getCacheDir(),"motorai-r3-import");
        if(stage.exists())delete(stage);
        if(!stage.mkdirs())throw new IOException("Memoria temporanea non disponibile");
        Map<String,String> hashes=new HashMap<>();long total=0;int count=0;
        try(ZipInputStream zip=new ZipInputStream(input)){
            ZipEntry entry;
            while((entry=zip.getNextEntry())!=null){
                if(entry.isDirectory())continue;
                String name=entry.getName();
                boolean validName="manifest.json".equals(name);
                for(String slot:SLOTS)for(String file:new String[]{"checkpoint.json","weights.bin"})
                    if(name.equals("checkpoints/"+slot+"/"+file))validName=true;
                for(String p:PREFS)if(name.equals("prefs/"+p+".json"))validName=true;
                if(!validName||hashes.containsKey(name)||++count>25)throw new IOException("ZIP non conforme");
                File target=new File(stage,name);
                File dir=target.getParentFile();if(!dir.exists()&&!dir.mkdirs())throw new IOException("Cartella temporanea");
                try(FileOutputStream file=new FileOutputStream(target)){
                    copy(zip,file,MAX_FILE);
                }
                total+=target.length();if(total>MAX_TOTAL)throw new IOException("ZIP troppo grande");
                hashes.put(name,sha(read(target,MAX_FILE)));
                zip.closeEntry();
            }
        }catch(Exception e){delete(stage);throw e;}
        File mf=new File(stage,"manifest.json");
        JSONObject manifest=new JSONObject(new String(read(mf,128*1024),StandardCharsets.UTF_8));
        if(!"MOTORAI_RECOVERY_ZIP_V1".equals(manifest.optString("format"))||
          !"it.motorai.seed".equals(manifest.optString("application_id")))throw new IOException("ZIP di un'altra app");
        JSONObject expected=manifest.getJSONObject("entries_sha256");
        if(expected.length()!=hashes.size()-1)throw new IOException("Archivio incompleto");
        for(String name:hashes.keySet())if(!name.equals("manifest.json") &&
          !hashes.get(name).equals(expected.optString(name)))throw new IOException("SHA-256 non corrisponde: "+name);
        File current=new File(stage,"checkpoints/current");
        Checkpoint best=scanDir("ZIP current",current);
        if(!best.valid)throw new IOException("Nessun checkpoint current utilizzabile nello ZIP");
        return new ImportData(stage,best.label());
    }
    public static void discard(ImportData data){if(data!=null)delete(data.stage);}
    private static boolean delete(File f){
        if(!f.exists())return true;
        if(f.isDirectory()){File[] children=f.listFiles();if(children==null)return false;
            for(File child:children)if(!delete(child))return false;}
        return f.delete();
    }
    private static void copyFile(File src,File dst)throws IOException{
        File dir=dst.getParentFile();if(!dir.exists()&&!dir.mkdirs())throw new IOException("Cartella non creabile");
        try(InputStream in=new FileInputStream(src);OutputStream out=new FileOutputStream(dst)){copy(in,out,MAX_FILE);}
    }
    private static void copySlot(File source,File target)throws IOException {
        if(target.exists()&&!delete(target))throw new IOException("Cartella di lavoro occupata");
        if(!target.mkdirs())throw new IOException("Cartella di lavoro");
        for(String filename:new String[]{"weights.bin","checkpoint.json"})
            copyFile(new File(source,filename),new File(target,filename));
    }
    private static void applyPrefs(Context c,File stage)throws Exception{
        for(String p:PREFS){
            File f=new File(stage,"prefs/"+p+".json");
            if(!f.isFile())continue;
            JSONObject j=new JSONObject(new String(read(f,MAX_FILE),StandardCharsets.UTF_8));
            SharedPreferences.Editor e=c.getSharedPreferences(p,Context.MODE_PRIVATE).edit().clear();
            Iterator<String> keys=j.keys();
            while(keys.hasNext()){
                String key=keys.next();JSONObject node=j.getJSONObject(key);
                String type=node.getString("type");
                switch(type){
                  case "s":e.putString(key,node.getString("value"));break;
                  case "i":e.putInt(key,node.getInt("value"));break;
                  case "l":e.putLong(key,node.getLong("value"));break;
                  case "b":e.putBoolean(key,node.getBoolean("value"));break;
                  case "f":e.putFloat(key,(float)node.getDouble("value"));break;
                  case "a":JSONArray a=node.getJSONArray("value");Set<String> set=new HashSet<>();
                        for(int n=0;n<a.length();n++)set.add(a.getString(n));e.putStringSet(key,set);break;
                  default:throw new IOException("Tipo preferenza non consentito");
                }
            }
            if(!e.commit())throw new IOException("Errore recupero preferenze");
        }
    }

    /** Import or previous->current restoration, without touching previous or baseline.
     * Always retains first pre-restore current in an emergency safety folder.
     */
    public static String restore(Context c,File candidate,File importedPrefs)throws Exception {
        if(!MotorAIRecoveryMode.paused(c))throw new IOException("Modalità protezione non attiva");
        synchronized(MotorAICheckpointStore.recoveryMutex()){
            Checkpoint src=scanDir("candidato",candidate);
            if(!src.valid)throw new IOException(src.detail);
            File root=root(c);if(!root.isDirectory()&&!root.mkdirs())throw new IOException("Cartella checkpoint");
            File current=new File(root,"current"),stage=new File(root,"r3-restore-stage");
            File backup=new File(root,"r3-pre-restore-protected");
            if(backup.exists())throw new IOException("Esiste già un ripristino precedente: protezione attiva");
            copySlot(candidate,stage);
            if(!scanDir("stage",stage).valid)throw new IOException("Copia di ripristino non valida");
            boolean prior=current.exists();
            if(prior && !current.renameTo(backup))throw new IOException("Impossibile preservare current");
            if(!stage.renameTo(current)){
                if(prior)backup.renameTo(current);
                throw new IOException("Promozione checkpoint fallita");
            }
            if(!MotorAICheckpointStore.load(current.getAbsolutePath())){
                delete(current);
                if(prior)backup.renameTo(current);
                if(prior)MotorAICheckpointStore.load(current.getAbsolutePath());
                throw new IOException("Caricamento nativo non riuscito, rollback eseguito");
            }
            if(importedPrefs!=null)applyPrefs(c,importedPrefs);
            return "Ripristino completato da "+src.name+" · L"+src.level+" · step "+src.step+
                    ". Riavviare MotorAI per verificare.";
        }
    }
}
