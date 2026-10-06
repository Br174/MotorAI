#include <iostream>
#include <iomanip>
#include <filesystem>
#include <string>
#include "motorai_core.h"

static bool accepted(motorai::Engine& e, int level) {
    auto va=e.evaluateValidation();
    auto r0=e.evaluateRetentionL0();
    auto r1=e.evaluateRetentionL1();
    auto r2=e.evaluateRetentionL2();
    return va.answer_accuracy>=0.90f &&
           (level<1 || r0.answer_accuracy>=0.90f) &&
           (level<2 || r1.answer_accuracy>=0.90f) &&
           (level<3 || r2.answer_accuracy>=0.90f);
}

static bool train_until(motorai::Engine& e,int level,int max_extra_steps){
    int stable=0;
    int start=e.globalStep();
    while(e.globalStep()-start<max_extra_steps && stable<2){
        e.train(20,24,0.001f);
        stable=accepted(e,level)?stable+1:0;
    }
    return stable>=2;
}

static bool save_reload(motorai::Engine& e,unsigned seed,int expected_level,motorai::Engine& out){
    std::string dir="/tmp/motorai_seed009_"+std::to_string(seed)+"_"+std::to_string(expected_level);
    std::filesystem::remove_all(dir);
    if(!e.saveCheckpoint(dir)) return false;
    if(!out.loadCheckpoint(dir)) return false;
    return out.curriculumLevel()==expected_level &&
           out.globalStep()==e.globalStep();
}

static bool run_one(unsigned seed){
    // Ricostruisce esattamente il percorso già verificato fino a Seed008.
    motorai::Engine e(seed);

    e.train(220,24,0.001f);
    auto l0=e.evaluateTest();
    if(l0.answer_accuracy<0.90f){
        std::cerr<<"FAIL seed="<<seed<<" stage=L0 test="<<l0.answer_accuracy<<" step="<<e.globalStep()<<"\n";
        return false;
    }

    e.setCurriculum(1);
    e.train(400,24,0.001f);
    auto l1=e.evaluateTest();
    auto l0r1=e.evaluateRetentionL0();
    if(l1.answer_accuracy<0.90f || l0r1.answer_accuracy<0.90f){
        std::cerr<<"FAIL seed="<<seed<<" stage=L1 test="<<l1.answer_accuracy<<" r0="<<l0r1.answer_accuracy<<" step="<<e.globalStep()<<"\n";
        return false;
    }

    e.setCurriculum(2);
    if(!train_until(e,2,1800)){
        auto va=e.evaluateValidation(); auto r1=e.evaluateRetentionL1(); auto r0=e.evaluateRetentionL0();
        std::cerr<<"FAIL seed="<<seed<<" stage=L2_validation val="<<va.answer_accuracy<<" r1="<<r1.answer_accuracy<<" r0="<<r0.answer_accuracy<<" step="<<e.globalStep()<<"\n";
        return false;
    }
    auto l2=e.evaluateTest();
    auto l1r=e.evaluateRetentionL1();
    auto l0r=e.evaluateRetentionL0();
    if(l2.answer_accuracy<0.90f || l1r.answer_accuracy<0.90f || l0r.answer_accuracy<0.90f){
        std::cerr<<"FAIL seed="<<seed<<" stage=L2_test test="<<l2.answer_accuracy<<" r1="<<l1r.answer_accuracy<<" r0="<<l0r.answer_accuracy<<" step="<<e.globalStep()<<"\n";
        return false;
    }

    // Simula aggiornamento app/checkpoint prima del nuovo Livello 3.
    motorai::Engine resumed(999);
    if(!save_reload(e,seed,2,resumed)){
        std::cerr<<"FAIL seed="<<seed<<" stage=L2_checkpoint_reload\n";
        return false;
    }
    resumed.setCurriculum(3);

    if(!train_until(resumed,3,1800)){
        auto va=resumed.evaluateValidation(); auto r2=resumed.evaluateRetentionL2();
        auto r1=resumed.evaluateRetentionL1(); auto r0=resumed.evaluateRetentionL0();
        std::cerr<<"FAIL seed="<<seed<<" stage=L3_validation val="<<va.answer_accuracy
                 <<" r2="<<r2.answer_accuracy<<" r1="<<r1.answer_accuracy<<" r0="<<r0.answer_accuracy
                 <<" step="<<resumed.globalStep()<<"\n";
        return false;
    }

    auto l3=resumed.evaluateTest();
    auto r2=resumed.evaluateRetentionL2();
    auto r1=resumed.evaluateRetentionL1();
    auto r0=resumed.evaluateRetentionL0();

    std::cout<<std::fixed<<std::setprecision(4)
             <<"seed="<<seed
             <<" l0="<<l0.answer_accuracy
             <<" l1="<<l1.answer_accuracy
             <<" l2="<<l2.answer_accuracy
             <<" l3="<<l3.answer_accuracy
             <<" l2ret="<<r2.answer_accuracy
             <<" l1ret="<<r1.answer_accuracy
             <<" l0ret="<<r0.answer_accuracy
             <<" step="<<resumed.globalStep()
             <<" l3_start="<<resumed.curriculumStartStep()<<"\n";

    return l3.answer_accuracy>=0.90f &&
           r2.answer_accuracy>=0.90f &&
           r1.answer_accuracy>=0.90f &&
           r0.answer_accuracy>=0.90f;
}

int main(){
    const unsigned seeds[]={174,175,176};
    for(unsigned s:seeds) if(!run_one(s)) return 10;
    return 0;
}
