#include <iostream>
#include <iomanip>
#include <filesystem>
#include <string>
#include "motorai_core.h"

static bool expect(motorai::Engine& e,const std::string& text,const std::string& decision){
    std::string j=e.classifyGoal5(text);
    return j.find("\"decision\":\""+decision+"\"")!=std::string::npos;
}

static bool run_one(unsigned seed){
    motorai::Engine e(seed);
    const int baseStep=e.globalStep();
    const int baseLevel=e.curriculumLevel();

    int stable=0;
    while(e.goal5Step()<2500 && stable<4){
        auto r=e.trainGoal5(20,24,0.08f);
        stable=r.validation.answer_accuracy>=0.90f ? stable+1 : 0;
    }

    auto val=e.evaluateGoal5Validation();
    auto test=e.evaluateGoal5Test();
    if(stable<4 || val.answer_accuracy<0.90f || test.answer_accuracy<0.90f){
        std::cerr<<"FAIL seed="<<seed
                 <<" goal5_step="<<e.goal5Step()
                 <<" val="<<val.answer_accuracy
                 <<" test="<<test.answer_accuracy<<"\n";
        return false;
    }

    if(e.globalStep()!=baseStep || e.curriculumLevel()!=baseLevel){
        std::cerr<<"FAIL seed="<<seed<<" foundation isolation regression\n";
        return false;
    }

    if(!expect(e,"ricordati che vivo a verona","local_known")) return false;
    if(!expect(e,"parto da 14 aggiungo 3 poi tolgo 5","local_known")) return false;
    if(!expect(e,"come mi chiamo","local_known")) return false;
    if(!expect(e,"che tempo fa adesso a roma","verify")) return false;
    if(!expect(e,"chi ha vinto la partita di ieri","verify")) return false;
    if(!expect(e,"qual e il prezzo corrente del bitcoin","verify")) return false;

    std::string dir="/tmp/motorai_goal5_"+std::to_string(seed);
    std::filesystem::remove_all(dir);
    if(!e.saveCheckpoint(dir)) return false;

    motorai::Engine resumed(999);
    if(!resumed.loadCheckpoint(dir)) return false;
    if(resumed.goal5Step()!=e.goal5Step()) return false;
    if(resumed.evaluateGoal5Test().answer_accuracy<0.90f) return false;
    if(!expect(resumed,"quali sono le notizie di oggi","verify")) return false;

    std::cout<<std::fixed<<std::setprecision(4)
             <<"seed="<<seed
             <<" goal5_step="<<e.goal5Step()
             <<" val="<<val.answer_accuracy
             <<" test="<<test.answer_accuracy
             <<" params="<<e.goal5ParameterCount()
             <<" checkpoint_resume=yes\n";
    return true;
}

int main(){
    const unsigned seeds[]={174,175,176};
    for(unsigned s:seeds) if(!run_one(s)) return 60;
    return 0;
}
