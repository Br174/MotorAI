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
    std::string s = "{\"step\":" + std::to_string(g_engine.globalStep()) +
        ",\"curriculum\":" + std::to_string(g_engine.curriculumLevel()) +
        ",\"parameters\":" + std::to_string(g_engine.parameterCount()) +
        ",\"train_loss\":" + std::to_string(tr.loss) +
        ",\"train_accuracy\":" + std::to_string(tr.answer_accuracy) +
        ",\"val_loss\":" + std::to_string(va.loss) +
        ",\"val_accuracy\":" + std::to_string(va.answer_accuracy) +
        ",\"test_loss\":" + std::to_string(te.loss) +
        ",\"test_accuracy\":" + std::to_string(te.answer_accuracy) +
        ",\"retention_l0_accuracy\":" + std::to_string(r0.answer_accuracy) +
        ",\"retention_l1_accuracy\":" + std::to_string(r1.answer_accuracy) + "}";
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
        ",\"test_loss\":" + std::to_string(r.test.loss) +
        ",\"test_accuracy\":" + std::to_string(r.test.answer_accuracy) +
        ",\"retention_l0_accuracy\":" + std::to_string(r.retention_l0.answer_accuracy) +
        ",\"retention_l1_accuracy\":" + std::to_string(r.retention_l1.answer_accuracy) +
        ",\"curriculum\":" + std::to_string(g_engine.curriculumLevel()) +
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
