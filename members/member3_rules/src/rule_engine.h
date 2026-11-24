#ifndef RULE_ENGINE_H
#define RULE_ENGINE_H

#include <string>
#include <vector>

struct MetricScores {
    double pageFaultZ;
    double availableRamZ;
    double cpuZ;
    double diskQueueZ;
    double diskTimeZ;
};

struct RuleResult {
    bool matched = false;
    std::string ruleName;
    std::string diagnosis;
    std::string reason;
    std::string recommendationText;
    // Hard-coded, user-facing steps to try for the detected root cause.
    std::vector<std::string> suggestions;
};

RuleResult evaluateRules(const MetricScores& scores);

#endif