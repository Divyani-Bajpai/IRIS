#include "deviation_calculator.h"
#include "iris/disk_process_monitor.h"
#include "iris/shared_metrics_store.h"
#include "rule_engine.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#define CHECK(expression) do { if (!(expression)) { \
    std::cerr << "Check failed at line " << __LINE__ << ": " #expression << '\n'; \
    return 1; } } while (false)

namespace {
bool readZ(iris::SharedMetricsStore& store, const std::string& name,
           double& z, std::string& error) {
    double current = 0.0;
    bool found = false;
    if (!store.readLatestMetric(name, "", current, found, error) || !found) {
        return false;
    }
    iris::BaselineRecord baseline;
    if (!store.readBaseline(name, baseline, found, error) || !found) {
        return false;
    }
    return deviationCalculation(current, baseline.averageValue,
                                baseline.standardDeviation, z);
}

bool makeScores(iris::SharedMetricsStore& store, MetricScores& scores,
                std::string& error) {
    return readZ(store, iris::kPageFaultsPerSec, scores.pageFaultZ, error) &&
           readZ(store, iris::kAvailableRamMb, scores.availableRamZ, error) &&
           readZ(store, iris::kCpuPercent, scores.cpuZ, error) &&
           readZ(store, iris::kDiskQueueLength, scores.diskQueueZ, error) &&
           readZ(store, iris::kPercentDiskTime, scores.diskTimeZ, error);
}
} // namespace

int main() {
    iris::SharedMetricsStore store;
    std::string error;
    CHECK(store.open(":memory:", error));
    const std::string timestamp = iris::utcTimestampNow();
    const std::vector<iris::MetricSample> normal = {
        {timestamp, iris::kCpuPercent, 20.0, {}},
        {timestamp, iris::kCpuPercent, 22.0, {}},
        {timestamp, iris::kCpuPercent, 24.0, {}},
        {timestamp, iris::kAvailableRamMb, 1000.0, {}},
        {timestamp, iris::kAvailableRamMb, 1050.0, {}},
        {timestamp, iris::kAvailableRamMb, 1100.0, {}},
        {timestamp, iris::kPageFaultsPerSec, 1.0, {}},
        {timestamp, iris::kPageFaultsPerSec, 2.0, {}},
        {timestamp, iris::kPageFaultsPerSec, 3.0, {}},
        {timestamp, iris::kDiskQueueLength, 0.1, {}},
        {timestamp, iris::kDiskQueueLength, 0.2, {}},
        {timestamp, iris::kDiskQueueLength, 0.3, {}},
        {timestamp, iris::kPercentDiskTime, 10.0, {}},
        {timestamp, iris::kPercentDiskTime, 20.0, {}},
        {timestamp, iris::kPercentDiskTime, 30.0, {}},
    };
    CHECK(store.insertSamples(normal, error));
    CHECK(store.recomputeSystemBaselines(7, 3, error));

    // Synthetic memory-thrashing readings should match the Member 3 rule.
    const std::vector<iris::MetricSample> memoryAnomaly = {
        {timestamp, iris::kCpuPercent, 22.0, {}},
        {timestamp, iris::kAvailableRamMb, 800.0, {}},
        {timestamp, iris::kPageFaultsPerSec, 10.0, {}},
        {timestamp, iris::kDiskQueueLength, 0.2, {}},
        {timestamp, iris::kPercentDiskTime, 20.0, {}},
    };
    CHECK(store.insertSamples(memoryAnomaly, error));
    MetricScores scores{};
    CHECK(makeScores(store, scores, error));
    RuleResult result = evaluateRules(scores);
    CHECK(result.matched && result.ruleName == "memory_thrashing");
    CHECK(!result.recommendationText.empty());
    CHECK(!result.suggestions.empty());
    CHECK(store.recordDiagnosticEvent(result.ruleName, "{}", error));

    // Disk contention matches while RAM remains near baseline.
    const std::vector<iris::MetricSample> diskAnomaly = {
        {timestamp, iris::kCpuPercent, 22.0, {}},
        {timestamp, iris::kAvailableRamMb, 1050.0, {}},
        {timestamp, iris::kPageFaultsPerSec, 2.0, {}},
        {timestamp, iris::kDiskQueueLength, 1.0, {}},
        {timestamp, iris::kPercentDiskTime, 100.0, {}},
    };
    CHECK(store.insertSamples(diskAnomaly, error));
    CHECK(makeScores(store, scores, error));
    result = evaluateRules(scores);
    CHECK(result.matched && result.ruleName == "disk_io_contention");
    CHECK(!result.suggestions.empty());
    CHECK(store.recordDiagnosticEvent(result.ruleName, "{}", error));

    // A zero-variance baseline cannot be used for a z-score.
    double z = 0.0;
    CHECK(!deviationCalculation(2.0, 2.0, 0.0, z));
    store.close();
    return 0;
}
