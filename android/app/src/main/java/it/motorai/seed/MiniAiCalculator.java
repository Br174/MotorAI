package it.motorai.seed;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

public final class MiniAiCalculator {
    public static final class Result {
        public final boolean ok;
        public final double value;
        public final String expression;
        Result(boolean ok,double value,String expression){
            this.ok=ok;this.value=value;this.expression=expression;
        }
    }

    private static final Map<String,Integer> WORDS=new HashMap<>();
    static {
        String[] n={"zero","uno","due","tre","quattro","cinque","sei","sette","otto","nove","dieci",
                "undici","dodici","tredici","quattordici","quindici","sedici","diciassette","diciotto","diciannove","venti"};
        for(int i=0;i<n.length;i++) WORDS.put(n[i],i);
    }

    private MiniAiCalculator(){}

    public static Result solve(String raw){
        if(raw==null)return new Result(false,0,"");
        String s=raw.toLowerCase(Locale.ITALY).replace("×"," per ").replace("÷"," diviso ");
        String normalized=s.replaceAll("[^a-z0-9+*/.\\-]+"," ").trim();
        List<Double> nums=new ArrayList<>();
        for(String t:normalized.split("\\s+")){
            if(t.isEmpty())continue;
            try{
                if(t.matches("-?\\d+(?:\\.\\d+)?")) nums.add(Double.parseDouble(t));
                else if(WORDS.containsKey(t)) nums.add(WORDS.get(t).doubleValue());
            }catch(Exception ignored){}
        }
        if(nums.size()<2)return new Result(false,0,"");

        double a=nums.get(0),b=nums.get(1),v;
        String op;
        if(s.contains(" diviso ")||s.contains("/")){if(b==0)return new Result(false,0,"");v=a/b;op="÷";}
        else if(s.contains(" per ")||s.contains("moltip")||s.contains("*")){v=a*b;op="×";}
        else if(s.contains(" meno ")||s.contains("sottra")||s.contains("-")){v=a-b;op="-";}
        else if(s.contains(" piu ")||s.contains(" più ")||s.contains("somma")||s.contains("+")){v=a+b;op="+";}
        else return new Result(false,0,"");
        return new Result(true,v,format(a)+" "+op+" "+format(b));
    }

    public static String format(double v){
        if(Math.rint(v)==v)return String.valueOf((long)v);
        return String.format(Locale.ITALY,"%.4f",v).replaceAll("0+$","").replaceAll("[,.]$","");
    }
}
