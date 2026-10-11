#ifndef RULE_ENGINE_H
#define RULE_ENGINE_H

#include <string>
using namespace std;

struct MetricScores {
    double pageFaultZ;
    double availableRamZ;
    double cpuZ;
    double diskQueueZ;
    double diskTimeZ;
};

struct RuleResult {
    bool matched;
    string diagnosis;
    string reason;
};

RuleResult evaluateRules(MetricScores);

#endif