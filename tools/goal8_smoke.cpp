#include <iostream>
#include <iomanip>
#include <filesystem>
#include <string>
#include "motorai_core.h"

static bool expect(motorai::Engine&e,const std::string&t,const std::string&mode,const std::string&part){
    std::string j=e.planGoal8(t);bool ok=j.find("\"mode\":\""+mode+"\"")!=std::string::npos&&j.find(part)!=std::string::npos;
    if(!ok)std::cerr<<"EXPECT FAIL "<<t<<" -> "<<j<<"\n";return ok;
}
static bool run_one(unsigned seed){
    motorai::Engine e(seed);int base=e.globalStep(),level=e.curriculumLevel(),stable=0;
    while(e.goal8Step()<2500&&stable<4){auto r=e.trainGoal8(20,24,0.08f);stable=r.validation.answer_accuracy>=0.90f?stable+1:0;}
    auto val=e.evaluateGoal8Validation(),test=e.evaluateGoal8Test();
    if(stable<4||val.answer_accuracy<0.90f||test.answer_accuracy<0.90f){std::cerr<<"FAIL seed="<<seed<<" goal8_step="<<e.goal8Step()<<" val="<<val.answer_accuracy<<" test="<<test.answer_accuracy<<"\n";return false;}
    if(e.globalStep()!=base||e.curriculumLevel()!=level)return false;
    if(!expect(e,"ciao","single","ciao"))return false;
    if(!expect(e,"ricorda che vivo a roma e poi dimmi dove vivo","sequence","dimmi dove vivo"))return false;
    if(!expect(e,"quanto fa sette piu otto e poi cerca informazioni su marte","sequence","cerca informazioni su marte"))return false;
    std::string dir="/tmp/motorai_goal8_"+std::to_string(seed);std::filesystem::remove_all(dir);if(!e.saveCheckpoint(dir))return false;
    motorai::Engine r(999);if(!r.loadCheckpoint(dir)||r.goal8Step()!=e.goal8Step()||r.evaluateGoal8Test().answer_accuracy<0.90f)return false;
    if(!expect(r,"cerca informazioni su dante e dopo cerca informazioni su galileo","sequence","galileo"))return false;
    std::cout<<std::fixed<<std::setprecision(4)<<"seed="<<seed<<" goal8_step="<<e.goal8Step()<<" val="<<val.answer_accuracy<<" test="<<test.answer_accuracy<<" params="<<e.goal8ParameterCount()<<" checkpoint_resume=yes\n";return true;
}
int main(){for(unsigned s:{174u,175u,176u})if(!run_one(s))return 90;return 0;}
