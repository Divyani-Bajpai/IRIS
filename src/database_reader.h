// database_reader.h
// Reads the latest metric values and their baselines from the database,
// so the rule engine can use them.
//
// Where the data comes from:
//   CPU, RAM, page faults  -> table cpu_mem_readings  (Member 1)
//   Disk queue, disk time  -> table live_metrics      (disk module)
//   Baselines              -> table metric_baseline   (Member 1)

#ifndef DATABASE_READER_H
#define DATABASE_READER_H

#include <ctime>
#include <string>

#include "metric_processor.h"

struct sqlite3;

// A reading older than this many seconds is "stale" and is ignored.
const int FRESH_SECONDS = 30;

// The five metric names used by the rule engine.
// They are also used in the evidence saved with each event.
const char *const METRIC_PAGE_FAULTS = "page_faults_per_sec";
const char *const METRIC_AVAILABLE_RAM = "available_ram_mb";
const char *const METRIC_CPU = "cpu_percent";
const char *const METRIC_DISK_QUEUE = "disk_queue_length";
const char *const METRIC_DISK_TIME = "percent_disk_time";

// Gives time as text like "2026-10-14 08:31:17" (UTC, same as the database).
std::string utcTimeText(std::time_t t);

class DatabaseReader
{
public:
    DatabaseReader();
    ~DatabaseReader();

    // A reader cannot be copied (it owns open database connections).
    DatabaseReader(const DatabaseReader &) = delete;
    DatabaseReader &operator=(const DatabaseReader &) = delete;

    // Opens the database file. Call this first.
    bool open(const std::string &dbPath, std::string &error);

    // Recalculates the baselines of CPU, RAM and page faults from saved history.
    bool recomputeBaselines(int minSamples, std::string &error);

    // Loads the newest reading and the baseline of every metric.
    //  - A metric with no fresh reading, or no ready baseline, is left
    //    "not available". It is never replaced by 0.
    //  - Returns false only if the database itself fails.
    bool loadInputs(MetricInputs &inputs, std::string &error);

    // Closes the database. Safe to call more than once.
    void close();

private:
    // Gets the newest value of one disk metric from live_metrics.
    bool readLiveMetric(const char *metricName, const std::string &since, double &value);

    // Joins a current value with its baseline into one MetricInput.
    bool fillInput(const char *baselineName, bool found, double current,
                   MetricInput &input, std::string &error);

    sqlite3 *db_ = nullptr;
};

#endif
