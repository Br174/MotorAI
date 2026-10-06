#include <iostream>
#include <iomanip>
#include "motorai_core.h"

int main() {
    motorai::Engine e(174);

    auto initial = e.evaluateTest();
    e.train(220, 24, 0.001f);
    auto l0 = e.evaluateTest();
    if (l0.answer_accuracy < 0.90f) {
        std::cerr << "FAIL L0 accuracy=" << l0.answer_accuracy << "\n";
        return 2;
    }

    e.setCurriculum(1);
    e.train(400, 24, 0.001f);
    auto l1 = e.evaluateTest();
    auto l0ret1 = e.evaluateRetentionL0();
    if (l1.answer_accuracy < 0.90f || l0ret1.answer_accuracy < 0.90f) {
        std::cerr << "FAIL L1 accuracy=" << l1.answer_accuracy
                  << " L0ret=" << l0ret1.answer_accuracy << "\n";
        return 3;
    }

    e.setCurriculum(2);
    auto beforeL0 = e.evaluateRetentionL0();
    auto beforeL1 = e.evaluateRetentionL1();
    e.train(600, 24, 0.001f);
    auto l2 = e.evaluateTest();
    auto l0ret2 = e.evaluateRetentionL0();
    auto l1ret2 = e.evaluateRetentionL1();

    std::cout << std::fixed << std::setprecision(4)
              << "initial_l0=" << initial.answer_accuracy
              << " l0=" << l0.answer_accuracy
              << " l1=" << l1.answer_accuracy
              << " l0ret_after_l1=" << l0ret1.answer_accuracy
              << " before_l2_l0=" << beforeL0.answer_accuracy
              << " before_l2_l1=" << beforeL1.answer_accuracy
              << " l2=" << l2.answer_accuracy
              << " l2_loss=" << l2.loss
              << " l1ret_after_l2=" << l1ret2.answer_accuracy
              << " l0ret_after_l2=" << l0ret2.answer_accuracy
              << " step=" << e.globalStep() << "\n";

    if (l2.answer_accuracy < 0.90f) return 4;
    if (l1ret2.answer_accuracy < 0.90f) return 5;
    if (l0ret2.answer_accuracy < 0.90f) return 6;
    if (e.globalStep() != 1220) return 7;
    return 0;
}
