#include <cstdio>

#include "diagnosis_cycle.h"

using namespace std;

// One metric as JSON: its current value, baseline and deviation score.
static string metricJson(const char *name, const MetricInput &input, double z)
{
    char text[256];
    snprintf(text, sizeof(text),
             "\"%s\":{\"current\":%.2f,\"baseline_mean\":%.2f,"
             "\"baseline_sd\":%.2f,\"deviation\":%.2f}",
             name, input.current, input.average, input.sd, z);
    return text;
}

// Evidence = the three metrics that the matched rule looked at.
static string buildEvidence(const CycleResult &c)
{
    if (c.rule.ruleName == RULE_MEMORY_THRASHING)
    {
        return "{" +
               metricJson(METRIC_PAGE_FAULTS, c.inputs.pageFaults, c.scores.pageFaultsZ) + "," +
               metricJson(METRIC_AVAILABLE_RAM, c.inputs.availableRam, c.scores.availableRamZ) + "," +
               metricJson(METRIC_CPU, c.inputs.cpu, c.scores.cpuZ) + "}";
    }

    return "{" +
           metricJson(METRIC_DISK_QUEUE, c.inputs.diskQueue, c.scores.diskQueueZ) + "," +
           metricJson(METRIC_DISK_TIME, c.inputs.diskTime, c.scores.diskTimeZ) + "," +
           metricJson(METRIC_AVAILABLE_RAM, c.inputs.availableRam, c.scores.availableRamZ) + "}";
}

bool runDiagnosisCycle(DatabaseReader &reader, EventStore &store,
                       CycleResult &result, string &error)
{
    result = CycleResult();
    error.clear();

    // 1. Latest readings + baselines
    if (!reader.loadInputs(result.inputs, error))
        return false;

    // 2. Deviation (Z) scores
    const char *scoreError = "";
    if (!processMetrics(result.inputs, result.scores, scoreError))
    {
        result.dataReady = false;
        result.message = string(scoreError) +
                         " (no fresh readings, or baseline not ready yet)";
        return true;   // nothing to diagnose in this cycle, not an error
    }
    result.dataReady = true;

    // 3. Rules (first match wins)
    result.rule = evaluateRules(result.scores);

    // 4. Save the event only when a rule matched
    if (result.rule.matched)
    {
        if (!store.saveEvent(result.rule.ruleName, buildEvidence(result), error))
            return false;
        result.eventSaved = true;

        string ignored;
        store.getRecommendation(result.rule.ruleName, result.recommendation, ignored);
    }
    return true;
}
