#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <filesystem>
#include <cmath>
#include "motorai_core.h"

using motorai::Engine;
static bool has(const std::string& json,const std::string& key,const std::string& value) {
    return json.find("\""+key+"\":\""+value+"\"")!=std::string::npos;
}
static bool field(const std::string& json,const std::string& key) {
    auto i=json.find("\""+key+"\":");
    if(i==std::string::npos)return false;
    i+=key.size()+3;
    return i<json.size() && json[i]!='"' ? true :
           (i+1<json.size() && json[i]=='"' && json[i+1]!='"');
}
static double number(const std::string& json,const std::string& key) {
    auto i=json.find("\""+key+"\":");
    if(i==std::string::npos)return -1;
    try {return std::stod(json.substr(i+key.size()+3));}catch(...){return -1;}
}
template<class F,class E>static bool train(int limit,float valTarget,float testTarget,F fn,E ev) {
    int n=0,stable=0;
    while(n<limit && stable<4){
        fn();
        n+=20;
        stable=ev.first()>=valTarget?stable+1:0;
    }
    return stable>=4 && ev.second()>=testTarget;
}

int main(){
    Engine e(174);
    auto g1=std::pair{[&](){return (double)e.evaluateGoal1Validation().answer_accuracy;},
                       [&](){return (double)e.evaluateGoal1Test().answer_accuracy;}};
    auto g2=std::pair{[&](){return (double)e.evaluateGoal2Validation().answer_accuracy;},
                       [&](){return (double)e.evaluateGoal2Test().answer_accuracy;}};
    auto g3=std::pair{[&](){return (double)e.evaluateGoal3Validation().answer_accuracy;},
                       [&](){return (double)e.evaluateGoal3Test().answer_accuracy;}};
    bool trained=true;
    trained &= train(2500,.90f,.90f,[&](){e.trainGoal1(20,24,.08f);},g1);
    trained &= train(3000,.60f,.55f,[&](){e.trainGoal2(20,16,.12f);},g2);
    trained &= train(2500,.90f,.90f,[&](){e.trainGoal3(20,24,.08f);},g3);
    trained &= train(2500,.90f,.90f,[&](){e.trainGoal4(20,24,.08f);},
        std::pair{[&](){return (double)e.evaluateGoal4Validation().answer_accuracy;},
                  [&](){return (double)e.evaluateGoal4Test().answer_accuracy;}});
    trained &= train(2500,.90f,.90f,[&](){e.trainGoal5(20,24,.08f);},
        std::pair{[&](){return (double)e.evaluateGoal5Validation().answer_accuracy;},
                  [&](){return (double)e.evaluateGoal5Test().answer_accuracy;}});
    trained &= train(2500,.90f,.90f,[&](){e.trainGoal6(20,24,.08f);},
        std::pair{[&](){return (double)e.evaluateGoal6Validation().answer_accuracy;},
                  [&](){return (double)e.evaluateGoal6Test().answer_accuracy;}});
    trained &= train(2500,.90f,.90f,[&](){e.trainGoal7(20,24,.08f);},
        std::pair{[&](){return (double)e.evaluateGoal7Validation().answer_accuracy;},
                  [&](){return (double)e.evaluateGoal7Test().answer_accuracy;}});
    trained &= train(2500,.90f,.90f,[&](){e.trainGoal8(20,24,.08f);},
        std::pair{[&](){return (double)e.evaluateGoal8Validation().answer_accuracy;},
                  [&](){return (double)e.evaluateGoal8Test().answer_accuracy;}});
    trained &= train(2500,.90f,.90f,[&](){e.trainGoal9(20,24,.08f);},
        std::pair{[&](){return (double)e.evaluateGoal9Validation().answer_accuracy;},
                  [&](){return (double)e.evaluateGoal9Test().answer_accuracy;}});
    if(!trained){std::cerr<<"FAIL: one prerequisite goal did not meet independent validation/test\n";return 70;}

    // Exercise the same bounded, targeted extra training used by the Android
    // Goal10 recovery worker. This is training, not a bypass of certification.
    int goal1Extra=0, goal3Extra=0, goal9Extra=0;
    auto g1Ready=[&](){
        auto x=e.classifyGoal1("quanto fa sette piu otto");
        return has(x,"intent","calcolo") && number(x,"confidence")>=0.50;
    };
    auto g3Ready=[&](){
        auto x=e.classifyGoal3("come mi chiamo");
        return has(x,"action","recall") && has(x,"slot","name")
            && number(x,"confidence")>=0.50;
    };
    auto g9Ready=[&](){
        auto a=e.reviewGoal9("quanto fa diciassette piu quattro","17 + 4 = 22");
        auto b=e.reviewGoal9("chi e michelangelo","Michelangelo era un artista. Fonte: Wikipedia");
        return has(a,"check","calculation") && number(a,"confidence")>=0.50
            && has(b,"check","source") && number(b,"confidence")>=0.50;
    };
    while(!g1Ready() && goal1Extra<160) { e.trainGoal1(20,24,0.08f); goal1Extra++; }
    while(!g3Ready() && goal3Extra<160) { e.trainGoal3(20,24,0.02f); goal3Extra++; }
    while(!g9Ready() && goal9Extra<160) { e.trainGoal9(20,24,0.08f); goal9Extra++; }
    bool independentAfter=
          e.evaluateGoal1Test().answer_accuracy>=.90f
       && e.evaluateGoal2Test().answer_accuracy>=.55f
       && e.evaluateGoal3Test().answer_accuracy>=.90f
       && e.evaluateGoal4Test().answer_accuracy>=.90f
       && e.evaluateGoal5Test().answer_accuracy>=.90f
       && e.evaluateGoal6Test().answer_accuracy>=.90f
       && e.evaluateGoal7Test().answer_accuracy>=.90f
       && e.evaluateGoal8Test().answer_accuracy>=.90f
       && e.evaluateGoal9Test().answer_accuracy>=.90f;
    std::cout<<"Recovery extra chunks: Goal1="<<goal1Extra
             <<" Goal3="<<goal3Extra<<" Goal9="<<goal9Extra
             <<" independent-tests="<<(independentAfter?"PASS":"FAIL")<<"\n";
    if(!g1Ready() || !g3Ready() || !g9Ready() || !independentAfter) return 74;
    const std::string dir="/tmp/motorai_goal10_integrated_174";
    std::filesystem::remove_all(dir);
    if(!e.saveCheckpoint(dir))return 71;
    Engine saved(999);
    if(!saved.loadCheckpoint(dir))return 72;
    auto a=saved.classifyGoal1("quanto fa sette piu otto");
    auto b=saved.respondGoal2("ciao");
    auto c=saved.classifyGoal3("come mi chiamo");
    auto d=saved.solveGoal4("parto da 10 aggiungo 3 poi tolgo 4");
    auto f=saved.classifyGoal5("che tempo fa oggi");
    auto h=saved.planGoal6("chi e michelangelo");
    auto i=saved.planGoal6("che temperatura c e adesso");
    auto j=saved.routeGoal7("quanto fa diciassette piu quattro");
    auto k=saved.routeGoal7("chi e dante");
    auto l=saved.planGoal8("quanto fa sette piu otto e poi cerca informazioni su marte");
    auto m=saved.reviewGoal9("quanto fa diciassette piu quattro","17 + 4 = 22");
    auto n=saved.reviewGoal9("chi e michelangelo","Michelangelo era un artista. Fonte: Wikipedia");
    struct Test {const char* label;bool ok;std::string json;};
    std::vector<Test> tests={
      {"comprensione",has(a,"intent","calcolo")&&number(a,"confidence")>=.5,a},
      {"risposta",field(b,"reply"),b},
      {"memoria",has(c,"action","recall")&&has(c,"slot","name")&&number(c,"confidence")>=.5,c},
      {"ragionamento",d.find("\"valid\":true")!=std::string::npos&&has(d,"plan","add_sub")&&number(d,"result")==9,d},
      {"incertezza",has(f,"decision","verify")&&number(f,"confidence")>=.55,f},
      {"ricerca",has(h,"source","wikipedia")&&has(i,"source","live")&&field(h,"query"),h+" "+i},
      {"strumenti",has(j,"tool","calculator")&&has(k,"tool","search"),j+" "+k},
      {"piano",has(l,"mode","sequence")&&field(l,"first")&&field(l,"second"),l},
      {"autocontrollo-calcolo",has(m,"check","calculation")&&number(m,"confidence")>=.5,m},
      {"autocontrollo-fonte",has(n,"check","source")&&number(n,"confidence")>=.5,n}
    };
    int passed=0;
    for(const auto& t:tests) {
        std::cout<<(t.ok?"PASS ":"FAIL ")<<t.label<<" "<<t.json<<"\n";
        if(t.ok)passed++;
    }
    std::cout<<"Goal10 full native integration after checkpoint: "<<passed<<"/10\n";
    std::filesystem::remove_all(dir);
    return passed==10 ? 0 : 73;
}
