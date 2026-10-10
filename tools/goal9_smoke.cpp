#include <iostream>
#include <iomanip>
#include <filesystem>
#include <string>
#include "motorai_core.h"

static bool expect(motorai::Engine&e,const std::string&q,const std::string&a,const std::string&check){
    std::string j=e.reviewGoal9(q,a);
    bool ok=j.find("\"check\":\""+check+"\"")!=std::string::npos;
    if(!ok) std::cerr<<"EXPECT FAIL q=["<<q<<"] a=["<<a<<"] wanted="<<check<<" got="<<j<<"\n";
    return ok;
}

static bool run_one(unsigned seed){
    motorai::Engine e(seed);
    int base=e.globalStep(),level=e.curriculumLevel(),stable=0;

    while(e.goal9Step()<2500&&stable<4){
        auto r=e.trainGoal9(20,24,0.08f);
        stable=r.validation.answer_accuracy>=0.90f?stable+1:0;
    }

    auto val=e.evaluateGoal9Validation(),test=e.evaluateGoal9Test();
    if(stable<4||val.answer_accuracy<0.90f||test.answer_accuracy<0.90f){
        std::cerr<<"FAIL seed="<<seed<<" goal9_step="<<e.goal9Step()
                 <<" val="<<val.answer_accuracy<<" test="<<test.answer_accuracy<<"\n";
        return false;
    }
    if(e.globalStep()!=base||e.curriculumLevel()!=level) return false;

    if(!expect(e,"ciao motorai","ciao sono qui","direct")) return false;
    if(!expect(e,"come mi chiamo","mi hai detto che ti chiami marco","memory")) return false;
    if(!expect(e,"quanto fa diciassette piu quattro","17 + 4 = 21","calculation")) return false;
    if(!expect(e,"chi e michelangelo","Michelangelo... Fonte: Wikipedia","source")) return false;

    std::string dir="/tmp/motorai_goal9_"+std::to_string(seed);
    std::filesystem::remove_all(dir);
    if(!e.saveCheckpoint(dir)) return false;

    motorai::Engine r(999);
    if(!r.loadCheckpoint(dir)||r.goal9Step()!=e.goal9Step()
            ||r.evaluateGoal9Test().answer_accuracy<0.90f) return false;
    if(!expect(r,"qual e la capitale della spagna","Madrid. Fonte: Wikipedia","source")) return false;

    std::cout<<std::fixed<<std::setprecision(4)
             <<"seed="<<seed<<" goal9_step="<<e.goal9Step()
             <<" val="<<val.answer_accuracy<<" test="<<test.answer_accuracy
             <<" params="<<e.goal9ParameterCount()<<" checkpoint_resume=yes\n";
    return true;
}

int main(){
    for(unsigned s:{174u,175u,176u}) if(!run_one(s)) return 100;
    return 0;
}
