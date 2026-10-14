#include <cmath>
#include "rule_engine.h"

using namespace std;

RuleResult evaluateRules(MetricScores scores)
{
    RuleResult result;

    if (scores.pageFaultsValid &&
        scores.availableRamValid &&
        scores.cpuValid &&
        isfinite(scores.pageFaultsZ) &&
        isfinite(scores.availableRamZ) &&
        isfinite(scores.cpuZ) &&
        scores.pageFaultsZ > 2.0 &&
        scores.availableRamZ < -2.0 &&
        scores.cpuZ >= -1.0 &&
        scores.cpuZ <= 1.0)
    {
        result.matched = true;
        result.ruleName = RULE_MEMORY_THRASHING;
        result.diagnosis = "Possible Memory Thrashing";
        result.reason = "High page faults and low available RAM";
        return result;
    }

    if (scores.diskQueueValid &&
        scores.diskTimeValid &&
        scores.availableRamValid &&
        isfinite(scores.diskQueueZ) &&
        isfinite(scores.diskTimeZ) &&
        isfinite(scores.availableRamZ) &&
        scores.diskQueueZ > 2.0 &&
        scores.diskTimeZ > 2.0 &&
        scores.availableRamZ >= -1.0 &&
        scores.availableRamZ <= 1.0)
    {
        result.matched = true;
        result.ruleName = RULE_DISK_IO_CONTENTION;
        result.diagnosis = "Possible Disk I/O Contention";
        result.reason = "High disk queue and disk activity";
        return result;
    }

    return result;
}
