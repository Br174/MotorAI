#include <iostream>
#include <iomanip>
#include <filesystem>
#include <string>
#include "motorai_core.h"

static bool expectSolve(motorai::Engine& e,const std::string& text,const std::string& plan,long long result){
    std::string j=e.solveGoal4(text);
    return j.find("\"valid\":true")!=std::string::npos
        && j.find("\"plan\":\""+plan+"\"")!=std::string::npos
        && j.find("\"result\":"+std::to_string(result))!=std::string::npos;
}

static bool run_one(unsigned seed){
    motorai::Engine e(seed);
    const int baseStep=e.globalStep();
    const int baseLevel=e.curriculumLevel();

    int stable=0;
    while(e.goal4Step()<2500 && stable<4){
        auto r=e.trainGoal4(20,24,0.08f);
        stable=r.validation.answer_accuracy>=0.90f ? stable+1 : 0;
    }

    auto val=e.evaluateGoal4Validation();
    auto test=e.evaluateGoal4Test();
    if(stable<4 || val.answer_accuracy<0.90f || test.answer_accuracy<0.90f){
        std::cerr<<"FAIL seed="<<seed
                 <<" goal4_step="<<e.goal4Step()
                 <<" val="<<val.answer_accuracy
                 <<" test="<<test.answer_accuracy<<"\n";
        return false;
    }

    if(e.globalStep()!=baseStep || e.curriculumLevel()!=baseLevel){
        std::cerr<<"FAIL seed="<<seed<<" foundation isolation regression\n";
        return false;
    }

    if(!expectSolve(e,"parto da 23 aggiungo 7 poi tolgo 5","add_sub",25)) return false;
    if(!expectSolve(e,"parto da venti tolgo tre poi aggiungo due","sub_add",19)) return false;
    if(!expectSolve(e,"parto da 6 moltiplico per 4 poi aggiungo 3","mul_add",27)) return false;
    if(!expectSolve(e,"parto da nove aggiungo due poi moltiplico tutto per tre","add_mul",33)) return false;

    std::string dir="/tmp/motorai_goal4_"+std::to_string(seed);
    std::filesystem::remove_all(dir);
    if(!e.saveCheckpoint(dir)) return false;

    motorai::Engine resumed(999);
    if(!resumed.loadCheckpoint(dir)) return false;
    if(resumed.goal4Step()!=e.goal4Step()) return false;
    if(resumed.evaluateGoal4Test().answer_accuracy<0.90f) return false;
    if(!expectSolve(resumed,"parto da 18 tolgo 4 poi tolgo 3","sub_sub",11)) return false;

    std::cout<<std::fixed<<std::setprecision(4)
             <<"seed="<<seed
             <<" goal4_step="<<e.goal4Step()
             <<" val="<<val.answer_accuracy
             <<" test="<<test.answer_accuracy
             <<" params="<<e.goal4ParameterCount()
             <<" checkpoint_resume=yes\n";
    return true;
}

int main(){
    const unsigned seeds[]={174,175,176};
    for(unsigned s:seeds) if(!run_one(s)) return 50;
    return 0;
}
