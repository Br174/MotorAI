#include <iostream>
#include <iomanip>
#include <filesystem>
#include <string>
#include "motorai_core.h"

static bool expect(motorai::Engine& e,const std::string& text,const std::string& source,const std::string& queryPart){
    std::string j=e.planGoal6(text);
    return j.find("\"source\":\""+source+"\"")!=std::string::npos
        && j.find(queryPart)!=std::string::npos;
}

static bool run_one(unsigned seed){
    motorai::Engine e(seed);
    const int baseStep=e.globalStep();
    const int baseLevel=e.curriculumLevel();

    int stable=0;
    while(e.goal6Step()<2500 && stable<4){
        auto r=e.trainGoal6(20,24,0.08f);
        stable=r.validation.answer_accuracy>=0.90f ? stable+1 : 0;
    }

    auto val=e.evaluateGoal6Validation();
    auto test=e.evaluateGoal6Test();
    if(stable<4 || val.answer_accuracy<0.90f || test.answer_accuracy<0.90f){
        std::cerr<<"FAIL seed="<<seed
                 <<" goal6_step="<<e.goal6Step()
                 <<" val="<<val.answer_accuracy
                 <<" test="<<test.answer_accuracy<<"\n";
        return false;
    }

    if(e.globalStep()!=baseStep || e.curriculumLevel()!=baseLevel){
        std::cerr<<"FAIL seed="<<seed<<" foundation isolation regression\n";
        return false;
    }

    if(!expect(e,"chi e michelangelo","wikipedia","michelangelo")) return false;
    if(!expect(e,"cerca informazioni su saturno","wikipedia","saturno")) return false;
    if(!expect(e,"qual e la capitale della grecia","wikipedia","grecia")) return false;
    if(!expect(e,"che tempo fa oggi a roma","live","tempo")) return false;
    if(!expect(e,"prezzo corrente del bitcoin","live","bitcoin")) return false;
    if(!expect(e,"notizie di oggi sulla scienza","live","notizie")) return false;

    std::string dir="/tmp/motorai_goal6_"+std::to_string(seed);
    std::filesystem::remove_all(dir);
    if(!e.saveCheckpoint(dir)) return false;

    motorai::Engine resumed(999);
    if(!resumed.loadCheckpoint(dir)) return false;
    if(resumed.goal6Step()!=e.goal6Step()) return false;
    if(resumed.evaluateGoal6Test().answer_accuracy<0.90f) return false;
    if(!expect(resumed,"parlami di raffaello","wikipedia","raffaello")) return false;

    std::cout<<std::fixed<<std::setprecision(4)
             <<"seed="<<seed
             <<" goal6_step="<<e.goal6Step()
             <<" val="<<val.answer_accuracy
             <<" test="<<test.answer_accuracy
             <<" params="<<e.goal6ParameterCount()
             <<" checkpoint_resume=yes\n";
    return true;
}

int main(){
    const unsigned seeds[]={174,175,176};
    for(unsigned s:seeds) if(!run_one(s)) return 70;
    return 0;
}
