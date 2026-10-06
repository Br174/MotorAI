#include <iostream>
#include <iomanip>
#include "motorai_core.h"

int main() {
    motorai::Engine e(174);
    auto initial = e.evaluateTest();
    auto r0 = e.train(220, 24, 0.001f);
    auto level0 = e.evaluateTest();
    if (level0.answer_accuracy < 0.90f) {
        std::cerr << "FAIL level0 accuracy=" << level0.answer_accuracy << "\n";
        return 2;
    }

    e.setCurriculum(1);
    auto beforeRetention = e.evaluateRetention();
    auto r1 = e.train(800, 24, 0.001f);
    auto level1 = e.evaluateTest();
    auto retention = e.evaluateRetention();

    std::cout << std::fixed << std::setprecision(4)
              << "initial_l0_acc=" << initial.answer_accuracy
              << " level0_acc=" << level0.answer_accuracy
              << " before_retention=" << beforeRetention.answer_accuracy
              << " level1_acc=" << level1.answer_accuracy
              << " level1_loss=" << level1.loss
              << " retention=" << retention.answer_accuracy
              << " step=" << e.globalStep() << "\n";

    if (level1.answer_accuracy < 0.90f) return 3;
    if (retention.answer_accuracy < 0.90f) return 4;
    if (e.globalStep() != 1020) return 5;
    return 0;
}
