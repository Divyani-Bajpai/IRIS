#include <cmath>
#include "rule_engine.h"

RuleResult evaluateRules(MetricScores scores) {
    
    RuleResult result;

    result.matched = false;
    result.diagnosis = "No matching Rule";
    result.reason = "No rule conditions matched";

    // Check for the Scores
    if(!isfinite(scores.pageFaultZ) || !isfinite(scores.availableRamZ) || !isfinite(scores.cpuZ)  || !isfinite(scores.diskQueueZ) || !isfinite(scores.diskTimeZ)) {
        result.diagnosis = "Invalid metrics Score";
        result.reason = "Some Scores are not Valid.";
        return result;
    }

    // Defining Rules :-> Rule 1 : Memory Thrashing
    if(scores.pageFaultZ > 2 && scores.availableRamZ < -2 && scores.cpuZ >= -1 && scores.cpuZ <= 1) {
        result.matched = true;
        result.diagnosis = "Possible Memory Thrasing";
        result.reason = "High Page Fault And Low Available RAM";
        return result;
    }

    // Rule 2 : Disk I/O Contention
    if(scores.diskQueueZ > 2 && scores.diskTimeZ > 2 && scores.availableRamZ >= -1 && scores.availableRamZ <= 1) {
        result.matched = true;
        result.diagnosis = "Possible Disk I/O Contenstion";
        result.reason = "Disk Queue and disk activity are high while RAM is near Baseline";
        return result;
    }
    return result;
}