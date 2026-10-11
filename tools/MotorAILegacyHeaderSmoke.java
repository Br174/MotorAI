import it.motorai.seed.MotorAILegacyHeader;
public final class MotorAILegacyHeaderSmoke {
  private static void put(byte[] b,int off,int v) {
    for(int i=0;i<4;i++) b[off+i]=(byte)((v>>(i*8))&255);
  }
  private static void check(int family,int ver,int step,int level,int start,boolean ok) {
    byte[] b=new byte[48];
    byte[] sig=String.format(java.util.Locale.US,"MOTAI%03d",family)
                     .getBytes(java.nio.charset.StandardCharsets.US_ASCII);
    System.arraycopy(sig,0,b,0,8);put(b,8,ver);put(b,16,step);
    if(family>4)put(b,20,level);
    if(family>=8)put(b,24,start);
    MotorAILegacyHeader.Parsed p=MotorAILegacyHeader.parse(b);
    if(p.compatible()!=ok || (ok && (p.step!=step||p.level!=level))) {
       throw new AssertionError(family+" result "+p.diagnostic);
    }
  }
  public static void main(String[] args){
    for(int family=4;family<=20;family++){
       int ver=family==4?1:family<=7?2:family<=9?3:family-6;
       int level=family==4?0:family<=7?1:family<=9?3:family==10?4:5;
       check(family,ver,3100,level,level==0?0:level==1?220:620,true);
       check(family,ver+1,3100,level,0,false);
    }
    check(20,14,1760,4,1760,true);
    check(20,14,1760,4,1761,false);
    byte[] bad=new byte[32];System.arraycopy("BROKEN00".getBytes(),0,bad,0,8);
    if(MotorAILegacyHeader.parse(bad).compatible())throw new AssertionError("bad magic passed");
    System.out.println("PASS: 17 native header families, malformed/corrupt headers rejected");
  }
}
