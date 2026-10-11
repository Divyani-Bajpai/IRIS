#pragma once

#include <string>
#include "metric_types.h"

namespace iris {

// Reads CPU, RAM and page-fault counters through the Windows PDH API.
// Usage: init() once, then call sampleOnce() every interval, then close().
// The caller handles the waiting between samples (see the demo).
class MetricsCollector {
public:
    MetricsCollector();
    ~MetricsCollector();

    MetricsCollector(const MetricsCollector&) = delete;
    MetricsCollector& operator=(const MetricsCollector&) = delete;

    // Opens the PDH query and takes the first (priming) sample.
    // Returns false and fills error if something fails.
    bool init(std::string& error);

    // Takes one reading. Returns false only if collection itself failed.
    // Individual metrics can still be invalid, so check the flags.
    bool sampleOnce(CpuMemReading& reading, std::string& error);

    // Releases the PDH query and all counters. Safe to call more than once.
    void close();

    bool isOpen() const { return query_ != nullptr; }

private:
    void*  query_ = nullptr;
    void*  cpuCounter_ = nullptr;
    void*  availableCounter_ = nullptr;
    void*  committedCounter_ = nullptr;
    void*  pageFaultCounter_ = nullptr;
    double totalRamMB_ = 0.0;
};

}  // namespace iris