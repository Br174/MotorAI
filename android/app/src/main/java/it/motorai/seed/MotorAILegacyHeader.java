package it.motorai.seed;

import java.nio.charset.StandardCharsets;

/** Read-only native checkpoint header compatibility check (MOTAI004...MOTAI020).
 * Keep this aligned with Engine::loadCheckpoint; never modifies weights.
 */
public final class MotorAILegacyHeader {
    private MotorAILegacyHeader() {}

    public static final class Parsed {
        public final String magic;
        public final int version;
        public final int step;
        public final int level;
        public final int levelStart;
        public final String diagnostic;
        private Parsed(String m,int v,int s,int l,int start,String d) {
            magic=m;version=v;step=s;level=l;levelStart=start;diagnostic=d;
        }
        public boolean compatible() {return diagnostic.equals("OK");}
    }

    private static int u32(byte[] h,int offset) {
        return (h[offset]&255)|((h[offset+1]&255)<<8)
            |((h[offset+2]&255)<<16)|((h[offset+3]&255)<<24);
    }
    public static Parsed parse(byte[] h) {
        if(h==null||h.length<24)
            return new Parsed("?",-1,-1,-1,-1,"Header troppo corto");
        String magic=new String(h,0,8,StandardCharsets.US_ASCII);
        if(!magic.startsWith("MOTAI"))
            return new Parsed(magic,-1,-1,-1,-1,"Firma sconosciuta");
        int family;
        try{family=Integer.parseInt(magic.substring(5));}
        catch(Exception ex){return new Parsed(magic,-1,-1,-1,-1,"Firma illeggibile");}
        if(family<4||family>20)
            return new Parsed(magic,-1,-1,-1,-1,"Famiglia non supportata");
        int expected=family==4?1:(family<=7?2:family<=9?3:family-6);
        int version=u32(h,8);
        if(version!=expected)
            return new Parsed(magic,version,-1,-1,-1,"Versione non coerente");
        int required=family==4?24:family<=7?28:32;
        if(h.length<required)
            return new Parsed(magic,version,-1,-1,-1,"Header incompleto");
        int step=u32(h,16);
        int level=family==4?0:u32(h,20);
        int start=family==4?0:family<=7?(level==0?0:level==1?220:620):u32(h,24);
        if(step<0||step>100000000||level<0||level>(family<=7?2:family<=9?3:family==10?4:5)
          ||start<0||start>step)
            return new Parsed(magic,version,step,level,start,"Metadati header fuori intervallo");
        return new Parsed(magic,version,step,level,start,"OK");
    }
}
