#pragma once

#include <windows.h>
#include <pdh.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "iris/metric_types.h"

namespace iris {

// Calculate process CPU as a percentage of total machine capacity.
// FILETIME values are in 100 ns units. Returns false for invalid or reversed
// samples, including a process that exited/restarted between observations.
bool calculateProcessCpuPercent(std::uint64_t previousCpu100ns,
                                std::uint64_t currentCpu100ns,
                                double elapsedSeconds,
                                unsigned int logicalProcessorCount,
                                double& percent);

class DiskProcessMonitor {
public:
    DiskProcessMonitor();
    ~DiskProcessMonitor();

    DiskProcessMonitor(const DiskProcessMonitor&) = delete;
    DiskProcessMonitor& operator=(const DiskProcessMonitor&) = delete;

    bool initialize(std::string& error);
    CollectionResult collect();

private:
    struct ProcessState {
        std::uint64_t creationTime100ns = 0;
        std::uint64_t cpuTime100ns = 0;
        std::uint64_t readBytes = 0;
        std::uint64_t writeBytes = 0;
        bool ioCountersValid = false;
        std::chrono::steady_clock::time_point sampledAt;
    };

    void collectDiskMetrics(const std::string& timestamp,
                            CollectionResult& result);
    void collectDiskCapacityMetrics(const std::string& timestamp,
                                    CollectionResult& result);
    void collectProcessMetrics(const std::string& timestamp,
                               CollectionResult& result);
    bool readCounter(PDH_HCOUNTER counter, double& value) const;
    void close();

    PDH_HQUERY query_ = nullptr;
    PDH_HCOUNTER queueCounter_ = nullptr;
    PDH_HCOUNTER diskTimeCounter_ = nullptr;
    PDH_HCOUNTER diskReadCounter_ = nullptr;
    PDH_HCOUNTER diskWriteCounter_ = nullptr;
    bool initialized_ = false;
    std::string diskInitializationWarning_;
    bool diskInitializationWarningReported_ = false;
    bool diskCollectionWarningReported_ = false;
    std::chrono::steady_clock::time_point lastDiskCollection_;
    unsigned int logicalProcessorCount_ = 0;
    std::unordered_map<DWORD, ProcessState> previousProcesses_;
};

std::string utcTimestampNow();

}  // namespace iris
