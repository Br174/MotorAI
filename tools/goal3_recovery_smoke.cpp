#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include "motorai_core.h"

// Simulate the Android chunk-save / severe-regression rollback / adaptive-retry
// path using real native Goal3 training and exact checkpoint serialization.
static bool check(unsigned seed) {
    const float rates[]={0.04f,0.02f,0.01f,0.005f,0.0025f};
    motorai::Engine e(seed);
    const int foundationStep=e.globalStep();
    const int foundationLevel=e.curriculumLevel();
    const std::string dir="/tmp/motorai_goal3_adaptive_"+std::to_string(seed);
    int retries=0, stable=0, chunks=0;
    std::filesystem::remove_all(dir);
    while (e.goal3Step()<2500 && stable<4 && retries<5) {
        float before=e.evaluateGoal3Validation().answer_accuracy;
        if(!e.saveCheckpoint(dir)) return false;
        auto r=e.trainGoal3(20,24,rates[retries]);
        float after=r.validation.answer_accuracy;
        ++chunks;
        if(!std::isfinite(after) || after+0.20f<before) {
            if(!e.loadCheckpoint(dir)) return false;
            ++retries;
            stable=0;
            continue;
        }
        stable=after>=0.90f ? stable+1 : 0;
    }
    auto validation=e.evaluateGoal3Validation();
    auto test=e.evaluateGoal3Test();
    bool ok=stable>=4 && validation.answer_accuracy>=0.90f
            && test.answer_accuracy>=0.90f && retries<5
            && e.globalStep()==foundationStep && e.curriculumLevel()==foundationLevel;
    if(ok) {
        if(!e.saveCheckpoint(dir)) ok=false;
        motorai::Engine restored(999);
        if(!restored.loadCheckpoint(dir)) ok=false;
        else if(restored.goal3Step()!=e.goal3Step()
             || restored.evaluateGoal3Test().answer_accuracy<0.90f) ok=false;
    }
    std::filesystem::remove_all(dir);
    std::cout<<"seed="<<seed<<" goal3_steps="<<e.goal3Step()
             <<" retries="<<retries<<" chunks="<<chunks
             <<" validation="<<validation.answer_accuracy
             <<" test="<<test.answer_accuracy<<" result="<<(ok?"PASS":"FAIL")<<"\n";
    return ok;
}

int main(){
    for(unsigned seed:{174u,175u,176u}) if(!check(seed)) return 41;
    return 0;
}
