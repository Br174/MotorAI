#include <iostream>
#include <iomanip>
#include <filesystem>
#include <string>
#include "motorai_core.h"

static bool containsAny(const std::string& s, std::initializer_list<const char*> needles){
    for(const char* n:needles) if(s.find(n)!=std::string::npos) return true;
    return false;
}

static bool run_one(unsigned seed){
    motorai::Engine e(seed);

    int goal1Stable=0;
    while(e.goal1Step()<2500 && goal1Stable<4){
        auto r=e.trainGoal1(20,24,0.08f);
        goal1Stable=r.validation.answer_accuracy>=0.90f ? goal1Stable+1 : 0;
    }
    if(goal1Stable<4 || e.evaluateGoal1Test().answer_accuracy<0.90f){
        std::cerr<<"FAIL seed="<<seed<<" prerequisite Goal1\n";
        return false;
    }

    const int baseStep=e.globalStep();
    const int baseLevel=e.curriculumLevel();

    int stable=0;
    while(e.goal2Step()<3000 && stable<4){
        auto r=e.trainGoal2(20,16,0.12f);
        stable=r.validation.answer_accuracy>=0.60f ? stable+1 : 0;
    }

    auto val=e.evaluateGoal2Validation();
    auto test=e.evaluateGoal2Test();
    if(stable<4 || val.answer_accuracy<0.60f || test.answer_accuracy<0.55f){
        std::cerr<<"FAIL seed="<<seed
                 <<" goal2_step="<<e.goal2Step()
                 <<" val="<<val.answer_accuracy
                 <<" test="<<test.answer_accuracy<<"\n";
        return false;
    }

    if(e.globalStep()!=baseStep || e.curriculumLevel()!=baseLevel){
        std::cerr<<"FAIL seed="<<seed<<" foundation isolation regression\n";
        return false;
    }

    const std::string prompts[]={
        "ciao come va",
        "chi e raffaello",
        "quanto fa nove piu otto",
        "cerca informazioni su venere",
        "apri la calcolatrice"
    };
    for(const auto& p:prompts){
        std::string j=e.respondGoal2(p);
        if(j.find("\"reply\":\"\"")!=std::string::npos || j.size()<24){
            std::cerr<<"FAIL seed="<<seed<<" empty learned reply for "<<p<<" -> "<<j<<"\n";
            return false;
        }
    }

    std::string greet=e.respondGoal2("ciao come va");
    std::string calc=e.respondGoal2("quanto fa nove piu otto");
    std::string search=e.respondGoal2("cerca informazioni su venere");
    std::string action=e.respondGoal2("apri la calcolatrice");
    if(!containsAny(greet,{"ciao","salve"})) return false;
    if(!containsAny(calc,{"calcol","risultato"})) return false;
    if(!containsAny(search,{"cerc","ricerca","informazioni"})) return false;
    if(!containsAny(action,{"azione","fare","farlo","eseg"})) return false;

    std::string dir="/tmp/motorai_goal2_"+std::to_string(seed);
    std::filesystem::remove_all(dir);
    if(!e.saveCheckpoint(dir)) return false;

    motorai::Engine resumed(999);
    if(!resumed.loadCheckpoint(dir)) return false;
    if(resumed.goal2Step()!=e.goal2Step()) return false;
    if(resumed.evaluateGoal2Test().answer_accuracy<0.55f) return false;

    std::cout<<std::fixed<<std::setprecision(4)
             <<"seed="<<seed
             <<" goal2_step="<<e.goal2Step()
             <<" val="<<val.answer_accuracy
             <<" test="<<test.answer_accuracy
             <<" params="<<e.goal2ParameterCount()
             <<" reply="<<resumed.respondGoal2("ciao come va")
             <<" checkpoint_resume=yes\n";
    return true;
}

int main(){
    const unsigned seeds[]={174,175,176};
    for(unsigned s:seeds) if(!run_one(s)) return 30;
    return 0;
}
