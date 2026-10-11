#pragma once

#include <string>
#include <vector>

#include "metric_types.h"

struct sqlite3;

namespace iris {

// Design choices, still to be approved by the team.
const int kDefaultWindowDays = 7;
const int kDefaultMinSamples = 720;   // about 1 hour at a 5 second interval

// Mean and sample standard deviation (n - 1) of the given values.
// ready is true only when there are at least minSamples values.
BaselineStats computeBaselineStats(const std::string& metricName,
                                   const std::vector<double>& values,
                                   int minSamples);

// Learned, machine-specific baseline for the CPU and memory metrics.
// It reads cpu_mem_readings and keeps results in the metric_baseline table.
// If a baseline is not ready, the rest of IRIS should use the default baseline.
class CpuMemoryBaseline {
public:
    CpuMemoryBaseline();
    ~CpuMemoryBaseline();

    CpuMemoryBaseline(const CpuMemoryBaseline&) = delete;
    CpuMemoryBaseline& operator=(const CpuMemoryBaseline&) = delete;

    // Opens the database file and makes sure metric_baseline exists.
    bool open(const std::string& dbPath, std::string& error);

    // Recalculates all baselines from the last windowDays days and saves them.
    bool recompute(int windowDays, int minSamples, std::string& error);

    // Loads the saved baseline of one metric.
    // If nothing is saved yet, returns true with ready = false and count 0.
    bool getBaseline(const std::string& metricName, BaselineStats& out,
                     std::string& error);

    // Loads the saved baselines of all metrics of this module.
    bool getAllBaselines(std::vector<BaselineStats>& out, std::string& error);

    // Names of the metrics this module keeps a baseline for.
    static const std::vector<std::string>& metricNames();

    // Safe to call more than once.
    void close();

    bool isOpen() const { return db_ != nullptr; }

private:
    bool createSchema(std::string& error);
    bool loadValues(const std::string& metricName, int windowDays,
                    std::vector<double>& values, std::string& error);
    bool saveStats(const BaselineStats& stats, int minSamples, int windowDays,
                   std::string& error);

    sqlite3* db_ = nullptr;
};

}  // namespace iris