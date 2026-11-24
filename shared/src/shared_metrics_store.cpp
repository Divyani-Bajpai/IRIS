#include "iris/shared_metrics_store.h"

#include <algorithm>
#include <cmath>

namespace iris {
namespace {
std::string sqliteError(sqlite3* db, const char* operation) {
    std::string result(operation);
    result += ": ";
    result += db != nullptr ? sqlite3_errmsg(db) : "database is not open";
    return result;
}

bool hasColumn(sqlite3* db, const char* table, const char* column,
               bool& found, std::string& error) {
    found = false;
    const std::string sql = std::string("PRAGMA table_info(") + table + ");";
    sqlite3_stmt* statement = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr);
    if (rc != SQLITE_OK) {
        error = sqliteError(db, "inspect shared schema");
        return false;
    }
    while ((rc = sqlite3_step(statement)) == SQLITE_ROW) {
        const unsigned char* name = sqlite3_column_text(statement, 1);
        if (name != nullptr &&
            std::string(reinterpret_cast<const char*>(name)) == column) {
            found = true;
        }
    }
    if (rc != SQLITE_DONE) {
        error = sqliteError(db, "read shared schema");
        sqlite3_finalize(statement);
        return false;
    }
    sqlite3_finalize(statement);
    return true;
}
} // namespace

SharedMetricsStore::~SharedMetricsStore() { close(); }

bool SharedMetricsStore::open(const std::string& path, std::string& error) {
    close();
    int rc = sqlite3_open(path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        error = sqliteError(db_, "open SQLite database");
        close();
        return false;
    }
    sqlite3_busy_timeout(db_, 3000);
    if (sqlite3_exec(db_, "PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr)
        != SQLITE_OK) {
        error = sqliteError(db_, "enable SQLite foreign keys");
        close();
        return false;
    }
    if (sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr)
        != SQLITE_OK) {
        error = sqliteError(db_, "enable SQLite WAL mode");
        close();
        return false;
    }
    if (!createSchema(error) || !ensureProcessNameColumn(error)) {
        close();
        return false;
    }
    const char* sql =
        "INSERT INTO live_metrics (timestamp, metric_name, value, process_name) "
        "VALUES (?, ?, ?, ?);";
    rc = sqlite3_prepare_v2(db_, sql, -1, &insertStmt_, nullptr);
    if (rc != SQLITE_OK) {
        error = sqliteError(db_, "prepare live_metrics insert");
        close();
        return false;
    }
    return true;
}

bool SharedMetricsStore::createSchema(std::string& error) {
    const char* sql =
        "CREATE TABLE IF NOT EXISTS live_metrics ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " timestamp DATETIME NOT NULL,"
        " metric_name TEXT NOT NULL,"
        " value REAL NOT NULL,"
        " process_name TEXT);"
        "CREATE INDEX IF NOT EXISTS idx_live_metrics_metric_time "
        "ON live_metrics(metric_name, timestamp);"
        "CREATE TABLE IF NOT EXISTS iris_metadata ("
        " key TEXT PRIMARY KEY, value TEXT NOT NULL);"
        "INSERT OR IGNORE INTO iris_metadata(key,value) "
        "SELECT 'first_metric_timestamp', MIN(timestamp) FROM live_metrics "
        "HAVING COUNT(*) > 0;"
        "CREATE TABLE IF NOT EXISTS baselines ("
        " metric_name TEXT PRIMARY KEY,"
        " average_value REAL NOT NULL,"
        " standard_deviation REAL NOT NULL,"
        " last_updated DATETIME NOT NULL,"
        " source TEXT NOT NULL DEFAULT 'default');"
        "CREATE TABLE IF NOT EXISTS diagnostic_rules ("
        " rule_name TEXT PRIMARY KEY,"
        " condition_logic TEXT NOT NULL,"
        " recommendation_text TEXT NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS diagnostic_events ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " timestamp DATETIME NOT NULL,"
        " rule_matched TEXT NOT NULL,"
        " metric_values TEXT,"
        " FOREIGN KEY (rule_matched) REFERENCES diagnostic_rules(rule_name)"
        ");"
        "CREATE TABLE IF NOT EXISTS diagnostic_periods ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " rule_name TEXT NOT NULL,"
        " started_at DATETIME NOT NULL,"
        " ended_at DATETIME);"
        "CREATE TABLE IF NOT EXISTS startup_apps ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " timestamp DATETIME NOT NULL,"
        " app_name TEXT NOT NULL,"
        " registry_location TEXT);"
        "CREATE TABLE IF NOT EXISTS boot_times ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " timestamp DATETIME NOT NULL,"
        " boot_duration_seconds REAL NOT NULL);";
    char* message = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &message);
    if (rc != SQLITE_OK) {
        error = std::string("create shared schema: ") +
                (message != nullptr ? message : sqlite3_errmsg(db_));
        sqlite3_free(message);
        return false;
    }
    const char* rules =
        "INSERT OR IGNORE INTO diagnostic_rules "
        "(rule_name, condition_logic, recommendation_text) VALUES "
        "('memory_thrashing', "
        "'page_faults_per_sec>+2.0 AND available_ram_mb<-2.0 AND cpu_percent between -1.0 and 1.0', "
        "'Memory Thrashing detected. Your PC does not have enough free memory for what is currently running, so it is constantly swapping data to disk. Close memory-heavy apps or add more RAM.'),"
        "('disk_io_contention', "
        "'disk_queue_length>+2.0 AND percent_disk_time>+2.0 AND available_ram_mb between -1.0 and 1.0', "
        "'Disk I/O Contention detected. Something is heavily using your disk right now. Check for background downloads, backups, or updates.');";
    rc = sqlite3_exec(db_, rules, nullptr, nullptr, &message);
    if (rc == SQLITE_OK) return true;
    error = std::string("insert diagnostic rules: ") +
            (message != nullptr ? message : sqlite3_errmsg(db_));
    sqlite3_free(message);
    return false;
}

bool SharedMetricsStore::ensureProcessNameColumn(std::string& error) {
    bool found = false;
    if (!hasColumn(db_, "live_metrics", "process_name", found, error)) return false;
    if (found) return true;
    const int rc = sqlite3_exec(db_,
        "ALTER TABLE live_metrics ADD COLUMN process_name TEXT;",
        nullptr, nullptr, nullptr);
    if (rc == SQLITE_OK) return true;
    error = sqliteError(db_, "add live_metrics.process_name");
    return false;
}

bool SharedMetricsStore::insertSamples(
    const std::vector<MetricSample>& samples, std::string& error) {
    if (!isOpen() || insertStmt_ == nullptr) {
        error = "SQLite database is not open";
        return false;
    }
    if (samples.empty()) return true;
    if (sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr)
        != SQLITE_OK) {
        error = sqliteError(db_, "begin live_metrics transaction");
        return false;
    }
    for (const MetricSample& sample : samples) {
        if (sample.timestamp.empty() || sample.metricName.empty() ||
            !std::isfinite(sample.value)) {
            error = "Refusing to insert a sample with an empty field or non-finite value";
            sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }
        sqlite3_reset(insertStmt_);
        sqlite3_clear_bindings(insertStmt_);
        sqlite3_bind_text(insertStmt_, 1, sample.timestamp.c_str(), -1,
                          SQLITE_TRANSIENT);
        sqlite3_bind_text(insertStmt_, 2, sample.metricName.c_str(), -1,
                          SQLITE_TRANSIENT);
        sqlite3_bind_double(insertStmt_, 3, sample.value);
        if (sample.processName.empty()) {
            sqlite3_bind_null(insertStmt_, 4);
        } else {
            sqlite3_bind_text(insertStmt_, 4, sample.processName.c_str(), -1,
                              SQLITE_TRANSIENT);
        }
        const int rc = sqlite3_step(insertStmt_);
        if (rc != SQLITE_DONE) {
            error = sqliteError(db_, "insert live_metrics sample");
            sqlite3_reset(insertStmt_);
            sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }
    }
    const auto firstSample = std::min_element(samples.begin(), samples.end(),
        [](const MetricSample& a, const MetricSample& b) { return a.timestamp < b.timestamp; });
    sqlite3_stmt* historyStatement = nullptr;
    if (sqlite3_prepare_v2(db_,
            "INSERT OR IGNORE INTO iris_metadata(key,value) VALUES('first_metric_timestamp',?);",
            -1, &historyStatement, nullptr) != SQLITE_OK) {
        error = sqliteError(db_, "prepare first metric timestamp");
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }
    sqlite3_bind_text(historyStatement, 1, firstSample->timestamp.c_str(), -1, SQLITE_TRANSIENT);
    const int historyRc = sqlite3_step(historyStatement);
    sqlite3_finalize(historyStatement);
    if (historyRc != SQLITE_DONE) {
        error = sqliteError(db_, "save first metric timestamp");
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }
    if (sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        error = sqliteError(db_, "commit live_metrics transaction");
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }
    return true;
}

bool SharedMetricsStore::pruneRawMetrics(int retentionDays, std::string& error) {
    if (!isOpen() || retentionDays <= 0) {
        error = "SQLite database must be open and retention days must be positive";
        return false;
    }
    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "DELETE FROM live_metrics WHERE datetime(timestamp) < "
        "datetime('now', '-' || ? || ' days');";
    if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = sqliteError(db_, "prepare old metric cleanup");
        return false;
    }
    sqlite3_bind_int(statement, 1, retentionDays);
    const int rc = sqlite3_step(statement);
    sqlite3_finalize(statement);
    if (rc != SQLITE_DONE) {
        error = sqliteError(db_, "remove metrics older than retention period");
        return false;
    }
    const char* periodsSql =
        "DELETE FROM diagnostic_periods WHERE "
        "COALESCE(ended_at,started_at) < datetime('now','-' || ? || ' days');";
    if (sqlite3_prepare_v2(db_, periodsSql, -1, &statement, nullptr) != SQLITE_OK) {
        error = sqliteError(db_, "prepare old diagnostic period cleanup");
        return false;
    }
    sqlite3_bind_int(statement, 1, retentionDays);
    const int periodRc = sqlite3_step(statement);
    sqlite3_finalize(statement);
    if (periodRc != SQLITE_DONE) {
        error = sqliteError(db_, "remove old diagnostic periods");
        return false;
    }
    return true;
}

bool SharedMetricsStore::loadSystemValues(const std::string& metricName,
                                          int windowDays,
                                          std::vector<double>& values,
                                          std::string& error) {
    values.clear();
    const char* sql =
        "SELECT m.value FROM live_metrics m "
        "WHERE m.metric_name = ? AND m.process_name IS NULL "
        "AND m.timestamp >= datetime('now', ?) "
        "AND NOT EXISTS (SELECT 1 FROM diagnostic_periods p "
        "WHERE datetime(m.timestamp) >= datetime(p.started_at) "
        "AND (p.ended_at IS NULL OR datetime(m.timestamp) <= datetime(p.ended_at)));";
    sqlite3_stmt* statement = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr);
    if (rc != SQLITE_OK) {
        error = sqliteError(db_, "prepare disk baseline query");
        return false;
    }
    const std::string window = "-" + std::to_string(windowDays) + " days";
    sqlite3_bind_text(statement, 1, metricName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, window.c_str(), -1, SQLITE_TRANSIENT);
    while ((rc = sqlite3_step(statement)) == SQLITE_ROW) {
        values.push_back(sqlite3_column_double(statement, 0));
    }
    if (rc != SQLITE_DONE) {
        error = sqliteError(db_, "read disk baseline values");
        sqlite3_finalize(statement);
        return false;
    }
    sqlite3_finalize(statement);
    return true;
}

bool SharedMetricsStore::saveLearnedBaseline(const BaselineRecord& record,
                                              std::string& error) {
    const char* sql =
        "INSERT INTO baselines "
        "(metric_name, average_value, standard_deviation, last_updated, source) "
        "VALUES (?, ?, ?, datetime('now'), 'learned') "
        "ON CONFLICT(metric_name) DO UPDATE SET "
        "average_value=excluded.average_value, "
        "standard_deviation=excluded.standard_deviation, "
        "last_updated=excluded.last_updated, source='learned';";
    sqlite3_stmt* statement = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr);
    if (rc != SQLITE_OK) {
        error = sqliteError(db_, "prepare learned baseline upsert");
        return false;
    }
    sqlite3_bind_text(statement, 1, record.metricName.c_str(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_double(statement, 2, record.averageValue);
    sqlite3_bind_double(statement, 3, record.standardDeviation);
    rc = sqlite3_step(statement);
    if (rc != SQLITE_DONE) {
        error = sqliteError(db_, "save learned baseline");
        sqlite3_finalize(statement);
        return false;
    }
    sqlite3_finalize(statement);
    return true;
}

bool SharedMetricsStore::recomputeDiskBaselines(int windowDays, int minSamples,
                                                std::string& error) {
    return recomputeBaselines({kDiskQueueLength, kPercentDiskTime},
                              windowDays, minSamples, error);
}

bool SharedMetricsStore::recomputeSystemBaselines(int windowDays, int minSamples,
                                                  std::string& error) {
    return recomputeBaselines(
        {kCpuPercent, kAvailableRamMb, kPageFaultsPerSec,
         kDiskQueueLength, kPercentDiskTime},
        windowDays, minSamples, error);
}

bool SharedMetricsStore::recomputeBaselines(
    const std::vector<std::string>& metricNames, int windowDays, int minSamples,
    std::string& error) {
    if (!isOpen()) {
        error = "SQLite database is not open";
        return false;
    }
    if (windowDays < 1 || minSamples < 2) {
        error = "windowDays must be at least 1 and minSamples at least 2";
        return false;
    }
    sqlite3_stmt* ageStatement = nullptr;
    if (sqlite3_prepare_v2(db_,
            "SELECT COALESCE(julianday('now') - julianday(value),0) "
            "FROM iris_metadata WHERE key='first_metric_timestamp';",
            -1, &ageStatement, nullptr) != SQLITE_OK) {
        error = sqliteError(db_, "prepare baseline history age");
        return false;
    }
    const int ageRc = sqlite3_step(ageStatement);
    const double historyDays = ageRc == SQLITE_ROW ? sqlite3_column_double(ageStatement, 0) : 0.0;
    sqlite3_finalize(ageStatement);
    if (ageRc != SQLITE_ROW && ageRc != SQLITE_DONE) {
        error = sqliteError(db_, "read baseline history age");
        return false;
    }
    if (historyDays < static_cast<double>(kLearnedBaselineWarmupDays)) return true;

    std::vector<BaselineRecord> updates;
    for (const std::string& name : metricNames) {
        std::vector<double> values;
        if (!loadSystemValues(name, windowDays, values, error)) return false;
        if (values.size() < static_cast<std::size_t>(minSamples)) continue;
        double mean = 0.0;
        for (double value : values) mean += value;
        mean /= static_cast<double>(values.size());
        double squaredDifferences = 0.0;
        for (double value : values) {
            const double difference = value - mean;
            squaredDifferences += difference * difference;
        }
        BaselineRecord record;
        record.metricName = name;
        record.averageValue = mean;
        record.standardDeviation = std::sqrt(
            squaredDifferences / static_cast<double>(values.size() - 1));
        record.source = "learned";
        updates.push_back(record);
    }
    if (updates.empty()) return true;
    if (sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr)
        != SQLITE_OK) {
        error = sqliteError(db_, "begin baseline transaction");
        return false;
    }
    for (const BaselineRecord& record : updates) {
        if (!saveLearnedBaseline(record, error)) {
            sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }
    }
    if (sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        error = sqliteError(db_, "commit baseline transaction");
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }
    return true;
}

bool SharedMetricsStore::readBaseline(const std::string& metricName,
                                      BaselineRecord& out, bool& found,
                                      std::string& error) {
    found = false;
    if (!isOpen()) {
        error = "SQLite database is not open";
        return false;
    }
    const char* sql =
        "SELECT metric_name, average_value, standard_deviation, last_updated, source "
        "FROM baselines WHERE metric_name = ?;";
    sqlite3_stmt* statement = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr);
    if (rc != SQLITE_OK) {
        error = sqliteError(db_, "prepare baseline read");
        return false;
    }
    sqlite3_bind_text(statement, 1, metricName.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(statement);
    if (rc == SQLITE_ROW) {
        const unsigned char* metric = sqlite3_column_text(statement, 0);
        const unsigned char* updated = sqlite3_column_text(statement, 3);
        const unsigned char* source = sqlite3_column_text(statement, 4);
        out.metricName = metric ? reinterpret_cast<const char*>(metric) : "";
        out.averageValue = sqlite3_column_double(statement, 1);
        out.standardDeviation = sqlite3_column_double(statement, 2);
        out.lastUpdated = updated ? reinterpret_cast<const char*>(updated) : "";
        out.source = source ? reinterpret_cast<const char*>(source) : "";
        found = true;
    } else if (rc != SQLITE_DONE) {
        error = sqliteError(db_, "read baseline");
        sqlite3_finalize(statement);
        return false;
    }
    sqlite3_finalize(statement);
    return true;
}

bool SharedMetricsStore::readLatestMetric(const std::string& metricName,
                                          const std::string& processName,
                                          double& value, bool& found,
                                          std::string& error) {
    found = false;
    if (!isOpen()) {
        error = "SQLite database is not open";
        return false;
    }
    const char* systemSql =
        "SELECT value FROM live_metrics WHERE metric_name = ? "
        "AND process_name IS NULL ORDER BY timestamp DESC, id DESC LIMIT 1;";
    const char* processSql =
        "SELECT value FROM live_metrics WHERE metric_name = ? "
        "AND process_name = ? ORDER BY timestamp DESC, id DESC LIMIT 1;";
    sqlite3_stmt* statement = nullptr;
    const char* sql = processName.empty() ? systemSql : processSql;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr);
    if (rc != SQLITE_OK) {
        error = sqliteError(db_, "prepare latest metric read");
        return false;
    }
    sqlite3_bind_text(statement, 1, metricName.c_str(), -1, SQLITE_TRANSIENT);
    if (!processName.empty()) {
        sqlite3_bind_text(statement, 2, processName.c_str(), -1, SQLITE_TRANSIENT);
    }
    rc = sqlite3_step(statement);
    if (rc == SQLITE_ROW) {
        value = sqlite3_column_double(statement, 0);
        found = true;
    } else if (rc != SQLITE_DONE) {
        error = sqliteError(db_, "read latest metric");
        sqlite3_finalize(statement);
        return false;
    }
    sqlite3_finalize(statement);
    return true;
}

bool SharedMetricsStore::beginDiagnosticPeriod(const std::string& ruleName,
                                                std::string& error) {
    if (!isOpen() || ruleName.empty()) {
        error = "SQLite database must be open and a rule name must be provided";
        return false;
    }
    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "INSERT INTO diagnostic_periods(rule_name, started_at) "
        "VALUES (?, datetime('now'));";
    if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = sqliteError(db_, "prepare diagnostic period start");
        return false;
    }
    sqlite3_bind_text(statement, 1, ruleName.c_str(), -1, SQLITE_TRANSIENT);
    const int rc = sqlite3_step(statement);
    sqlite3_finalize(statement);
    if (rc != SQLITE_DONE) {
        error = sqliteError(db_, "start diagnostic period");
        return false;
    }
    return true;
}

bool SharedMetricsStore::endDiagnosticPeriod(std::string& error) {
    if (!isOpen()) {
        error = "SQLite database is not open";
        return false;
    }
    const int rc = sqlite3_exec(db_,
        "UPDATE diagnostic_periods SET ended_at=datetime('now') WHERE ended_at IS NULL;",
        nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        error = sqliteError(db_, "end diagnostic period");
        return false;
    }
    return true;
}

bool SharedMetricsStore::recordDiagnosticEvent(
    const std::string& ruleName, const std::string& metricValuesJson,
    std::string& error) {
    if (!isOpen()) {
        error = "SQLite database is not open";
        return false;
    }
    const char* sql =
        "INSERT INTO diagnostic_events(timestamp, rule_matched, metric_values) "
        "VALUES(datetime('now'), ?, ?);";
    sqlite3_stmt* statement = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr);
    if (rc != SQLITE_OK) {
        error = sqliteError(db_, "prepare diagnostic event insert");
        return false;
    }
    sqlite3_bind_text(statement, 1, ruleName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, metricValuesJson.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(statement);
    if (rc != SQLITE_DONE) {
        error = sqliteError(db_, "insert diagnostic event");
        sqlite3_finalize(statement);
        return false;
    }
    sqlite3_finalize(statement);
    return true;
}

bool SharedMetricsStore::bootstrapDefaultBaselines(double totalRamMb,
                                                    std::string& error) {
    if (!isOpen() || !std::isfinite(totalRamMb) || totalRamMb <= 0.0) {
        error = "Cannot create default baselines without a valid total RAM value";
        return false;
    }

    struct Defaults {
        const char* metric;
        double mean;
        double deviation;
    };
    const double totalRamGb = totalRamMb / 1024.0;
    double cpuMean = 15.0, cpuStdDev = 12.0;
    double faultsMean = 45.0, faultsStdDev = 40.0;
    double queueMean = 0.20, queueStdDev = 0.30;
    double diskTimeMean = 10.0, diskTimeStdDev = 15.0;
    if (totalRamGb <= 4.0) {
        faultsMean = 60.0; faultsStdDev = 50.0;
        queueMean = 0.25; queueStdDev = 0.35;
        diskTimeMean = 12.0; diskTimeStdDev = 18.0;
    } else if (totalRamGb > 16.0) {
        cpuMean = 12.0; cpuStdDev = 10.0;
        faultsMean = 35.0; faultsStdDev = 30.0;
        queueMean = 0.15; queueStdDev = 0.25;
        diskTimeMean = 8.0; diskTimeStdDev = 12.0;
    }
    const Defaults defaults[] = {
        {kCpuPercent, cpuMean, cpuStdDev},
        {kAvailableRamMb, totalRamMb * 0.35, totalRamMb * 0.15},
        {kPageFaultsPerSec, faultsMean, faultsStdDev},
        {kDiskQueueLength, queueMean, queueStdDev},
        {kPercentDiskTime, diskTimeMean, diskTimeStdDev},
    };
    const char* sql =
        "INSERT INTO baselines "
        "(metric_name, average_value, standard_deviation, last_updated, source) "
        "VALUES (?, ?, ?, CURRENT_TIMESTAMP, 'default') "
        "ON CONFLICT(metric_name) DO UPDATE SET "
        "average_value=excluded.average_value, "
        "standard_deviation=excluded.standard_deviation, "
        "last_updated=excluded.last_updated, source='default' "
        "WHERE COALESCE((SELECT julianday('now')-julianday(value) "
        "FROM iris_metadata WHERE key='first_metric_timestamp'),0) < 7;";
    sqlite3_stmt* statement = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr);
    if (rc != SQLITE_OK) {
        error = sqliteError(db_, "prepare default baseline insert");
        return false;
    }
    if (sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        error = sqliteError(db_, "begin default baseline transaction");
        sqlite3_finalize(statement);
        return false;
    }
    for (const Defaults& baseline : defaults) {
        sqlite3_reset(statement);
        sqlite3_clear_bindings(statement);
        sqlite3_bind_text(statement, 1, baseline.metric, -1, SQLITE_STATIC);
        sqlite3_bind_double(statement, 2, baseline.mean);
        sqlite3_bind_double(statement, 3, baseline.deviation);
        if (sqlite3_step(statement) != SQLITE_DONE) {
            error = sqliteError(db_, "insert default baseline");
            sqlite3_finalize(statement);
            sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }
    }
    sqlite3_finalize(statement);
    if (sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        error = sqliteError(db_, "commit default baseline transaction");
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }
    return true;
}

bool SharedMetricsStore::recordStartupApps(
    const std::vector<StartupAppRecord>& apps, std::string& error) {
    if (!isOpen()) {
        error = "SQLite database is not open";
        return false;
    }
    if (apps.empty()) return true;
    const char* sql =
        "INSERT INTO startup_apps (timestamp, app_name, registry_location) "
        "VALUES (CURRENT_TIMESTAMP, ?, ?);";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = sqliteError(db_, "prepare startup app insert");
        return false;
    }
    if (sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        error = sqliteError(db_, "begin startup app transaction");
        sqlite3_finalize(statement);
        return false;
    }
    for (const StartupAppRecord& app : apps) {
        sqlite3_reset(statement);
        sqlite3_clear_bindings(statement);
        sqlite3_bind_text(statement, 1, app.appName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 2, app.registryLocation.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(statement) != SQLITE_DONE) {
            error = sqliteError(db_, "insert startup app");
            sqlite3_finalize(statement);
            sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }
    }
    sqlite3_finalize(statement);
    if (sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        error = sqliteError(db_, "commit startup app transaction");
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }
    return true;
}

void SharedMetricsStore::close() {
    if (insertStmt_ != nullptr) {
        sqlite3_finalize(insertStmt_);
        insertStmt_ = nullptr;
    }
    if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}
} // namespace iris
