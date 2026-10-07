#include <iostream>
#include <iomanip>
#include <filesystem>
#include <string>
#include "motorai_core.h"

static bool run_one(unsigned seed){
    motorai::Engine e(seed);
    const int baseStep=e.globalStep();
    const int baseLevel=e.curriculumLevel();
    const float baseL0=e.evaluateTest().answer_accuracy;

    int stable=0;
    while(e.goal1Step()<2500 && stable<4){
        auto r=e.trainGoal1(20,24,0.08f);
        stable=r.validation.answer_accuracy>=0.90f ? stable+1 : 0;
    }

    auto val=e.evaluateGoal1Validation();
    auto test=e.evaluateGoal1Test();
    if(stable<4 || val.answer_accuracy<0.90f || test.answer_accuracy<0.90f){
        std::cerr<<"FAIL seed="<<seed
                 <<" goal1_step="<<e.goal1Step()
                 <<" val="<<val.answer_accuracy
                 <<" test="<<test.answer_accuracy<<"\n";
        return false;
    }

    // Goal1 is isolated: legacy L0-L5 trajectory must not move.
    if(e.globalStep()!=baseStep || e.curriculumLevel()!=baseLevel ||
       e.evaluateTest().answer_accuracy!=baseL0){
        std::cerr<<"FAIL seed="<<seed<<" isolation regression\n";
        return false;
    }

    std::string dir="/tmp/motorai_goal1_"+std::to_string(seed);
    std::filesystem::remove_all(dir);
    if(!e.saveCheckpoint(dir)) return false;

    motorai::Engine resumed(999);
    if(!resumed.loadCheckpoint(dir)) return false;
    if(resumed.goal1Step()!=e.goal1Step()) return false;
    auto resumedTest=resumed.evaluateGoal1Test();
    if(resumedTest.answer_accuracy<0.90f) return false;

    const std::string probes[]={
        "cerca informazioni su venere",
        "quanto fa nove piu otto",
        "chi e raffaello",
        "apri la calcolatrice",
        "ciao come va oggi"
    };
    const std::string expected[]={"ricerca","calcolo","informazione","azione","saluto"};
    for(int i=0;i<5;++i){
        std::string j=resumed.classifyGoal1(probes[i]);
        if(j.find("\"intent\":\""+expected[i]+"\"")==std::string::npos){
            std::cerr<<"FAIL seed="<<seed<<" probe="<<probes[i]<<" got="<<j<<"\n";
            return false;
        }
    }

    std::cout<<std::fixed<<std::setprecision(4)
             <<"seed="<<seed
             <<" goal1_step="<<e.goal1Step()
             <<" val="<<val.answer_accuracy
             <<" test="<<test.answer_accuracy
             <<" params="<<e.goal1ParameterCount()
             <<" checkpoint_resume=yes\n";
    return true;
}

int main(){
    const unsigned seeds[]={174,175,176};
    for(unsigned s:seeds) if(!run_one(s)) return 20;
    return 0;
}
