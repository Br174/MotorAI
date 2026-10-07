#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace motorai {

struct Metrics {
    float loss = 0.0f;
    float answer_accuracy = 0.0f;
};

struct TrainResult {
    int steps_completed = 0;
    Metrics train;
    Metrics validation;
    Metrics test;
    Metrics retention_l0;
    Metrics retention_l1;
    Metrics retention_l2;
    Metrics retention_l3;
    Metrics retention_l4;
    double elapsed_seconds = 0.0;
    bool paused = false;
};

struct Goal1TrainResult {
    int steps_completed = 0;
    Metrics train;
    Metrics validation;
    double elapsed_seconds = 0.0;
};

class Engine {
public:
    explicit Engine(uint32_t seed = 174);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void reset(uint32_t seed = 174);
    Metrics evaluateTrain();
    Metrics evaluateValidation();
    Metrics evaluateTest();
    Metrics evaluateRetentionL0();
    Metrics evaluateRetentionL1();
    Metrics evaluateRetentionL2();
    Metrics evaluateRetentionL3();
    Metrics evaluateRetentionL4();
    TrainResult train(int steps, int batch_size = 24, float lr = 0.001f);
    void requestPause();
    void clearPause();
    bool saveCheckpoint(const std::string& directory) const;
    bool loadCheckpoint(const std::string& directory);
    std::string generate(const std::string& prefix, int new_chars = 4);
    std::string statusJson() const;
    int parameterCount() const;
    int globalStep() const;
    void setCurriculum(int level);
    int curriculumLevel() const;
    int curriculumStartStep() const;

    // Mini-AI Goal 1: understand the broad intent of a simple Italian request.
    // This module is trained from scratch and is isolated from the accepted L0-L5 weights.
    Metrics evaluateGoal1Train();
    Metrics evaluateGoal1Validation();
    Metrics evaluateGoal1Test();
    Goal1TrainResult trainGoal1(int steps, int batch_size = 24, float lr = 0.08f);
    int goal1Step() const;
    int goal1ParameterCount() const;
    std::string classifyGoal1(const std::string& text) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace motorai
