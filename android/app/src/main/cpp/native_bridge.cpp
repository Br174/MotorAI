#include <jni.h>
#include <mutex>
#include <string>
#include <sstream>
#include <initializer_list>
#include <cmath>
#include <chrono>
#include "motorai_core.h"

namespace {
motorai::Engine g_engine(174);
std::mutex g_engine_call_mu;

std::string toString(JNIEnv* env, jstring s) {
    if (!s) return {};
    const char* p = env->GetStringUTFChars(s, nullptr);
    std::string out = p ? p : "";
    if (p) env->ReleaseStringUTFChars(s, p);
    return out;
}

jstring js(JNIEnv* env, const std::string& s) {
    return env->NewStringUTF(s.c_str());
}
}


/** Read-only checkpoint validation in a separate Engine. Never loads into g_engine. */
extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeProbeCheckpoint(JNIEnv* env, jclass, jstring path) {
    try {
        std::lock_guard<std::mutex> guard(g_engine_call_mu);
        const int liveStep=g_engine.globalStep(), liveLevel=g_engine.curriculumLevel();
        motorai::Engine isolated(174);
        const bool ok=isolated.loadCheckpoint(toString(env,path));
        const bool unchanged=liveStep==g_engine.globalStep() && liveLevel==g_engine.curriculumLevel();
        if(!ok||!unchanged)
            return js(env,std::string("{\"loaded\":false,\"live_unchanged\":")
                +(unchanged?"true":"false")+"}");
        auto test=isolated.evaluateTest();
        auto retained=isolated.evaluateRetentionL3();
        if(!std::isfinite(test.answer_accuracy)||!std::isfinite(retained.answer_accuracy))
            return js(env,"{\"loaded\":false,\"live_unchanged\":true}");
        return js(env,std::string("{\"loaded\":true,\"live_unchanged\":true,\"level\":")
            +std::to_string(isolated.curriculumLevel())
            +",\"step\":"+std::to_string(isolated.globalStep())
            +",\"test_accuracy\":"+std::to_string(test.answer_accuracy)
            +",\"retention_l3_accuracy\":"+std::to_string(retained.answer_accuracy)
            +",\"goal1_step\":"+std::to_string(isolated.goal1Step())+"}");
    }catch(...){return js(env,"{\"loaded\":false,\"live_unchanged\":true}");}
}


/** Stage B: Goal1 shadow fine-tuning. The trained candidate stays off-line in a
 * separate Engine and directory; g_engine and current checkpoint are untouched.
 * Only regular TRAIN examples are used. Independent validation/test never train.
 */
extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal1ShadowTrial(JNIEnv* env, jclass,
                                                          jstring basePath, jstring candidatePath) {
    try {
        std::lock_guard<std::mutex> guard(g_engine_call_mu);
        const int liveStep=g_engine.globalStep(), liveGoal1=g_engine.goal1Step();
        const std::string base=toString(env,basePath);
        const std::string dest=toString(env,candidatePath);
        if(base.empty()||dest.empty()||base==dest)
            return js(env,"{\"status\":\"invalid_path\"}");
        motorai::Engine shadow(174);
        if(!shadow.loadCheckpoint(base))return js(env,"{\"status\":\"checkpoint_unreadable\"}");
        const auto baselineVal=shadow.evaluateGoal1Validation();
        const auto baselineTest=shadow.evaluateGoal1Test();
        const auto origin=shadow.classifyGoal1("quanto fa sette piu otto");
        auto confidence=[](const std::string& json)->double {
            const std::string token="\"confidence\":";
            auto p=json.find(token);
            if(p==std::string::npos)return -1;
            try {return std::stod(json.substr(p+token.size()));}
            catch (...) {return -1;}
        };
        auto isCalc=[](const std::string& json)->bool {
            return json.find("\"intent\":\"calcolo\"")!=std::string::npos;
        };
        const double before=confidence(origin);
        const double liveConfidence=confidence(g_engine.classifyGoal1("quanto fa sette piu otto"));
        if(!std::isfinite(before)||std::abs(before-liveConfidence)>0.00005
           ||shadow.goal1Step()!=liveGoal1)
            return js(env,"{\"status\":\"source_out_of_sync\"}");
        double after=before, test=baselineTest.answer_accuracy;
        double validation=baselineVal.answer_accuracy;
        double validationLoss=baselineVal.loss;
        const double initialLoss=baselineVal.loss;
        int steps=0;
        bool ready=false;
        double bestLoss=initialLoss;
        int lossStagnant=0;
        int lossDegrades=0;
        std::string stopReason="step_limit";
        const auto start=std::chrono::steady_clock::now();
        // Validation accuracy may remain unchanged while predictive confidence
        // keeps improving; do NOT stop merely because accuracy is at 95%.
        // The final held-out TEST and integration request are never in this loop.
        for(int n=0;n<150;++n){
            const double elapsed=std::chrono::duration<double>(
                std::chrono::steady_clock::now()-start).count();
            if(elapsed>=12.0){stopReason="time_limit";break;}
            shadow.trainGoal1(20,24,0.02f);
            steps+=20;
            auto metrics=shadow.evaluateGoal1Validation();
            validation=metrics.answer_accuracy;
            validationLoss=metrics.loss;
            if(!std::isfinite(validationLoss) || !std::isfinite(validation)){
                stopReason="nonfinite_validation";break;
            }
            if(validation+0.00001<baselineVal.answer_accuracy){
                stopReason="validation_accuracy_drop";break;
            }
            if(validationLoss+0.00001<bestLoss){
                bestLoss=validationLoss;lossStagnant=0;lossDegrades=0;
            }else{
                ++lossStagnant;
                if(validationLoss>bestLoss+0.01)++lossDegrades;
                else lossDegrades=0;
            }
            if(lossDegrades>=5){stopReason="validation_loss_regression";break;}
            if(lossStagnant>=36 && steps>=720){
                stopReason="validation_loss_plateau";break;
            }
        }
        // Only now perform an independent final-test evaluation and
        // final integration prompt. Neither was used to tune training steps.
        const auto actual=shadow.classifyGoal1("quanto fa sette piu otto");
        after=confidence(actual);
        test=shadow.evaluateGoal1Test().answer_accuracy;
        const bool retained=std::isfinite(after)
                         && validation+0.00001>=baselineVal.answer_accuracy
                         && test+0.00001>=baselineTest.answer_accuracy
                         && validation>=.90 && test>=.90
                         && validationLoss<=initialLoss+0.01;
        const bool generalized=isCalc(shadow.classifyGoal1("quanto fa undici piu tre"))
                      && isCalc(shadow.classifyGoal1("calcola quattordici piu sei"))
                      && shadow.classifyGoal1("ciao").find("\"intent\":\"saluto\"")!=std::string::npos;
        ready=retained&&generalized&&isCalc(actual)&&after>=.50;
        const bool unchanged=liveStep==g_engine.globalStep() &&
                             liveGoal1==g_engine.goal1Step();
        if(!unchanged)return js(env,"{\"status\":\"live_changed\"}");
        if(ready&&!shadow.saveCheckpoint(dest))
            return js(env,"{\"status\":\"candidate_save_failed\"}");
        const std::string state=ready?"candidate_ready":"insufficient_confidence";
        return js(env,std::string("{\"status\":\"")+state
              +"\",\"live_unchanged\":true,\"candidate_ready\":"
              +(ready?"true":"false")
              +",\"confidence_before\":"+std::to_string(before)
              +",\"confidence_after\":"+std::to_string(after)
              +",\"stopped_by\":\""+stopReason+"\""
              +",\"validation_loss_before\":"+std::to_string(initialLoss)
              +",\"validation_loss_after\":"+std::to_string(validationLoss)
              +",\"validation_before\":"+std::to_string(baselineVal.answer_accuracy)
              +",\"validation_after\":"+std::to_string(validation)
              +",\"test_before\":"+std::to_string(baselineTest.answer_accuracy)
              +",\"test_after\":"+std::to_string(test)
              +",\"shadow_train_steps\":"+std::to_string(steps)+"}");
    }catch(...){return js(env,"{\"status\":\"shadow_exception\"}");}
}


/** Stage C: read-only integrated Goal10 preview for saved shadow candidate.
 * Both profiles are evaluated under the same mutex, with no train/save/load
 * operation on g_engine, no mutation of Goal10 progress and no promotion.
 */
static std::string goal10RawProfile(motorai::Engine& engine) {
    return std::string("{")
      +"\"comprensione\":"+engine.classifyGoal1("quanto fa sette piu otto")
      +",\"risposta\":"+engine.respondGoal2("ciao")
      +",\"memoria\":"+engine.classifyGoal3("come mi chiamo")
      +",\"ragionamento\":"+engine.solveGoal4("parto da 10 aggiungo 3 poi tolgo 4")
      +",\"incertezza\":"+engine.classifyGoal5("che tempo fa oggi")
      +",\"ricerca_wiki\":"+engine.planGoal6("chi e michelangelo")
      +",\"ricerca_live\":"+engine.planGoal6("che temperatura c e adesso")
      +",\"strumenti_calc\":"+engine.routeGoal7("quanto fa diciassette piu quattro")
      +",\"strumenti_search\":"+engine.routeGoal7("chi e dante")
      +",\"piano\":"+engine.planGoal8("quanto fa sette piu otto e poi cerca informazioni su marte")
      +",\"autocontrollo_calcolo\":"+engine.reviewGoal9("quanto fa diciassette piu quattro","17 + 4 = 22")
      +",\"autocontrollo_fonte\":"+engine.reviewGoal9("chi e michelangelo","Michelangelo era un artista. Fonte: Wikipedia")
      +"}";
}

extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal10CandidateCompare(JNIEnv* env, jclass,
                                                                jstring candidatePath) {
    try {
        std::lock_guard<std::mutex> guard(g_engine_call_mu);
        const int liveStep=g_engine.globalStep();
        const int level=g_engine.curriculumLevel();
        const int goal1Step=g_engine.goal1Step();
        motorai::Engine candidate(174);
        if(!candidate.loadCheckpoint(toString(env,candidatePath)))
            return js(env,"{\"loaded\":false,\"reason\":\"candidate_unreadable\"}");
        if(candidate.globalStep()!=liveStep || candidate.curriculumLevel()!=level)
            return js(env,"{\"loaded\":false,\"reason\":\"base_model_mismatch\"}");
        if(candidate.goal1Step()<=goal1Step)
            return js(env,"{\"loaded\":false,\"reason\":\"not_finetuned\"}");
        const auto baseline=goal10RawProfile(g_engine);
        const auto proposed=goal10RawProfile(candidate);
        const auto activeGoal1Test=g_engine.evaluateGoal1Test();
        const auto trialGoal1Test=candidate.evaluateGoal1Test();
        const auto activeGoal1Val=g_engine.evaluateGoal1Validation();
        const auto trialGoal1Val=candidate.evaluateGoal1Validation();
        if(liveStep!=g_engine.globalStep() || level!=g_engine.curriculumLevel()
           || goal1Step!=g_engine.goal1Step())
            return js(env,"{\"loaded\":false,\"reason\":\"live_engine_changed\"}");
        if(!std::isfinite(trialGoal1Test.answer_accuracy) ||
           !std::isfinite(trialGoal1Val.answer_accuracy))
            return js(env,"{\"loaded\":false,\"reason\":\"nonfinite_test\"}");
        return js(env,std::string("{\"loaded\":true,\"active\":")+baseline+
           ",\"candidate\":"+proposed+
           ",\"active_goal1_test\":"+std::to_string(activeGoal1Test.answer_accuracy)+
           ",\"candidate_goal1_test\":"+std::to_string(trialGoal1Test.answer_accuracy)+
           ",\"active_goal1_validation\":"+std::to_string(activeGoal1Val.answer_accuracy)+
           ",\"candidate_goal1_validation\":"+std::to_string(trialGoal1Val.answer_accuracy)+
           ",\"live_unchanged\":true}");
    } catch (...) {
        return js(env,"{\"loaded\":false,\"reason\":\"candidate_probe_exception\"}");
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeStatus(JNIEnv* env, jclass) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    return js(env, g_engine.statusJson());
}

extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeEvaluate(JNIEnv* env, jclass) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    auto tr = g_engine.evaluateTrain();
    auto va = g_engine.evaluateValidation();
    auto te = g_engine.evaluateTest();
    auto r0 = g_engine.evaluateRetentionL0();
    auto r1 = g_engine.evaluateRetentionL1();
    auto r2 = g_engine.evaluateRetentionL2();
    auto r3 = g_engine.evaluateRetentionL3();
    auto r4 = g_engine.evaluateRetentionL4();
    std::string s = "{\"step\":" + std::to_string(g_engine.globalStep()) +
        ",\"curriculum\":" + std::to_string(g_engine.curriculumLevel()) +
        ",\"curriculum_start_step\":" + std::to_string(g_engine.curriculumStartStep()) +
        ",\"parameters\":" + std::to_string(g_engine.parameterCount()) +
        ",\"train_loss\":" + std::to_string(tr.loss) +
        ",\"train_accuracy\":" + std::to_string(tr.answer_accuracy) +
        ",\"val_loss\":" + std::to_string(va.loss) +
        ",\"val_accuracy\":" + std::to_string(va.answer_accuracy) +
        ",\"test_loss\":" + std::to_string(te.loss) +
        ",\"test_accuracy\":" + std::to_string(te.answer_accuracy) +
        ",\"retention_l0_accuracy\":" + std::to_string(r0.answer_accuracy) +
        ",\"retention_l1_accuracy\":" + std::to_string(r1.answer_accuracy) +
        ",\"retention_l2_accuracy\":" + std::to_string(r2.answer_accuracy) +
        ",\"retention_l3_accuracy\":" + std::to_string(r3.answer_accuracy) +
        ",\"retention_l4_accuracy\":" + std::to_string(r4.answer_accuracy) + "}";
    return js(env, s);
}


extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeTrainingEvaluate(JNIEnv* env, jclass) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    auto tr = g_engine.evaluateTrain();
    auto va = g_engine.evaluateValidation();
    auto r0 = g_engine.evaluateRetentionL0();
    auto r1 = g_engine.evaluateRetentionL1();
    auto r2 = g_engine.evaluateRetentionL2();
    auto r3 = g_engine.evaluateRetentionL3();
    auto r4 = g_engine.evaluateRetentionL4();
    std::string s = "{\"step\":" + std::to_string(g_engine.globalStep()) +
        ",\"curriculum\":" + std::to_string(g_engine.curriculumLevel()) +
        ",\"curriculum_start_step\":" + std::to_string(g_engine.curriculumStartStep()) +
        ",\"parameters\":" + std::to_string(g_engine.parameterCount()) +
        ",\"train_loss\":" + std::to_string(tr.loss) +
        ",\"val_loss\":" + std::to_string(va.loss) +
        ",\"val_accuracy\":" + std::to_string(va.answer_accuracy) +
        ",\"retention_l0_accuracy\":" + std::to_string(r0.answer_accuracy) +
        ",\"retention_l1_accuracy\":" + std::to_string(r1.answer_accuracy) +
        ",\"retention_l2_accuracy\":" + std::to_string(r2.answer_accuracy) +
        ",\"retention_l3_accuracy\":" + std::to_string(r3.answer_accuracy) +
        ",\"retention_l4_accuracy\":" + std::to_string(r4.answer_accuracy) + "}";
    return js(env, s);
}

extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeTrainChunk(JNIEnv* env, jclass, jint steps) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    auto r = g_engine.train(static_cast<int>(steps), 24, 0.001f);
    std::string s = "{\"steps_completed\":" + std::to_string(r.steps_completed) +
        ",\"step\":" + std::to_string(g_engine.globalStep()) +
        ",\"elapsed_seconds\":" + std::to_string(r.elapsed_seconds) +
        ",\"train_loss\":" + std::to_string(r.train.loss) +
        ",\"val_loss\":" + std::to_string(r.validation.loss) +
        ",\"val_accuracy\":" + std::to_string(r.validation.answer_accuracy) +
        ",\"retention_l0_accuracy\":" + std::to_string(r.retention_l0.answer_accuracy) +
        ",\"retention_l1_accuracy\":" + std::to_string(r.retention_l1.answer_accuracy) +
        ",\"retention_l2_accuracy\":" + std::to_string(r.retention_l2.answer_accuracy) +
        ",\"retention_l3_accuracy\":" + std::to_string(r.retention_l3.answer_accuracy) +
        ",\"retention_l4_accuracy\":" + std::to_string(r.retention_l4.answer_accuracy) +
        ",\"curriculum\":" + std::to_string(g_engine.curriculumLevel()) +
        ",\"curriculum_start_step\":" + std::to_string(g_engine.curriculumStartStep()) +
        ",\"paused\":" + std::string(r.paused ? "true" : "false") + "}";
    return js(env, s);
}

extern "C" JNIEXPORT void JNICALL
Java_it_motorai_seed_MainActivity_nativeRequestPause(JNIEnv*, jclass) {
    g_engine.requestPause();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_it_motorai_seed_MainActivity_nativeSaveCheckpoint(JNIEnv* env, jclass, jstring path) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    return g_engine.saveCheckpoint(toString(env, path)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_it_motorai_seed_MainActivity_nativeLoadCheckpoint(JNIEnv* env, jclass, jstring path) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    return g_engine.loadCheckpoint(toString(env, path)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_it_motorai_seed_MainActivity_nativeReset(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    g_engine.reset(174);
}

extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGenerate(JNIEnv* env, jclass, jstring prefix) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    return js(env, g_engine.generate(toString(env, prefix), 4));
}



/** R3R10: one-shot isolated experiment. No reference to g_engine for training
 * or checkpoint output: g_engine is compared, never mutated.
 * Extra training samples are added to TRAIN in shadow only.
 * The 16 independent prompts below are not part of any training dataset.
 */
/** R3R11 Pollicino. Independent best snapshots using only existing validation
 * splits for selection. New 20 unseen holdout prompts are evaluated ONCE after
 * candidate selection, never in the training loop. No writes to g_engine.
 */
extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativePhase2R3R11Experiment(
    JNIEnv* env, jclass, jstring basePath, jstring candidatePath) {
    try {
        std::lock_guard<std::mutex> serial(g_engine_call_mu);
        const std::string base=toString(env,basePath), dest=toString(env,candidatePath);
        if(base.empty()||dest.empty()||base==dest) return js(env,R"({"status":"invalid_path"})");
        const std::string liveStatus=g_engine.statusJson();
        const std::string liveProfile=goal10RawProfile(g_engine);
        motorai::Engine selected(174);
        if(!selected.loadCheckpoint(base)) return js(env,R"({"status":"base_unreadable"})");
        if(liveStatus!=selected.statusJson()||liveProfile!=goal10RawProfile(selected))
            return js(env,R"({"status":"source_out_of_sync"})");

        struct Case {int lane; const char* question; const char* label;};
        // FRESH holdout R3R11: never added to any train/val/test.
        const Case blind[]={
            {1,"un messaggio di saluto per tutti voi","saluto"},
            {1,"ti saluto con molta cordialita","saluto"},
            {1,"auguro una piacevole giornata a motorai","saluto"},
            {1,"e un piacere poterci salutare","saluto"},
            {1,"puoi recuperare articoli sulle maree","ricerca"},
            {1,"mi serve una pubblicazione sul sistema immunitario","ricerca"},
            {1,"rintraccia materiali informativi sulle orchidee","ricerca"},
            {1,"individua ricerche pubblicate sui vulcani marini","ricerca"},
            {1,"mi calcoli la somma tra dodici e ventisei","calcolo"},
            {1,"avvia il programma per ascoltare le registrazioni","azione"},
            {7,"usa lo strumento calcolatrice per fare la somma","calculator"},
            {7,"fammi il totale aritmetico tra diciannove e tredici","calculator"},
            {7,"con uno strumento matematico calcola otto piu dodici","calculator"},
            {7,"esegui una somma usando un calcolatore numerico","calculator"},
            {7,"consulta online una ricerca dedicata agli insetti","search"},
            {7,"reperisci in rete notizie sul sistema solare","search"},
            {7,"recupera una pagina web sulle piante medicinali","search"},
            {7,"fammi vedere informazioni online sui coralli","search"},
            {7,"ti ricordi il nome che ti avevo confidato","memory"},
            {7,"ti mando un saluto affettuoso","chat"}
        };
        // Retest six known R3R9 queries; replay is NOT a new blind benchmark.
        const Case replay[]={
            {1,"mi aiuti a sommare tredici e sei","calcolo"},
            {1,"un saluto a te motore","saluto"},
            {1,"per favore trova un articolo sulla botanica","ricerca"},
            {1,"puoi aprire il lettore musicale","azione"},
            {7,"somma ventidue e trenta con la calcolatrice","calculator"},
            {7,"trova online notizie sulle comete","search"}
        };
        const auto score=[](motorai::Engine& e,const Case* a,size_t n){
            int count=0;
            for(size_t i=0;i<n;++i){
                const std::string result=a[i].lane==1?e.classifyGoal1(a[i].question):e.routeGoal7(a[i].question);
                const char*field=a[i].lane==1?"intent":"tool";
                if(result.find(std::string("\"")+field+"\":\""+a[i].label+"\"")!=std::string::npos)++count;
            }
            return count;
        };
        const auto baseVal1=selected.evaluateGoal1Validation();
        const auto baseVal7=selected.evaluateGoal7Validation();
        const auto baseTest1=selected.evaluateGoal1Test();
        const auto baseTest7=selected.evaluateGoal7Test();
        const int beforeBlind=score(selected,blind,sizeof(blind)/sizeof(blind[0]));
        const int beforeReplay=score(selected,replay,sizeof(replay)/sizeof(replay[0]));

        // Independent selection by original validation only. A worse tentative
        // step is discarded; the selected lane is never mutated in that case.
        const float rates[]={0.003f,0.007f,0.012f};
        const int budgets[]={10,20,40,80,160};
        const auto started=std::chrono::steady_clock::now();
        int bestSteps1=0,bestSteps7=0;
        int attempts1=0,attempts7=0;
        float bestAcc1=baseVal1.answer_accuracy,bestAcc7=baseVal7.answer_accuracy;
        float bestLoss1=baseVal1.loss,bestLoss7=baseVal7.loss;
        bool timedOut=false,nonfinite=false;
        for(int lane: {1,7}) {
            for(float rate:rates) {
                if(timedOut)break;
                motorai::Engine trial(174);
                if(!trial.loadCheckpoint(base))return js(env,R"({"status":"trial_checkpoint_unreadable"})");
                trial.augmentPhase2TrainOnly();
                int last=0;
                for(int budget:budgets){
                    if(std::chrono::duration<double>(
                        std::chrono::steady_clock::now()-started).count()>14.0){
                        timedOut=true;break;
                    }
                    const int chunk=budget-last;
                    if(lane==1){trial.trainGoal1(chunk,24,rate);attempts1+=chunk;}
                    else {trial.trainGoal7(chunk,24,rate);attempts7+=chunk;}
                    last=budget;
                    const auto val=lane==1?trial.evaluateGoal1Validation():trial.evaluateGoal7Validation();
                    if(!std::isfinite(val.loss)||!std::isfinite(val.answer_accuracy)){
                        nonfinite=true;break;
                    }
                    const auto baseline=lane==1?baseVal1:baseVal7;
                    if(val.answer_accuracy+0.000001f<baseline.answer_accuracy ||
                       val.loss>baseline.loss+0.01f) break; // rollback by discarding trial
                    float &bestAcc=lane==1?bestAcc1:bestAcc7;
                    float &bestLoss=lane==1?bestLoss1:bestLoss7;
                    const bool better=val.answer_accuracy>bestAcc+0.00001f
                         ||(val.answer_accuracy+0.00001f>=bestAcc && val.loss+0.00002f<bestLoss);
                    if(better){
                        if(lane==1){selected.adoptIsolatedGoal1(trial);bestSteps1=budget;}
                        else {selected.adoptIsolatedGoal7(trial);bestSteps7=budget;}
                        bestAcc=val.answer_accuracy;bestLoss=val.loss;
                    }
                }
                if(nonfinite)break;
            }
            if(nonfinite)break;
        }

        // Following evaluations occur ONCE after validation-only selection.
        const auto finalVal1=selected.evaluateGoal1Validation();
        const auto finalVal7=selected.evaluateGoal7Validation();
        const auto finalTest1=selected.evaluateGoal1Test();
        const auto finalTest7=selected.evaluateGoal7Test();
        const int afterBlind=score(selected,blind,sizeof(blind)/sizeof(blind[0]));
        const int afterReplay=score(selected,replay,sizeof(replay)/sizeof(replay[0]));
        const bool valNoLoss=finalVal1.answer_accuracy+0.000001f>=baseVal1.answer_accuracy
          &&finalVal7.answer_accuracy+0.000001f>=baseVal7.answer_accuracy
          &&finalVal1.loss<=baseVal1.loss+0.01f
          &&finalVal7.loss<=baseVal7.loss+0.01f;
        const bool testsNoLoss=finalTest1.answer_accuracy+0.000001f>=baseTest1.answer_accuracy
          &&finalTest7.answer_accuracy+0.000001f>=baseTest7.answer_accuracy;
        if(liveStatus!=g_engine.statusJson()||liveProfile!=goal10RawProfile(g_engine))
            return js(env,R"({"status":"live_changed_STOP"})");

        const bool improvement=afterBlind>beforeBlind && afterReplay>=beforeReplay;
        const bool qualified=!nonfinite && valNoLoss && testsNoLoss && improvement
             &&(bestSteps1>0||bestSteps7>0);
        if(qualified && !selected.saveCheckpoint(dest))
            return js(env,R"({"status":"candidate_save_failed"})");
        std::ostringstream out;
        out<<"{\"status\":\""<<(qualified?"candidate_saved":"candidate_rejected")
          <<"\",\"candidate_saved\":"<<(qualified?"true":"false")
          <<",\"live_unchanged\":true,\"nonfinite\":"<<(nonfinite?"true":"false")
          <<",\"validation_no_regression\":"<<(valNoLoss?"true":"false")
          <<",\"test_no_regression\":"<<(testsNoLoss?"true":"false")
          <<",\"blind_before\":"<<beforeBlind<<",\"blind_after\":"<<afterBlind
          <<",\"r3r9_target_before\":"<<beforeReplay<<",\"r3r9_target_after\":"<<afterReplay
          <<",\"goal1_validation_before\":"<<baseVal1.answer_accuracy
          <<",\"goal1_validation_after\":"<<finalVal1.answer_accuracy
          <<",\"goal1_test_before\":"<<baseTest1.answer_accuracy
          <<",\"goal1_test_after\":"<<finalTest1.answer_accuracy
          <<",\"goal7_validation_before\":"<<baseVal7.answer_accuracy
          <<",\"goal7_validation_after\":"<<finalVal7.answer_accuracy
          <<",\"goal7_test_before\":"<<baseTest7.answer_accuracy
          <<",\"goal7_test_after\":"<<finalTest7.answer_accuracy
          <<",\"goal1_best_steps\":"<<bestSteps1<<",\"goal7_best_steps\":"<<bestSteps7
          <<",\"goal1_search_steps\":"<<attempts1<<",\"goal7_search_steps\":"<<attempts7
          <<",\"time_limit\":"<<(timedOut?"true":"false")
          <<",\"active\":"<<liveProfile<<",\"candidate\":"<<goal10RawProfile(selected)<<"}";
        return js(env,out.str());
    }catch(...){return js(env,R"({"status":"r3r11_exception"})");}
}

extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativePhase2ShadowExperiment(
    JNIEnv* env, jclass, jstring basePath, jstring candidatePath) {
    try {
        std::lock_guard<std::mutex> guard(g_engine_call_mu);
        std::string base=toString(env,basePath), dest=toString(env,candidatePath);
        if(base.empty()||dest.empty()||base==dest)
            return js(env,"{\"status\":\"bad_path\"}");
        const std::string liveOriginal=g_engine.statusJson();
        const std::string goalOriginal=goal10RawProfile(g_engine);
        motorai::Engine shadow(174);
        if(!shadow.loadCheckpoint(base))
            return js(env,"{\"status\":\"checkpoint_unreadable\"}");
        if(liveOriginal!=shadow.statusJson()||
           goalOriginal!=goal10RawProfile(shadow))
            return js(env,"{\"status\":\"source_out_of_sync\"}");

        struct Probe { int model; const char* prompt; const char* expected; };
        const Probe blind[] = {
            {1,"un abbraccio e tanti saluti per te","saluto"},
            {1,"un messaggio di benvenuto a questa conversazione","saluto"},
            {1,"ti mando i miei saluti piu sinceri","saluto"},
            {1,"buona serata e cordiali saluti","saluto"},
            {1,"mi puoi rintracciare un saggio di geografia","ricerca"},
            {1,"vorrei cercare pubblicazioni sui dinosauri","ricerca"},
            {1,"recupera uno studio online sulle api","ricerca"},
            {1,"fai una ricerca bibliografica sui fossili","ricerca"},
            {7,"utilizza la calcolatrice per sommare due cifre","calculator"},
            {7,"fai un addizione fra quindici e quarantuno","calculator"},
            {7,"usa uno strumento di calcolo per otto piu cinque","calculator"},
            {7,"voglio il totale della somma fra cinque e nove","calculator"},
            {7,"consultiamo una pagina web di botanica","search"},
            {7,"rintraccia informazioni online su faraday","search"},
            {7,"ricordami quale colore ti ho detto","memory"},
            {7,"facciamo una chiacchierata tranquilla","chat"}
        };
        const Probe rerun[] = {
            {1,"mi aiuti a sommare tredici e sei","calcolo"},
            {1,"un saluto a te motore","saluto"},
            {1,"per favore trova un articolo sulla botanica","ricerca"},
            {1,"puoi aprire il lettore musicale","azione"},
            {7,"somma ventidue e trenta con la calcolatrice","calculator"},
            {7,"trova online notizie sulle comete","search"}
        };
        auto hasClass=[](const std::string& json,const char* field,const char* expected){
            return json.find(std::string("\"")+field+"\":\""+expected+"\"")!=std::string::npos;
        };
        auto score=[&](motorai::Engine& e,const Probe* arr,size_t size){
            int n=0;
            for(size_t i=0;i<size;++i){
                std::string result=arr[i].model==1?e.classifyGoal1(arr[i].prompt):e.routeGoal7(arr[i].prompt);
                if(hasClass(result,arr[i].model==1?"intent":"tool",arr[i].expected))++n;
            }
            return n;
        };
        const int blindBefore=score(shadow,blind,sizeof(blind)/sizeof(blind[0]));
        const int oldBefore=score(shadow,rerun,sizeof(rerun)/sizeof(rerun[0]));
        const auto v1Before=shadow.evaluateGoal1Validation(), t1Before=shadow.evaluateGoal1Test();
        const auto v7Before=shadow.evaluateGoal7Validation(), t7Before=shadow.evaluateGoal7Test();
        shadow.augmentPhase2TrainOnly();
        const auto start=std::chrono::steady_clock::now();
        int trained1=0,trained7=0;
        std::string stop="step_limit";
        // Early-stop decisions depend ONLY on original VAL (not on blind or old 20).
        for(int i=0;i<50;++i){
            if(std::chrono::duration<double>(
                std::chrono::steady_clock::now()-start).count()>16.0){
                stop="time_limit";break;
            }
            shadow.trainGoal1(20,24,0.017f);
            shadow.trainGoal7(20,24,0.017f);
            trained1+=20; trained7+=20;
            auto a=shadow.evaluateGoal1Validation(), b=shadow.evaluateGoal7Validation();
            if(!std::isfinite(a.loss)||!std::isfinite(b.loss)||
               !std::isfinite(a.answer_accuracy)||!std::isfinite(b.answer_accuracy)){
                stop="nonfinite";break;
            }
            if(a.answer_accuracy+0.00001<v1Before.answer_accuracy||
               b.answer_accuracy+0.00001<v7Before.answer_accuracy){
                stop="validation_regression";break;
            }
            if(a.loss>v1Before.loss+0.03f||b.loss>v7Before.loss+0.03f){
                stop="validation_loss_regression";break;
            }
        }
        const auto v1After=shadow.evaluateGoal1Validation(), t1After=shadow.evaluateGoal1Test();
        const auto v7After=shadow.evaluateGoal7Validation(), t7After=shadow.evaluateGoal7Test();
        const int blindAfter=score(shadow,blind,sizeof(blind)/sizeof(blind[0]));
        const int oldAfter=score(shadow,rerun,sizeof(rerun)/sizeof(rerun[0]));
        const bool testSafe=t1After.answer_accuracy+0.00001>=t1Before.answer_accuracy
             &&t7After.answer_accuracy+0.00001>=t7Before.answer_accuracy;
        const bool valSafe=v1After.answer_accuracy+0.00001>=v1Before.answer_accuracy
             &&v7After.answer_accuracy+0.00001>=v7Before.answer_accuracy
             &&v1After.loss<=v1Before.loss+0.03f
             &&v7After.loss<=v7Before.loss+0.03f;
        const bool blindBetter=blindAfter>blindBefore;
        const bool oldNotWorse=oldAfter>=oldBefore;
        const bool liveUnchanged=liveOriginal==g_engine.statusJson()
             &&goalOriginal==goal10RawProfile(g_engine);
        if(!liveUnchanged)return js(env,"{\"status\":\"live_changed_STOP\"}");
        const bool eligible=valSafe&&testSafe&&blindBetter&&oldNotWorse
             &&stop!="nonfinite"&&stop!="validation_regression"
             &&stop!="validation_loss_regression";
        if(eligible && !shadow.saveCheckpoint(dest))
            return js(env,"{\"status\":\"candidate_save_failed\"}");
        auto num=[](double x){return std::to_string(x);};
        std::string json=std::string("{\"status\":\"")+
            (eligible?"candidate_saved":"candidate_not_qualified")
            +"\",\"live_unchanged\":true"
            +",\"candidate_saved\":"+(eligible?"true":"false")
            +",\"blind_before\":"+std::to_string(blindBefore)
            +",\"blind_after\":"+std::to_string(blindAfter)
            +",\"r3r9_target_before\":"+std::to_string(oldBefore)
            +",\"r3r9_target_after\":"+std::to_string(oldAfter)
            +",\"goal1_validation_before\":"+num(v1Before.answer_accuracy)
            +",\"goal1_validation_after\":"+num(v1After.answer_accuracy)
            +",\"goal1_test_before\":"+num(t1Before.answer_accuracy)
            +",\"goal1_test_after\":"+num(t1After.answer_accuracy)
            +",\"goal7_validation_before\":"+num(v7Before.answer_accuracy)
            +",\"goal7_validation_after\":"+num(v7After.answer_accuracy)
            +",\"goal7_test_before\":"+num(t7Before.answer_accuracy)
            +",\"goal7_test_after\":"+num(t7After.answer_accuracy)
            +",\"goal1_steps\":"+std::to_string(trained1)
            +",\"goal7_steps\":"+std::to_string(trained7)
            +",\"stop\":\""+stop+"\""
            +",\"heldout_non_degradation\":"+(testSafe&&valSafe?"true":"false")
            +"}";
        return js(env,json);
    }catch(...){return js(env,"{\"status\":\"shadow_exception\"}");}
}

extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal1Evaluate(JNIEnv* env, jclass) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    auto tr=g_engine.evaluateGoal1Train();
    auto va=g_engine.evaluateGoal1Validation();
    std::string s="{\"goal1_step\":"+std::to_string(g_engine.goal1Step())+
        ",\"goal1_parameters\":"+std::to_string(g_engine.goal1ParameterCount())+
        ",\"train_loss\":"+std::to_string(tr.loss)+
        ",\"train_accuracy\":"+std::to_string(tr.answer_accuracy)+
        ",\"validation_loss\":"+std::to_string(va.loss)+
        ",\"validation_accuracy\":"+std::to_string(va.answer_accuracy)+"}";
    return js(env,s);
}

extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal1TrainChunk(JNIEnv* env, jclass, jint steps) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    auto r=g_engine.trainGoal1(static_cast<int>(steps),24,0.08f);
    std::string s="{\"steps_completed\":"+std::to_string(r.steps_completed)+
        ",\"goal1_step\":"+std::to_string(g_engine.goal1Step())+
        ",\"elapsed_seconds\":"+std::to_string(r.elapsed_seconds)+
        ",\"train_loss\":"+std::to_string(r.train.loss)+
        ",\"train_accuracy\":"+std::to_string(r.train.answer_accuracy)+
        ",\"validation_loss\":"+std::to_string(r.validation.loss)+
        ",\"validation_accuracy\":"+std::to_string(r.validation.answer_accuracy)+"}";
    return js(env,s);
}

extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal1FinalTest(JNIEnv* env, jclass) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    auto te=g_engine.evaluateGoal1Test();
    std::string s="{\"goal1_step\":"+std::to_string(g_engine.goal1Step())+
        ",\"test_loss\":"+std::to_string(te.loss)+
        ",\"test_accuracy\":"+std::to_string(te.answer_accuracy)+"}";
    return js(env,s);
}

extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal1Classify(JNIEnv* env, jclass, jstring text) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    return js(env,g_engine.classifyGoal1(toString(env,text)));
}


extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal2Evaluate(JNIEnv* env, jclass) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    auto tr=g_engine.evaluateGoal2Train();
    auto va=g_engine.evaluateGoal2Validation();
    std::string s="{\"goal2_step\":"+std::to_string(g_engine.goal2Step())+
        ",\"goal2_parameters\":"+std::to_string(g_engine.goal2ParameterCount())+
        ",\"train_loss\":"+std::to_string(tr.loss)+
        ",\"train_accuracy\":"+std::to_string(tr.answer_accuracy)+
        ",\"validation_loss\":"+std::to_string(va.loss)+
        ",\"validation_accuracy\":"+std::to_string(va.answer_accuracy)+"}";
    return js(env,s);
}

extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal2TrainChunk(JNIEnv* env, jclass, jint steps) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    auto r=g_engine.trainGoal2(static_cast<int>(steps),16,0.12f);
    std::string s="{\"steps_completed\":"+std::to_string(r.steps_completed)+
        ",\"goal2_step\":"+std::to_string(g_engine.goal2Step())+
        ",\"elapsed_seconds\":"+std::to_string(r.elapsed_seconds)+
        ",\"train_loss\":"+std::to_string(r.train.loss)+
        ",\"train_accuracy\":"+std::to_string(r.train.answer_accuracy)+
        ",\"validation_loss\":"+std::to_string(r.validation.loss)+
        ",\"validation_accuracy\":"+std::to_string(r.validation.answer_accuracy)+"}";
    return js(env,s);
}

extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal2FinalTest(JNIEnv* env, jclass) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    auto te=g_engine.evaluateGoal2Test();
    std::string s="{\"goal2_step\":"+std::to_string(g_engine.goal2Step())+
        ",\"test_loss\":"+std::to_string(te.loss)+
        ",\"test_accuracy\":"+std::to_string(te.answer_accuracy)+"}";
    return js(env,s);
}

extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal2Respond(JNIEnv* env, jclass, jstring text) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    return js(env,g_engine.respondGoal2(toString(env,text)));
}


extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal3Evaluate(JNIEnv* env, jclass) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    auto tr=g_engine.evaluateGoal3Train();
    auto va=g_engine.evaluateGoal3Validation();
    std::string s="{\"goal3_step\":"+std::to_string(g_engine.goal3Step())+
        ",\"goal3_parameters\":"+std::to_string(g_engine.goal3ParameterCount())+
        ",\"train_loss\":"+std::to_string(tr.loss)+
        ",\"train_accuracy\":"+std::to_string(tr.answer_accuracy)+
        ",\"validation_loss\":"+std::to_string(va.loss)+
        ",\"validation_accuracy\":"+std::to_string(va.answer_accuracy)+"}";
    return js(env,s);
}

extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal3TrainChunk(JNIEnv* env, jclass, jint steps, jfloat learningRate) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    const float rate=(learningRate>=0.0025f && learningRate<=0.08f) ? learningRate : 0.0025f;
    auto r=g_engine.trainGoal3(static_cast<int>(steps),24,rate);
    std::string s="{\"steps_completed\":"+std::to_string(r.steps_completed)+
        ",\"goal3_step\":"+std::to_string(g_engine.goal3Step())+
        ",\"elapsed_seconds\":"+std::to_string(r.elapsed_seconds)+
        ",\"train_loss\":"+std::to_string(r.train.loss)+
        ",\"train_accuracy\":"+std::to_string(r.train.answer_accuracy)+
        ",\"validation_loss\":"+std::to_string(r.validation.loss)+
        ",\"validation_accuracy\":"+std::to_string(r.validation.answer_accuracy)+"}";
    return js(env,s);
}

extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal3FinalTest(JNIEnv* env, jclass) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    auto te=g_engine.evaluateGoal3Test();
    std::string s="{\"goal3_step\":"+std::to_string(g_engine.goal3Step())+
        ",\"test_loss\":"+std::to_string(te.loss)+
        ",\"test_accuracy\":"+std::to_string(te.answer_accuracy)+"}";
    return js(env,s);
}

extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal3Classify(JNIEnv* env, jclass, jstring text) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    return js(env,g_engine.classifyGoal3(toString(env,text)));
}


extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal4Evaluate(JNIEnv* env, jclass) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    auto tr=g_engine.evaluateGoal4Train();
    auto va=g_engine.evaluateGoal4Validation();
    std::string s="{\"goal4_step\":"+std::to_string(g_engine.goal4Step())+
        ",\"goal4_parameters\":"+std::to_string(g_engine.goal4ParameterCount())+
        ",\"train_loss\":"+std::to_string(tr.loss)+
        ",\"train_accuracy\":"+std::to_string(tr.answer_accuracy)+
        ",\"validation_loss\":"+std::to_string(va.loss)+
        ",\"validation_accuracy\":"+std::to_string(va.answer_accuracy)+"}";
    return js(env,s);
}

extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal4TrainChunk(JNIEnv* env, jclass, jint steps) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    auto r=g_engine.trainGoal4(static_cast<int>(steps),24,0.08f);
    std::string s="{\"steps_completed\":"+std::to_string(r.steps_completed)+
        ",\"goal4_step\":"+std::to_string(g_engine.goal4Step())+
        ",\"elapsed_seconds\":"+std::to_string(r.elapsed_seconds)+
        ",\"train_loss\":"+std::to_string(r.train.loss)+
        ",\"train_accuracy\":"+std::to_string(r.train.answer_accuracy)+
        ",\"validation_loss\":"+std::to_string(r.validation.loss)+
        ",\"validation_accuracy\":"+std::to_string(r.validation.answer_accuracy)+"}";
    return js(env,s);
}

extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal4FinalTest(JNIEnv* env, jclass) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    auto te=g_engine.evaluateGoal4Test();
    std::string s="{\"goal4_step\":"+std::to_string(g_engine.goal4Step())+
        ",\"test_loss\":"+std::to_string(te.loss)+
        ",\"test_accuracy\":"+std::to_string(te.answer_accuracy)+"}";
    return js(env,s);
}

extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal4Solve(JNIEnv* env, jclass, jstring text) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    return js(env,g_engine.solveGoal4(toString(env,text)));
}


extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal5Evaluate(JNIEnv* env, jclass) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    auto tr=g_engine.evaluateGoal5Train();
    auto va=g_engine.evaluateGoal5Validation();
    std::string s="{\"goal5_step\":"+std::to_string(g_engine.goal5Step())+
        ",\"goal5_parameters\":"+std::to_string(g_engine.goal5ParameterCount())+
        ",\"train_loss\":"+std::to_string(tr.loss)+
        ",\"train_accuracy\":"+std::to_string(tr.answer_accuracy)+
        ",\"validation_loss\":"+std::to_string(va.loss)+
        ",\"validation_accuracy\":"+std::to_string(va.answer_accuracy)+"}";
    return js(env,s);
}

extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal5TrainChunk(JNIEnv* env, jclass, jint steps) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    auto r=g_engine.trainGoal5(static_cast<int>(steps),24,0.08f);
    std::string s="{\"steps_completed\":"+std::to_string(r.steps_completed)+
        ",\"goal5_step\":"+std::to_string(g_engine.goal5Step())+
        ",\"elapsed_seconds\":"+std::to_string(r.elapsed_seconds)+
        ",\"train_loss\":"+std::to_string(r.train.loss)+
        ",\"train_accuracy\":"+std::to_string(r.train.answer_accuracy)+
        ",\"validation_loss\":"+std::to_string(r.validation.loss)+
        ",\"validation_accuracy\":"+std::to_string(r.validation.answer_accuracy)+"}";
    return js(env,s);
}

extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal5FinalTest(JNIEnv* env, jclass) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    auto te=g_engine.evaluateGoal5Test();
    std::string s="{\"goal5_step\":"+std::to_string(g_engine.goal5Step())+
        ",\"test_loss\":"+std::to_string(te.loss)+
        ",\"test_accuracy\":"+std::to_string(te.answer_accuracy)+"}";
    return js(env,s);
}

extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal5Classify(JNIEnv* env, jclass, jstring text) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    return js(env,g_engine.classifyGoal5(toString(env,text)));
}


extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal6Evaluate(JNIEnv* env, jclass) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    auto tr=g_engine.evaluateGoal6Train();
    auto va=g_engine.evaluateGoal6Validation();
    std::string s="{\"goal6_step\":"+std::to_string(g_engine.goal6Step())+
        ",\"goal6_parameters\":"+std::to_string(g_engine.goal6ParameterCount())+
        ",\"train_loss\":"+std::to_string(tr.loss)+
        ",\"train_accuracy\":"+std::to_string(tr.answer_accuracy)+
        ",\"validation_loss\":"+std::to_string(va.loss)+
        ",\"validation_accuracy\":"+std::to_string(va.answer_accuracy)+"}";
    return js(env,s);
}

extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal6TrainChunk(JNIEnv* env, jclass, jint steps) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    auto r=g_engine.trainGoal6(static_cast<int>(steps),24,0.08f);
    std::string s="{\"steps_completed\":"+std::to_string(r.steps_completed)+
        ",\"goal6_step\":"+std::to_string(g_engine.goal6Step())+
        ",\"elapsed_seconds\":"+std::to_string(r.elapsed_seconds)+
        ",\"train_loss\":"+std::to_string(r.train.loss)+
        ",\"train_accuracy\":"+std::to_string(r.train.answer_accuracy)+
        ",\"validation_loss\":"+std::to_string(r.validation.loss)+
        ",\"validation_accuracy\":"+std::to_string(r.validation.answer_accuracy)+"}";
    return js(env,s);
}

extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal6FinalTest(JNIEnv* env, jclass) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    auto te=g_engine.evaluateGoal6Test();
    std::string s="{\"goal6_step\":"+std::to_string(g_engine.goal6Step())+
        ",\"test_loss\":"+std::to_string(te.loss)+
        ",\"test_accuracy\":"+std::to_string(te.answer_accuracy)+"}";
    return js(env,s);
}

extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal6Plan(JNIEnv* env, jclass, jstring text) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    return js(env,g_engine.planGoal6(toString(env,text)));
}


extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal7Evaluate(JNIEnv* env, jclass) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    auto tr=g_engine.evaluateGoal7Train(); auto va=g_engine.evaluateGoal7Validation();
    std::string s="{\"goal7_step\":"+std::to_string(g_engine.goal7Step())+
        ",\"goal7_parameters\":"+std::to_string(g_engine.goal7ParameterCount())+
        ",\"train_loss\":"+std::to_string(tr.loss)+",\"train_accuracy\":"+std::to_string(tr.answer_accuracy)+
        ",\"validation_loss\":"+std::to_string(va.loss)+",\"validation_accuracy\":"+std::to_string(va.answer_accuracy)+"}";
    return js(env,s);
}
extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal7TrainChunk(JNIEnv* env, jclass, jint steps) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    auto r=g_engine.trainGoal7(static_cast<int>(steps),24,0.08f);
    std::string s="{\"steps_completed\":"+std::to_string(r.steps_completed)+",\"goal7_step\":"+std::to_string(g_engine.goal7Step())+
        ",\"elapsed_seconds\":"+std::to_string(r.elapsed_seconds)+",\"train_loss\":"+std::to_string(r.train.loss)+
        ",\"train_accuracy\":"+std::to_string(r.train.answer_accuracy)+",\"validation_loss\":"+std::to_string(r.validation.loss)+
        ",\"validation_accuracy\":"+std::to_string(r.validation.answer_accuracy)+"}";
    return js(env,s);
}
extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal7FinalTest(JNIEnv* env, jclass) {
    std::lock_guard<std::mutex> g(g_engine_call_mu); auto te=g_engine.evaluateGoal7Test();
    std::string s="{\"goal7_step\":"+std::to_string(g_engine.goal7Step())+
        ",\"test_loss\":"+std::to_string(te.loss)+",\"test_accuracy\":"+std::to_string(te.answer_accuracy)+"}";
    return js(env,s);
}
extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal7Route(JNIEnv* env, jclass, jstring text) {
    std::lock_guard<std::mutex> g(g_engine_call_mu); return js(env,g_engine.routeGoal7(toString(env,text)));
}


extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal8Evaluate(JNIEnv* env,jclass){
    std::lock_guard<std::mutex>g(g_engine_call_mu);auto tr=g_engine.evaluateGoal8Train(),va=g_engine.evaluateGoal8Validation();
    return js(env,"{\"goal8_step\":"+std::to_string(g_engine.goal8Step())+",\"goal8_parameters\":"+std::to_string(g_engine.goal8ParameterCount())+
        ",\"train_loss\":"+std::to_string(tr.loss)+",\"train_accuracy\":"+std::to_string(tr.answer_accuracy)+
        ",\"validation_loss\":"+std::to_string(va.loss)+",\"validation_accuracy\":"+std::to_string(va.answer_accuracy)+"}");
}
extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal8TrainChunk(JNIEnv* env,jclass,jint steps){
    std::lock_guard<std::mutex>g(g_engine_call_mu);auto r=g_engine.trainGoal8(static_cast<int>(steps),24,0.08f);
    return js(env,"{\"steps_completed\":"+std::to_string(r.steps_completed)+",\"goal8_step\":"+std::to_string(g_engine.goal8Step())+
        ",\"elapsed_seconds\":"+std::to_string(r.elapsed_seconds)+",\"train_loss\":"+std::to_string(r.train.loss)+
        ",\"train_accuracy\":"+std::to_string(r.train.answer_accuracy)+",\"validation_loss\":"+std::to_string(r.validation.loss)+
        ",\"validation_accuracy\":"+std::to_string(r.validation.answer_accuracy)+"}");
}
extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal8FinalTest(JNIEnv* env,jclass){
    std::lock_guard<std::mutex>g(g_engine_call_mu);auto te=g_engine.evaluateGoal8Test();
    return js(env,"{\"goal8_step\":"+std::to_string(g_engine.goal8Step())+",\"test_loss\":"+std::to_string(te.loss)+
        ",\"test_accuracy\":"+std::to_string(te.answer_accuracy)+"}");
}
extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal8Plan(JNIEnv* env,jclass,jstring text){
    std::lock_guard<std::mutex>g(g_engine_call_mu);return js(env,g_engine.planGoal8(toString(env,text)));
}


extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal9Evaluate(JNIEnv* env,jclass){
    std::lock_guard<std::mutex>g(g_engine_call_mu);auto tr=g_engine.evaluateGoal9Train(),va=g_engine.evaluateGoal9Validation();
    return js(env,"{\"goal9_step\":"+std::to_string(g_engine.goal9Step())+",\"goal9_parameters\":"+std::to_string(g_engine.goal9ParameterCount())+
        ",\"train_loss\":"+std::to_string(tr.loss)+",\"train_accuracy\":"+std::to_string(tr.answer_accuracy)+
        ",\"validation_loss\":"+std::to_string(va.loss)+",\"validation_accuracy\":"+std::to_string(va.answer_accuracy)+"}");
}
extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal9TrainChunk(JNIEnv* env,jclass,jint steps){
    std::lock_guard<std::mutex>g(g_engine_call_mu);auto r=g_engine.trainGoal9(static_cast<int>(steps),24,0.08f);
    return js(env,"{\"steps_completed\":"+std::to_string(r.steps_completed)+",\"goal9_step\":"+std::to_string(g_engine.goal9Step())+
        ",\"elapsed_seconds\":"+std::to_string(r.elapsed_seconds)+",\"train_loss\":"+std::to_string(r.train.loss)+
        ",\"train_accuracy\":"+std::to_string(r.train.answer_accuracy)+",\"validation_loss\":"+std::to_string(r.validation.loss)+
        ",\"validation_accuracy\":"+std::to_string(r.validation.answer_accuracy)+"}");
}
extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal9FinalTest(JNIEnv* env,jclass){
    std::lock_guard<std::mutex>g(g_engine_call_mu);auto te=g_engine.evaluateGoal9Test();
    return js(env,"{\"goal9_step\":"+std::to_string(g_engine.goal9Step())+",\"test_loss\":"+std::to_string(te.loss)+
        ",\"test_accuracy\":"+std::to_string(te.answer_accuracy)+"}");
}
extern "C" JNIEXPORT jstring JNICALL
Java_it_motorai_seed_MainActivity_nativeGoal9Review(JNIEnv* env,jclass,jstring request,jstring draft){
    std::lock_guard<std::mutex>g(g_engine_call_mu);
    return js(env,g_engine.reviewGoal9(toString(env,request),toString(env,draft)));
}

extern "C" JNIEXPORT void JNICALL
Java_it_motorai_seed_MainActivity_nativeSetCurriculum(JNIEnv*, jclass, jint level) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    g_engine.setCurriculum(static_cast<int>(level));
}

extern "C" JNIEXPORT jint JNICALL
Java_it_motorai_seed_MainActivity_nativeCurriculum(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    return static_cast<jint>(g_engine.curriculumLevel());
}
