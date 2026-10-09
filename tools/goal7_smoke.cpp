#include <iostream>
#include <iomanip>
#include <filesystem>
#include <string>
#include "motorai_core.h"

static bool expect(motorai::Engine&e,const std::string&t,const std::string&tool){
    std::string j=e.routeGoal7(t);
    bool ok=j.find("\"tool\":\""+tool+"\"")!=std::string::npos;
    if(!ok)std::cerr<<"EXPECT FAIL text=["<<t<<"] wanted="<<tool<<" got="<<j<<"\n";
    return ok;
}
static bool run_one(unsigned seed){
    motorai::Engine e(seed);int baseStep=e.globalStep(),baseLevel=e.curriculumLevel(),stable=0;
    while(e.goal7Step()<2500&&stable<4){auto r=e.trainGoal7(20,24,0.08f);stable=r.validation.answer_accuracy>=0.90f?stable+1:0;}
    auto val=e.evaluateGoal7Validation(),test=e.evaluateGoal7Test();
    if(stable<4||val.answer_accuracy<0.90f||test.answer_accuracy<0.90f){
        std::cerr<<"FAIL seed="<<seed<<" goal7_step="<<e.goal7Step()<<" val="<<val.answer_accuracy<<" test="<<test.answer_accuracy<<"\n";return false;}
    if(e.globalStep()!=baseStep||e.curriculumLevel()!=baseLevel)return false;
    if(!expect(e,"ciao motorai","chat"))return false;
    if(!expect(e,"ti ricordi dove vivo","memory"))return false;
    if(!expect(e,"quanto fa 17 piu 4","calculator"))return false;
    if(!expect(e,"chi e michelangelo","search"))return false;
    std::string dir="/tmp/motorai_goal7_"+std::to_string(seed);std::filesystem::remove_all(dir);
    if(!e.saveCheckpoint(dir))return false;motorai::Engine resumed(999);if(!resumed.loadCheckpoint(dir))return false;
    if(resumed.goal7Step()!=e.goal7Step()||resumed.evaluateGoal7Test().answer_accuracy<0.90f)return false;
    if(!expect(resumed,"cerca informazioni su saturno","search"))return false;
    std::cout<<std::fixed<<std::setprecision(4)<<"seed="<<seed<<" goal7_step="<<e.goal7Step()
             <<" val="<<val.answer_accuracy<<" test="<<test.answer_accuracy
             <<" params="<<e.goal7ParameterCount()<<" checkpoint_resume=yes\n";return true;
}
int main(){for(unsigned s:{174u,175u,176u})if(!run_one(s))return 80;return 0;}
