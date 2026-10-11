#pragma once

#include <string>

namespace iris {

// One sample of CPU, RAM and page-fault readings.
// Every metric has its own valid flag. If the flag is false, ignore the value.
struct CpuMemReading {
    std::string timestamp;           // UTC, "YYYY-MM-DD HH:MM:SS"

    bool   cpuValid = false;
    double cpuPercent = 0.0;         // 0 to 100, total CPU time

    bool   ramValid = false;
    double ramTotalMB = 0.0;         // installed physical RAM
    double ramAvailableMB = 0.0;     // free physical RAM
    double ramUsedPercent = 0.0;     // 0 to 100, physical RAM only

    bool   committedValid = false;
    double committedPercent = 0.0;   // committed virtual memory, not physical RAM

    bool   pageFaultsValid = false;
    double pageFaultsPerSec = 0.0;   // system-wide rate, includes soft faults
};

// Baseline for one metric. Filled in Stage 3.
struct BaselineStats {
    std::string metricName;
    double mean = 0.0;
    double stdDev = 0.0;
    int    sampleCount = 0;
    bool   ready = false;            // false until enough valid readings exist
};

}  // namespace iris