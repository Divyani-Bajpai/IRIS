#include "iris/shared_metrics_store.h"
#include "iris/disk_process_monitor.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#define CHECK(expression) do { if (!(expression)) { \
    std::cerr << "Check failed at line " << __LINE__ << ": " #expression << '\n'; \
    return 1; } } while (false)

int main() {
    iris::SharedMetricsStore store;
    std::string error;
    CHECK(store.open(":memory:", error));
    CHECK(store.bootstrapDefaultBaselines(8192.0, error));
    iris::BaselineRecord baseline;
    bool found = false;
    CHECK(store.readBaseline(iris::kCpuPercent, baseline, found, error));
    CHECK(found && baseline.source == "default");
    CHECK(std::abs(baseline.averageValue - 15.0) < 0.000001);
    CHECK(store.readBaseline(iris::kAvailableRamMb, baseline, found, error));
    CHECK(found && baseline.source == "default");
    CHECK(std::abs(baseline.averageValue - 8192.0 * 0.35) < 0.000001);

    const std::string timestamp = iris::utcTimestampNow();
    const std::vector<iris::MetricSample> samples = {
        {timestamp, iris::kDiskQueueLength, 0.1, {}},
        {timestamp, iris::kDiskQueueLength, 0.2, {}},
        {timestamp, iris::kDiskQueueLength, 0.3, {}},
        // Process rows must not leak into system-wide learned baselines.
        {timestamp, iris::kDiskQueueLength, 99.0, "fixture.exe"},
        {timestamp, iris::kPercentDiskTime, 10.0, {}},
        {timestamp, iris::kPercentDiskTime, 20.0, {}},
        {timestamp, iris::kPercentDiskTime, 30.0, {}},
    };
    CHECK(store.insertSamples(samples, error));
    CHECK(store.recomputeDiskBaselines(30, 3, error));

    CHECK(store.readBaseline(iris::kDiskQueueLength, baseline, found, error));
    CHECK(found && baseline.source == "learned");
    CHECK(std::abs(baseline.averageValue - 0.2) < 0.000001);
    CHECK(std::abs(baseline.standardDeviation - 0.1) < 0.000001);

    CHECK(store.readBaseline(iris::kPercentDiskTime, baseline, found, error));
    CHECK(found && baseline.source == "learned");
    CHECK(std::abs(baseline.averageValue - 20.0) < 0.000001);
    CHECK(std::abs(baseline.standardDeviation - 10.0) < 0.000001);

    // High disk readings can be recorded and retrieved as real observations.
    const std::vector<iris::MetricSample> spike = {
        {timestamp, iris::kDiskQueueLength, 7.5, {}},
        {timestamp, iris::kPercentDiskTime, 88.0, {}},
    };
    CHECK(store.insertSamples(spike, error));
    double latest = 0.0;
    CHECK(store.readLatestMetric(iris::kDiskQueueLength, "", latest, found, error));
    CHECK(found && std::abs(latest - 7.5) < 0.000001);
    CHECK(store.readLatestMetric(iris::kPercentDiskTime, "", latest, found, error));
    CHECK(found && std::abs(latest - 88.0) < 0.000001);

    CHECK(store.readLatestMetric(iris::kDiskQueueLength, "fixture.exe",
                                latest, found, error));
    CHECK(found && std::abs(latest - 99.0) < 0.000001);
    CHECK(store.recordDiagnosticEvent(
        "memory_thrashing", "{\"page_faults_per_sec\":12}", error));
    store.close();
    return 0;
}
