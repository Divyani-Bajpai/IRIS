#pragma once

#include <sqlite3.h>
#include <string>

namespace iris {

struct DiagnosisSummary {
    bool active = false;
    bool liveMetricsAvailable = false;
    long long eventId = 0;
    std::string timestamp;
    std::string rule;
    std::string metricValues;
    std::string recommendation;
};

void showLiveMetrics(sqlite3* database);
DiagnosisSummary getLatestDiagnosis(sqlite3* database);
void showDiagnosis(const DiagnosisSummary& diagnosis);
void showTechnicalDetails(sqlite3* database, const DiagnosisSummary& diagnosis);
void runReport(sqlite3* database);

} // namespace iris
