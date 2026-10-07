#include <iostream>
#include <iomanip>
#include <filesystem>
#include <string>
#include "motorai_core.h"

static bool expect(motorai::Engine& e,const std::string& text,const std::string& action,const std::string& slot){
    std::string j=e.classifyGoal3(text);
    return j.find("\"action\":\""+action+"\"")!=std::string::npos
        && j.find("\"slot\":\""+slot+"\"")!=std::string::npos;
}

static bool run_one(unsigned seed){
    motorai::Engine e(seed);
    const int baseStep=e.globalStep();
    const int baseLevel=e.curriculumLevel();

    int stable=0;
    while(e.goal3Step()<2500 && stable<4){
        auto r=e.trainGoal3(20,24,0.08f);
        stable=r.validation.answer_accuracy>=0.90f ? stable+1 : 0;
    }

    auto val=e.evaluateGoal3Validation();
    auto test=e.evaluateGoal3Test();
    if(stable<4 || val.answer_accuracy<0.90f || test.answer_accuracy<0.90f){
        std::cerr<<"FAIL seed="<<seed
                 <<" goal3_step="<<e.goal3Step()
                 <<" val="<<val.answer_accuracy
                 <<" test="<<test.answer_accuracy<<"\n";
        return false;
    }

    if(e.globalStep()!=baseStep || e.curriculumLevel()!=baseLevel){
        std::cerr<<"FAIL seed="<<seed<<" foundation isolation regression\n";
        return false;
    }

    if(!expect(e,"ricordati che mi chiamo stefano","store","name")) return false;
    if(!expect(e,"come avevo detto di chiamarmi","recall","name")) return false;
    if(!expect(e,"salva che vivo a verona","store","city")) return false;
    if(!expect(e,"ti ricordi dove abito","recall","city")) return false;
    if(!expect(e,"il colore che preferisco e turchese","store","color")) return false;
    if(!expect(e,"che colore ricordi di me","recall","color")) return false;
    if(!expect(e,"ho un cane chiamato teo","store","pet")) return false;
    if(!expect(e,"che animale avevo detto di avere","recall","pet")) return false;
    if(!expect(e,"quanto fa sette piu otto","none","none")) return false;

    std::string dir="/tmp/motorai_goal3_"+std::to_string(seed);
    std::filesystem::remove_all(dir);
    if(!e.saveCheckpoint(dir)) return false;

    motorai::Engine resumed(999);
    if(!resumed.loadCheckpoint(dir)) return false;
    if(resumed.goal3Step()!=e.goal3Step()) return false;
    if(resumed.evaluateGoal3Test().answer_accuracy<0.90f) return false;
    if(!expect(resumed,"qual era il mio nome","recall","name")) return false;

    std::cout<<std::fixed<<std::setprecision(4)
             <<"seed="<<seed
             <<" goal3_step="<<e.goal3Step()
             <<" val="<<val.answer_accuracy
             <<" test="<<test.answer_accuracy
             <<" params="<<e.goal3ParameterCount()
             <<" checkpoint_resume=yes\n";
    return true;
}

int main(){
    const unsigned seeds[]={174,175,176};
    for(unsigned s:seeds) if(!run_one(s)) return 40;
    return 0;
}
