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
    double elapsed_seconds = 0.0;
    bool paused = false;
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
    TrainResult train(int steps, int batch_size = 24, float lr = 0.01f);
    void requestPause();
    void clearPause();
    bool saveCheckpoint(const std::string& directory) const;
    bool loadCheckpoint(const std::string& directory);
    std::string generate(const std::string& prefix, int new_chars = 4);
    std::string statusJson() const;
    int parameterCount() const;
    int globalStep() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace motorai
