#pragma once
#include <sqlite3.h>
#include <string>
#include <vector>
#include "iris/metric_types.h"
namespace iris {
struct StartupAppRecord {
    std::string appName;
    std::string registryLocation;
};

class SharedMetricsStore {
public:
    SharedMetricsStore() = default;
    ~SharedMetricsStore();
    SharedMetricsStore(const SharedMetricsStore&) = delete;
    SharedMetricsStore& operator=(const SharedMetricsStore&) = delete;
    bool open(const std::string& path, std::string& error);
    bool insertSamples(const std::vector<MetricSample>& samples, std::string& error);
    bool pruneRawMetrics(int retentionDays, std::string& error);
    bool recomputeDiskBaselines(int windowDays, int minSamples, std::string& error);
    bool recomputeSystemBaselines(int windowDays, int minSamples, std::string& error);
    bool readLatestMetric(const std::string& metricName,
                          const std::string& processName,
                          double& value, bool& found, std::string& error);
    bool readBaseline(const std::string& metricName, BaselineRecord& out, bool& found, std::string& error);
    bool beginDiagnosticPeriod(const std::string& ruleName, std::string& error);
    bool endDiagnosticPeriod(std::string& error);
    bool recordDiagnosticEvent(const std::string& ruleName,
                               const std::string& metricValuesJson,
                               std::string& error);
    bool bootstrapDefaultBaselines(double totalRamMb, std::string& error);
    bool recordStartupApps(const std::vector<StartupAppRecord>& apps,
                           std::string& error);
    void close();
    bool isOpen() const { return db_ != nullptr; }
private:
    bool createSchema(std::string& error);
    bool ensureProcessNameColumn(std::string& error);
    bool loadSystemValues(const std::string& metricName, int windowDays, std::vector<double>& values, std::string& error);
    bool saveLearnedBaseline(const BaselineRecord& record, std::string& error);
    bool recomputeBaselines(const std::vector<std::string>& metricNames,
                            int windowDays, int minSamples,
                            std::string& error);
    sqlite3* db_ = nullptr;
    sqlite3_stmt* insertStmt_ = nullptr;
};
}
