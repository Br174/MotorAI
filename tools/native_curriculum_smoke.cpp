#include <iostream>
#include <iomanip>
#include <filesystem>
#include <string>
#include "motorai_core.h"

static bool accepted(motorai::Engine& e,int level){
    auto va=e.evaluateValidation();
    auto r0=e.evaluateRetentionL0();
    auto r1=e.evaluateRetentionL1();
    auto r2=e.evaluateRetentionL2();
    auto r3=e.evaluateRetentionL3();
    float required=0.90f;
    return va.answer_accuracy>=required &&
           (level<1 || r0.answer_accuracy>=0.90f) &&
           (level<2 || r1.answer_accuracy>=0.90f) &&
           (level<3 || r2.answer_accuracy>=0.90f) &&
           (level<4 || r3.answer_accuracy>=0.90f);
}

static bool train_until(motorai::Engine& e,int level,int max_extra){
    int stable=0, start=e.globalStep();
    int requiredStable=(level>=4)?4:2;
    while(e.globalStep()-start<max_extra && stable<requiredStable){
        e.train(20,24,0.001f);
        stable=accepted(e,level)?stable+1:0;
    }
    return stable>=requiredStable;
}

static bool save_reload(motorai::Engine& e,unsigned seed,int expected,motorai::Engine& out){
    std::string dir="/tmp/motorai_seed010_"+std::to_string(seed)+"_"+std::to_string(expected);
    std::filesystem::remove_all(dir);
    if(!e.saveCheckpoint(dir) || !out.loadCheckpoint(dir)) return false;
    return out.curriculumLevel()==expected && out.globalStep()==e.globalStep();
}

static bool run_one(unsigned seed){
    motorai::Engine e(seed);

    e.train(220,24,0.001f);
    auto l0=e.evaluateTest();
    if(l0.answer_accuracy<0.90f) return false;

    e.setCurriculum(1);
    e.train(400,24,0.001f);
    auto l1=e.evaluateTest();
    if(l1.answer_accuracy<0.90f || e.evaluateRetentionL0().answer_accuracy<0.90f) return false;

    e.setCurriculum(2);
    if(!train_until(e,2,1800)) return false;
    auto l2=e.evaluateTest();
    if(l2.answer_accuracy<0.90f || e.evaluateRetentionL1().answer_accuracy<0.90f || e.evaluateRetentionL0().answer_accuracy<0.90f) return false;

    e.setCurriculum(3);
    if(!train_until(e,3,1800)) return false;
    auto l3=e.evaluateTest();
    if(l3.answer_accuracy<0.90f || e.evaluateRetentionL2().answer_accuracy<0.90f ||
       e.evaluateRetentionL1().answer_accuracy<0.90f || e.evaluateRetentionL0().answer_accuracy<0.90f) return false;

    motorai::Engine resumed(999);
    if(!save_reload(e,seed,3,resumed)) return false;
    resumed.setCurriculum(4);
    if(!train_until(resumed,4,4800)){
        auto va=resumed.evaluateValidation();
        std::cerr<<"FAIL seed="<<seed<<" stage=L4_validation val="<<va.answer_accuracy
                 <<" r3="<<resumed.evaluateRetentionL3().answer_accuracy
                 <<" r2="<<resumed.evaluateRetentionL2().answer_accuracy
                 <<" r1="<<resumed.evaluateRetentionL1().answer_accuracy
                 <<" r0="<<resumed.evaluateRetentionL0().answer_accuracy
                 <<" step="<<resumed.globalStep()<<"\n";
        return false;
    }

    auto l4=resumed.evaluateTest();
    auto r3=resumed.evaluateRetentionL3();
    auto r2=resumed.evaluateRetentionL2();
    auto r1=resumed.evaluateRetentionL1();
    auto r0=resumed.evaluateRetentionL0();

    std::cout<<std::fixed<<std::setprecision(4)
             <<"seed="<<seed
             <<" l0="<<l0.answer_accuracy
             <<" l1="<<l1.answer_accuracy
             <<" l2="<<l2.answer_accuracy
             <<" l3="<<l3.answer_accuracy
             <<" l4="<<l4.answer_accuracy
             <<" l3ret="<<r3.answer_accuracy
             <<" l2ret="<<r2.answer_accuracy
             <<" l1ret="<<r1.answer_accuracy
             <<" l0ret="<<r0.answer_accuracy
             <<" step="<<resumed.globalStep()
             <<" l4_start="<<resumed.curriculumStartStep()<<"\n";

    return l4.answer_accuracy>=0.90f && r3.answer_accuracy>=0.90f &&
           r2.answer_accuracy>=0.90f && r1.answer_accuracy>=0.90f &&
           r0.answer_accuracy>=0.90f;
}

int main(){
    const unsigned seeds[]={174,175,176};
    for(unsigned s:seeds) if(!run_one(s)) return 10;
    return 0;
}
