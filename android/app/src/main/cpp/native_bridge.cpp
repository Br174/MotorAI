#include <jni.h>
#include <mutex>
#include <string>
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
Java_it_motorai_seed_MainActivity_nativeGoal3TrainChunk(JNIEnv* env, jclass, jint steps) {
    std::lock_guard<std::mutex> g(g_engine_call_mu);
    auto r=g_engine.trainGoal3(static_cast<int>(steps),24,0.08f);
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
