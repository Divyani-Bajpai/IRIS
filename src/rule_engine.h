
#ifndef RULE_ENGINE_H
#define RULE_ENGINE_H

#include <string>
using namespace std;

const char *const RULE_NONE = "NO_RULE_MATCHED";
const char *const RULE_MEMORY_THRASHING = "MEMORY_THRASHING";
const char *const RULE_DISK_IO_CONTENTION = "DISK_IO_CONTENTION";

struct MetricScores
{
    double pageFaultsZ = 0.0;
    double availableRamZ = 0.0;
    double cpuZ = 0.0;
    double diskQueueZ = 0.0;
    double diskTimeZ = 0.0;

    bool pageFaultsValid = false;
    bool availableRamValid = false;
    bool cpuValid = false;
    bool diskQueueValid = false;
    bool diskTimeValid = false;
};

struct RuleResult
{
    bool matched = false;
    string ruleName = RULE_NONE;
    string diagnosis = "No matching rule";
    string reason = "No rule conditions matched";
};

RuleResult evaluateRules(MetricScores scores);

#endif
