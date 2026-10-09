#include <iostream>
#include <iomanip>
#include <filesystem>
#include <string>
#include "motorai_core.h"

template <typename TrainFn, typename ValFn>
static bool train_stable(int max_steps, TrainFn train, ValFn val) {
    int stable=0, steps=0;
    while(steps<max_steps && stable<4){
        train(20);
        steps+=20;
        stable=val()>=0.90f ? stable+1 : 0;
    }
    return stable>=4;
}

static bool has(const std::string& j,const std::string& s){
    return j.find(s)!=std::string::npos;
}

static bool run_one(unsigned seed){
    motorai::Engine e(seed);
    const int base=e.globalStep(), level=e.curriculumLevel();

    if(!train_stable(2500,
        [&](int n){e.trainGoal4(n,24,0.08f);},
        [&](){return e.evaluateGoal4Validation().answer_accuracy;})) return false;
    if(e.evaluateGoal4Test().answer_accuracy<0.90f) return false;

    if(!train_stable(2500,
        [&](int n){e.trainGoal5(n,24,0.08f);},
        [&](){return e.evaluateGoal5Validation().answer_accuracy;})) return false;
    if(e.evaluateGoal5Test().answer_accuracy<0.90f) return false;

    if(!train_stable(2500,
        [&](int n){e.trainGoal6(n,24,0.08f);},
        [&](){return e.evaluateGoal6Validation().answer_accuracy;})) return false;
    if(e.evaluateGoal6Test().answer_accuracy<0.90f) return false;

    if(!train_stable(2500,
        [&](int n){e.trainGoal7(n,24,0.08f);},
        [&](){return e.evaluateGoal7Validation().answer_accuracy;})) return false;
    if(e.evaluateGoal7Test().answer_accuracy<0.90f) return false;

    if(!train_stable(2500,
        [&](int n){e.trainGoal8(n,24,0.08f);},
        [&](){return e.evaluateGoal8Validation().answer_accuracy;})) return false;
    if(e.evaluateGoal8Test().answer_accuracy<0.90f) return false;

    if(!train_stable(2500,
        [&](int n){e.trainGoal9(n,24,0.08f);},
        [&](){return e.evaluateGoal9Validation().answer_accuracy;})) return false;
    if(e.evaluateGoal9Test().answer_accuracy<0.90f) return false;

    if(e.globalStep()!=base || e.curriculumLevel()!=level) return false;

    std::string reasoning=e.solveGoal4("parto da 10 aggiungo 3 poi tolgo 4");
    std::string uncertainty=e.classifyGoal5("che tempo fa oggi");
    std::string wiki=e.planGoal6("chi e michelangelo");
    std::string live=e.planGoal6("che temperatura c e adesso");
    std::string calculator=e.routeGoal7("quanto fa diciassette piu quattro");
    std::string search=e.routeGoal7("chi e dante");
    std::string sequence=e.planGoal8("quanto fa sette piu otto e poi cerca informazioni su marte");
    std::string reviewCalc=e.reviewGoal9("quanto fa diciassette piu quattro","17 + 4 = 22");
    std::string reviewSource=e.reviewGoal9("chi e michelangelo","Michelangelo era un artista. Fonte: Wikipedia");

    bool ok =
        has(reasoning,"\"valid\":true") && has(reasoning,"\"result\":9") &&
        has(uncertainty,"\"decision\":\"verify\"") &&
        has(wiki,"\"source\":\"wikipedia\"") &&
        has(live,"\"source\":\"live\"") &&
        has(calculator,"\"tool\":\"calculator\"") &&
        has(search,"\"tool\":\"search\"") &&
        has(sequence,"\"mode\":\"sequence\"") &&
        has(reviewCalc,"\"check\":\"calculation\"") &&
        has(reviewSource,"\"check\":\"source\"");

    if(!ok){
        std::cerr<<"FAIL seed="<<seed
                 <<"\nreasoning="<<reasoning
                 <<"\nuncertainty="<<uncertainty
                 <<"\nwiki="<<wiki
                 <<"\nlive="<<live
                 <<"\ncalculator="<<calculator
                 <<"\nsearch="<<search
                 <<"\nsequence="<<sequence
                 <<"\nreviewCalc="<<reviewCalc
                 <<"\nreviewSource="<<reviewSource<<"\n";
        return false;
    }

    std::string dir="/tmp/motorai_goal10_"+std::to_string(seed);
    std::filesystem::remove_all(dir);
    if(!e.saveCheckpoint(dir)) return false;
    motorai::Engine resumed(999);
    if(!resumed.loadCheckpoint(dir)) return false;
    if(!has(resumed.routeGoal7("chi e dante"),"\"tool\":\"search\"")) return false;
    if(!has(resumed.reviewGoal9("quanto fa sei per sette","6 x 7 = 41"),
            "\"check\":\"calculation\"")) return false;

    std::cout<<std::fixed<<std::setprecision(4)
             <<"seed="<<seed
             <<" goal10_cycle=yes"
             <<" goal4_test="<<e.evaluateGoal4Test().answer_accuracy
             <<" goal5_test="<<e.evaluateGoal5Test().answer_accuracy
             <<" goal6_test="<<e.evaluateGoal6Test().answer_accuracy
             <<" goal7_test="<<e.evaluateGoal7Test().answer_accuracy
             <<" goal8_test="<<e.evaluateGoal8Test().answer_accuracy
             <<" goal9_test="<<e.evaluateGoal9Test().answer_accuracy
             <<" checkpoint_resume=yes\n";
    return true;
}

int main(){
    for(unsigned s:{174u,175u,176u}) if(!run_one(s)) return 110;
    return 0;
}
