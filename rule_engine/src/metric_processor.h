
#ifndef METRIC_PROCESSOR_H
#define METRIC_PROCESSOR_H

#include "rule_engine.h"

struct MetricInput
{
    double current = 0.0;
    double average = 0.0;
    double sd = 0.0;
    bool available = false;
};

struct MetricInputs
{
    MetricInput pageFaults;
    MetricInput availableRam;
    MetricInput cpu;
    MetricInput diskQueue;
    MetricInput diskTime;
};

bool processMetrics(
    const MetricInputs &inputs, MetricScores &scores, const char *&errorMessage
);

#endif
