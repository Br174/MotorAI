package it.motorai.seed;

import android.content.Context;
import java.io.File;

/** Single coordinator for MotorAI checkpoint filesystem rotation and loading.
 * Existing native engine owns weights; this helper owns the filesystem transaction.
 */
public final class MotorAICheckpointStore {
    private static final Object LOCK = new Object();
    private MotorAICheckpointStore() {}
    static Object recoveryMutex() { return LOCK; }

    private static File root(Context c) {
        return new File(c.getFilesDir(), "motorai/checkpoints");
    }
    private static File at(File dir, String name) {
        return new File(dir, name);
    }
    private static boolean valid(File dir) {
        return dir.isDirectory() && at(dir, "weights.bin").isFile()
                && at(dir, "weights.bin").length() > 0
                && at(dir, "checkpoint.json").isFile()
                && at(dir, "checkpoint.json").length() > 0;
    }
    private static boolean deleteTree(File file) {
        if (!file.exists()) return true;
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children == null) return false;
            for (File child : children) if (!deleteTree(child)) return false;
        }
        return file.delete();
    }

    public static boolean rotateAndSave(Context context) {
        synchronized (LOCK) {
            File dir = root(context);
            if (!dir.exists() && !dir.mkdirs()) return false;
            File current=at(dir,"current"), previous=at(dir,"previous");
            File temp=at(dir,"tmp-r2"), backup=at(dir,"previous-r2-safety");
            if (!deleteTree(temp)) return false;
            if (!MainActivity.nativeSaveCheckpoint(temp.getAbsolutePath()) || !valid(temp)) {
                deleteTree(temp);
                return false;
            }

            // A failed transaction may leave the safety directory behind.
            // Never discard it if current/previous are not both healthy.
            if (backup.exists()) {
                if (!valid(current) || !valid(previous)) return false;
                if (!deleteTree(backup)) return false;
            }

            boolean hadPrevious=previous.exists(), hadCurrent=current.exists();
            if (hadPrevious && !previous.renameTo(backup)) return false;
            if (hadCurrent && !current.renameTo(previous)) {
                if (hadPrevious) backup.renameTo(previous);
                return false;
            }
            if (!temp.renameTo(current)) {
                if (hadCurrent) previous.renameTo(current);
                if (hadPrevious) backup.renameTo(previous);
                return false;
            }
            if (hadPrevious) deleteTree(backup);
            return valid(current);
        }
    }

    public static boolean load(String path) {
        synchronized (LOCK) {
            File dir=new File(path);
            return valid(dir) && MainActivity.nativeLoadCheckpoint(path);
        }
    }

    public static boolean loadCurrent(Context c) {
        synchronized (LOCK) {
            File dir=root(c);
            File current=at(dir,"current");
            return valid(current) && MainActivity.nativeLoadCheckpoint(current.getAbsolutePath());
        }
    }
}
