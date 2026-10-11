#include <iostream>
#include "deviation_calculator.h"
#include "rule_engine.h"
#include "diagnostic_logger.h"

using namespace std;

int main() {
    double pageFaultsZ;
    double ramZ;
    double cpuZ;
    double diskQueueZ;
    double diskTimeZ;

    // Calculate Deviation Scores
    bool ok1 = deviationCalculation(130, 70, 20, pageFaultsZ);
    bool ok2 = deviationCalculation(20, 60, 20, ramZ);
    bool ok3 = deviationCalculation(50, 50, 10, cpuZ);
    bool ok4 = deviationCalculation(10, 10, 5, diskQueueZ);
    bool ok5 = deviationCalculation(40, 40, 10, diskTimeZ);

    if (!ok1 || !ok2 || !ok3 || !ok4 || !ok5) {
        cout << "Invalid input for calculation" << endl;
        return 1;
    }

    // Give Calculation Score to Rule Engine
    MetricScores scores = {
        pageFaultsZ, ramZ, cpuZ, diskQueueZ, diskTimeZ
    };
    
    RuleResult result = evaluateRules(scores);

    // Display Diagnosis
    logDiagnosis(result);

    return 0;

}