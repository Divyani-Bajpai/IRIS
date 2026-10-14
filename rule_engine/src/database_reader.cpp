// database_reader.cpp
// Job of this file: read the latest metric values and their baselines
// from the database, and give them to the rule engine.
//
// Where the data comes from:
//   CPU, RAM, page faults  -> table cpu_mem_readings  (Member 1)
//   Disk queue, disk time  -> table live_metrics      (disk module)
//   Baselines              -> table metric_baseline   (Member 1)

#include <cmath>
#include <cstdlib>
#include <vector>

#include "database_reader.h"
#include "sqlite3.h"

using namespace std;

// Member 1 calls the free-RAM metric "ram_available_mb" in the baseline table.
static const char *const BASELINE_RAM_NAME = "ram_available_mb";
static const int DEFAULT_WINDOW_DAYS = 7;


// ------------------------------------------------------------
// Small helper: time as text, like "2026-10-14 08:31:17" (UTC)
// ------------------------------------------------------------
string utcTimeText(time_t t)
{
    tm utc{};

#ifdef _WIN32
    gmtime_s(&utc, &t);
#else
    gmtime_r(&t, &utc);
#endif

    char text[20];
    strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &utc);
    return text;
}


// ------------------------------------------------------------
// The newest valid value of CPU, RAM and page faults
// ------------------------------------------------------------
struct NewestValues
{
    bool foundCpu = false;
    bool foundRam = false;
    bool foundPageFaults = false;

    double cpu = 0.0;
    double ram = 0.0;
    double pageFaults = 0.0;
};

// "readings" is sorted oldest first, so we walk from the end (newest).
// For each metric we keep the first valid value we see.
struct CpuMemReading
{
    bool cpuValid = false;
    bool ramValid = false;
    bool pageFaultsValid = false;
    double cpuPercent = 0.0;
    double ramAvailableMB = 0.0;
    double pageFaultsPerSec = 0.0;
};

struct BaselineStats
{
    bool ready = false;
    double mean = 0.0;
    double stdDev = 0.0;
};

static NewestValues pickNewestValues(const vector<CpuMemReading> &readings)
{
    NewestValues newest;

    for (int i = static_cast<int>(readings.size()) - 1; i >= 0; --i)
    {
        const CpuMemReading &r = readings[i];

        if (!newest.foundCpu && r.cpuValid)
        {
            newest.cpu = r.cpuPercent;
            newest.foundCpu = true;
        }

        if (!newest.foundRam && r.ramValid)
        {
            newest.ram = r.ramAvailableMB;
            newest.foundRam = true;
        }

        if (!newest.foundPageFaults && r.pageFaultsValid)
        {
            newest.pageFaults = r.pageFaultsPerSec;
            newest.foundPageFaults = true;
        }
    }

    return newest;
}


// ------------------------------------------------------------
// Open / close
// ------------------------------------------------------------
DatabaseReader::DatabaseReader() {}

DatabaseReader::~DatabaseReader()
{
    close();
}

static string sqliteError(sqlite3 *db, const char *what)
{
    string text = what;
    text += ": ";
    text += (db != nullptr) ? sqlite3_errmsg(db) : "no database handle";
    return text;
}

bool DatabaseReader::open(const string &dbPath, string &error)
{
    close();

    if (sqlite3_open(dbPath.c_str(), &db_) != SQLITE_OK)
    {
        error = sqliteError(db_, "sqlite3_open");
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }

    sqlite3_busy_timeout(db_, 3000);
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    return true;
}

void DatabaseReader::close()
{
    if (db_ != nullptr)
    {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}


// ------------------------------------------------------------
// Baselines
// ------------------------------------------------------------

// Asks Member 1's code to recalculate the baselines from saved history.
bool DatabaseReader::recomputeBaselines(int minSamples, string &error)
{
    if (db_ == nullptr)
    {
        error = "Database is not open";
        return false;
    }

    const char *sql =
        "INSERT OR REPLACE INTO metric_baseline "
        "(metric_name, mean, std_dev, sample_count, min_samples, ready, window_days, updated_at) "
        "SELECT metric_name, AVG(value), "
        "       CASE WHEN COUNT(*) > 1 THEN "
        "            sqrt((SUM(value * value) - SUM(value) * SUM(value) / COUNT(*)) / (COUNT(*) - 1)) "
        "       ELSE 0 END, "
        "       COUNT(*), ?, "
        "       CASE WHEN COUNT(*) >= ? THEN 1 ELSE 0 END, "
        "       ?, datetime('now') "
        "FROM ("
        "  SELECT 'cpu_percent' AS metric_name, cpu_percent AS value, timestamp FROM cpu_mem_readings WHERE cpu_percent IS NOT NULL "
        "  UNION ALL SELECT 'ram_available_mb', ram_available_mb, timestamp FROM cpu_mem_readings WHERE ram_available_mb IS NOT NULL "
        "  UNION ALL SELECT 'page_faults_per_sec', page_faults_per_sec, timestamp FROM cpu_mem_readings WHERE page_faults_per_sec IS NOT NULL "
        "  UNION ALL SELECT metric_name, value, timestamp FROM live_metrics WHERE value IS NOT NULL"
        ") "
        "WHERE timestamp >= datetime('now', ?) "
        "GROUP BY metric_name;";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        error = sqliteError(db_, "prepare recompute baselines");
        return false;
    }

    string window = "-" + to_string(DEFAULT_WINDOW_DAYS) + " days";
    sqlite3_bind_int(stmt, 1, minSamples);
    sqlite3_bind_int(stmt, 2, minSamples);
    sqlite3_bind_int(stmt, 3, DEFAULT_WINDOW_DAYS);
    sqlite3_bind_text(stmt, 4, window.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    if (!ok)
        error = sqliteError(db_, "recompute baselines");
    sqlite3_finalize(stmt);
    return ok;
}

static bool getBaseline(sqlite3 *db, const char *name, BaselineStats &baseline, string &error)
{
    const char *sql =
        "SELECT mean, std_dev, ready FROM metric_baseline WHERE metric_name = ? LIMIT 1;";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        error = sqliteError(db, "prepare read baseline");
        return false;
    }

    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW)
    {
        baseline.mean = sqlite3_column_double(stmt, 0);
        baseline.stdDev = sqlite3_column_double(stmt, 1);
        baseline.ready = sqlite3_column_int(stmt, 2) != 0;
    }

    bool ok = (rc == SQLITE_ROW || rc == SQLITE_DONE);
    if (!ok)
        error = sqliteError(db, "read baseline");
    sqlite3_finalize(stmt);
    return ok;
}

// Puts a current value and its baseline into one MetricInput.
// The metric is "available" only if BOTH the reading and the baseline are usable.
bool DatabaseReader::fillInput(const char *baselineName, bool found, double current,
                               MetricInput &input, string &error)
{
    // No fresh reading -> leave the metric as "not available".
    if (!found)
        return true;

    input.current = current;

    // Load the saved baseline of this metric.
    BaselineStats baseline;
    if (!getBaseline(db_, baselineName, baseline, error))
        return false;   // database problem

    // Baseline not ready or not usable -> leave the metric as "not available".
    bool usable = baseline.ready &&
                  isfinite(baseline.mean) &&
                  isfinite(baseline.stdDev) &&
                  baseline.stdDev > 0;
    if (!usable)
        return true;

    input.average = baseline.mean;
    input.sd = baseline.stdDev;
    input.available = true;
    return true;
}


// ------------------------------------------------------------
// Disk values (table live_metrics)
// ------------------------------------------------------------

// Gets the newest value of one disk metric.
// Returns false if there is no fresh value (or the table does not exist yet).
bool DatabaseReader::readLiveMetric(const char *metricName, const string &since, double &value)
{
    if (db_ == nullptr)
        return false;

    const char *sql =
        "SELECT value FROM live_metrics "
        "WHERE metric_name = ? AND timestamp >= ? AND value IS NOT NULL "
        "ORDER BY timestamp DESC, id DESC LIMIT 1;";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;   // table missing: the disk module is not running yet

    sqlite3_bind_text(stmt, 1, metricName, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, since.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        value = sqlite3_column_double(stmt, 0);
        found = true;
    }

    sqlite3_finalize(stmt);
    return found;
}


// ------------------------------------------------------------
// Main function: load everything the rule engine needs
// ------------------------------------------------------------
bool DatabaseReader::loadInputs(MetricInputs &inputs, string &error)
{
    inputs = MetricInputs{};
    error.clear();

    if (db_ == nullptr)
    {
        error = "Database is not open";
        return false;
    }

    // Step 1: only readings from the last FRESH_SECONDS seconds count.
    string since = utcTimeText(time(nullptr) - FRESH_SECONDS);

    // Step 2: newest CPU, RAM and page-fault values.
    vector<CpuMemReading> readings;
    const char *sql =
        "SELECT cpu_percent, ram_available_mb, page_faults_per_sec "
        "FROM cpu_mem_readings "
        "WHERE timestamp >= ? "
        "ORDER BY timestamp ASC;";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        error = sqliteError(db_, "prepare read cpu_mem_readings");
        return false;
    }

    sqlite3_bind_text(stmt, 1, since.c_str(), -1, SQLITE_TRANSIENT);

    int rc = SQLITE_ROW;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        CpuMemReading reading;
        reading.cpuValid = sqlite3_column_type(stmt, 0) != SQLITE_NULL;
        reading.ramValid = sqlite3_column_type(stmt, 1) != SQLITE_NULL;
        reading.pageFaultsValid = sqlite3_column_type(stmt, 2) != SQLITE_NULL;
        reading.cpuPercent = sqlite3_column_double(stmt, 0);
        reading.ramAvailableMB = sqlite3_column_double(stmt, 1);
        reading.pageFaultsPerSec = sqlite3_column_double(stmt, 2);
        readings.push_back(reading);
    }

    if (rc != SQLITE_DONE)
    {
        error = sqliteError(db_, "read cpu_mem_readings");
        sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_finalize(stmt);

    NewestValues newest = pickNewestValues(readings);

    // Step 3: newest disk queue and disk time values.
    double diskQueue = 0.0;
    double diskTime = 0.0;
    bool foundQueue = readLiveMetric(METRIC_DISK_QUEUE, since, diskQueue);
    bool foundTime = readLiveMetric(METRIC_DISK_TIME, since, diskTime);

    // Step 4: attach the baseline to each value.
    if (!fillInput("page_faults_per_sec", newest.foundPageFaults, newest.pageFaults,
                   inputs.pageFaults, error))
        return false;

    if (!fillInput(BASELINE_RAM_NAME, newest.foundRam, newest.ram,
                   inputs.availableRam, error))
        return false;

    if (!fillInput("cpu_percent", newest.foundCpu, newest.cpu,
                   inputs.cpu, error))
        return false;

    if (!fillInput("disk_queue_length", foundQueue, diskQueue,
                   inputs.diskQueue, error))
        return false;

    if (!fillInput("percent_disk_time", foundTime, diskTime,
                   inputs.diskTime, error))
        return false;

    return true;
}
