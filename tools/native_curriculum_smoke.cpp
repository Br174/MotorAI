#include <iostream>
#include <iomanip>
#include <filesystem>
#include <string>
#include "motorai_core.h"

static bool run_one(unsigned seed) {
    motorai::Engine e(seed);

    e.train(220, 24, 0.001f);
    auto l0 = e.evaluateTest();
    if (l0.answer_accuracy < 0.90f) {
        std::cerr << "FAIL seed=" << seed << " L0=" << l0.answer_accuracy << "\n";
        return false;
    }

    e.setCurriculum(1);
    e.train(400, 24, 0.001f);
    auto l1 = e.evaluateTest();
    auto l0r = e.evaluateRetentionL0();
    if (l1.answer_accuracy < 0.90f || l0r.answer_accuracy < 0.90f) {
        std::cerr << "FAIL seed=" << seed << " L1=" << l1.answer_accuracy
                  << " L0ret=" << l0r.answer_accuracy << "\n";
        return false;
    }

    // Simula davvero il confine APK/checkpoint: salva, crea un nuovo Engine e ricarica.
    std::string dir="/tmp/motorai_seed008_"+std::to_string(seed);
    std::filesystem::remove_all(dir);
    if(!e.saveCheckpoint(dir)) return false;

    motorai::Engine resumed(999);
    if(!resumed.loadCheckpoint(dir)) return false;
    if(resumed.curriculumLevel()!=1 || resumed.globalStep()!=620) return false;
    resumed.setCurriculum(2);

    int stable=0;
    while(resumed.globalStep()<2420 && stable<2) {
        resumed.train(20,24,0.001f);
        auto va=resumed.evaluateValidation();
        auto rr0=resumed.evaluateRetentionL0();
        auto rr1=resumed.evaluateRetentionL1();
        bool ok=va.answer_accuracy>=0.90f &&
                rr0.answer_accuracy>=0.90f &&
                rr1.answer_accuracy>=0.90f;
        stable=ok?stable+1:0;
    }

    auto l2=resumed.evaluateTest();
    auto r0=resumed.evaluateRetentionL0();
    auto r1=resumed.evaluateRetentionL1();
    std::cout<<std::fixed<<std::setprecision(4)
             <<"seed="<<seed
             <<" l0="<<l0.answer_accuracy
             <<" l1="<<l1.answer_accuracy
             <<" l2="<<l2.answer_accuracy
             <<" l1ret="<<r1.answer_accuracy
             <<" l0ret="<<r0.answer_accuracy
             <<" step="<<resumed.globalStep()<<"\n";

    return l2.answer_accuracy>=0.90f &&
           r1.answer_accuracy>=0.90f &&
           r0.answer_accuracy>=0.90f;
}

int main(){
    const unsigned seeds[]={174,175,176};
    for(unsigned s:seeds) if(!run_one(s)) return 10;
    return 0;
}
