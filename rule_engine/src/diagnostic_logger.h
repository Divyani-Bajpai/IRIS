#ifndef DIAGNOSTIC_LOGGER_H
#define DIAGNOSTIC_LOGGER_H

#include "rule_engine.h"
#include <string>
using namespace std;

void logDiagnosis(const RuleResult &result);

// Saves a diagnosis to a Member-3-owned table in the shared iris_data.db.
// Requires the project's existing shared sqlite3.h/sqlite3 library.
bool saveDiagnosis(const string &dbPath, const RuleResult &result, const MetricScores &scores, string &error);

#endif
