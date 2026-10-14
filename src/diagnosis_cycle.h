#ifndef DIAGNOSIS_CYCLE_H
#define DIAGNOSIS_CYCLE_H

#include <string>

#include "database_reader.h"
#include "event_store.h"
#include "metric_processor.h"
#include "rule_engine.h"

struct CycleResult
{
    bool dataReady = false;
    std::string message;
    MetricInputs inputs;
    MetricScores scores;
    RuleResult rule;
    bool eventSaved = false;
    std::string recommendation;
};

bool runDiagnosisCycle(
    DatabaseReader &reader,
    EventStore &store,
    CycleResult &result,
    std::string &error
);

#endif
