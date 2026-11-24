#pragma once

#include <string>
#include <vector>

namespace iris {

// The five system metric names below are the shared IRIS diagnosis contract.
// Process-specific values use separate names so they cannot be mistaken for
// system-wide values by the baseline or rule engine.
inline constexpr char kDiskQueueLength[] = "disk_queue_length";
inline constexpr char kPercentDiskTime[] = "percent_disk_time";
inline constexpr char kCpuPercent[] = "cpu_percent";
inline constexpr char kAvailableRamMb[] = "available_ram_mb";
inline constexpr char kPageFaultsPerSec[] = "page_faults_per_sec";
inline constexpr char kProcessCpuPercent[] = "process_cpu_percent";
inline constexpr char kProcessMemoryMb[] = "process_memory_mb";
inline constexpr char kDiskUsedPercent[] = "disk_used_percent";
inline constexpr char kDiskFreeGb[] = "disk_free_gb";
inline constexpr char kDiskReadMbPerSec[] = "disk_read_mb_per_sec";
inline constexpr char kDiskWriteMbPerSec[] = "disk_write_mb_per_sec";
inline constexpr char kProcessDiskReadMbPerSec[] = "process_disk_read_mb_per_sec";
inline constexpr char kProcessDiskWriteMbPerSec[] = "process_disk_write_mb_per_sec";
// Provisional shared defaults matching Member 1's current branch; confirm with
// the team before merging the modules into the main application.
inline constexpr int kLearnedBaselineWindowDays = 30;
inline constexpr int kLearnedBaselineMinimumSamples = 500;
inline constexpr int kLearnedBaselineWarmupDays = 7;

struct MetricSample {
    std::string timestamp;   // UTC: YYYY-MM-DD HH:MM:SS
    std::string metricName;
    double value = 0.0;
    std::string processName; // Empty means a system-wide metric (SQL NULL).
};

struct CollectionResult {
    std::vector<MetricSample> samples;
    std::vector<std::string> warnings;
};

struct BaselineRecord {
    std::string metricName;
    double averageValue = 0.0;
    double standardDeviation = 0.0;
    std::string lastUpdated;
    std::string source;
};

}  // namespace iris
