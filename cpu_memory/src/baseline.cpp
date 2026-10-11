#include "baseline.h"

#include <cmath>

#include "sqlite3.h"

namespace iris {

namespace {

std::string sqliteError(sqlite3* db, const char* what) {
    std::string text = what;
    text += ": ";
    text += (db != nullptr) ? sqlite3_errmsg(db) : "no database handle";
    return text;
}

}  // namespace

BaselineStats computeBaselineStats(const std::string& metricName,
                                   const std::vector<double>& values,
                                   int minSamples) {
    BaselineStats stats;
    stats.metricName = metricName;
    stats.sampleCount = static_cast<int>(values.size());

    if (values.empty()) {
        return stats;
    }

    double sum = 0.0;
    for (double v : values) {
        sum += v;
    }
    const double n = static_cast<double>(values.size());
    stats.mean = sum / n;

    // Standard deviation needs at least two values.
    if (values.size() >= 2) {
        double squares = 0.0;
        for (double v : values) {
            const double diff = v - stats.mean;
            squares += diff * diff;
        }
        stats.stdDev = std::sqrt(squares / (n - 1.0));
    }

    stats.ready = (values.size() >= 2 && stats.sampleCount >= minSamples);
    return stats;
}

CpuMemoryBaseline::CpuMemoryBaseline() {}

CpuMemoryBaseline::~CpuMemoryBaseline() {
    close();
}

const std::vector<std::string>& CpuMemoryBaseline::metricNames() {
    static const std::vector<std::string> names = {
        "cpu_percent",
        "ram_used_percent",
        "ram_available_mb",
        "committed_percent",
        "page_faults_per_sec"
    };
    return names;
}

bool CpuMemoryBaseline::open(const std::string& dbPath, std::string& error) {
    close();

    int rc = sqlite3_open(dbPath.c_str(), &db_);
    if (rc != SQLITE_OK) {
        error = sqliteError(db_, "sqlite3_open");
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }

    sqlite3_busy_timeout(db_, 3000);
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    if (!createSchema(error)) {
        close();
        return false;
    }
    return true;
}

bool CpuMemoryBaseline::createSchema(std::string& error) {
    // The readings table is created here too, so this works on a fresh file.
    const char* sql =
        "CREATE TABLE IF NOT EXISTS cpu_mem_readings ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " timestamp TEXT NOT NULL,"
        " cpu_percent REAL,"
        " ram_total_mb REAL,"
        " ram_available_mb REAL,"
        " ram_used_percent REAL,"
        " committed_percent REAL,"
        " page_faults_per_sec REAL"
        ");"
        "CREATE TABLE IF NOT EXISTS metric_baseline ("
        " metric_name TEXT PRIMARY KEY,"
        " mean REAL,"                  // NULL when there are no values
        " std_dev REAL,"               // NULL when there are fewer than 2 values
        " sample_count INTEGER NOT NULL,"
        " min_samples INTEGER NOT NULL,"
        " ready INTEGER NOT NULL,"     // 1 = learned baseline can be used
        " window_days INTEGER NOT NULL,"
        " updated_at TEXT NOT NULL"    // UTC
        ");";

    char* message = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &message);
    if (rc != SQLITE_OK) {
        error = std::string("create schema: ") + (message ? message : "unknown error");
        sqlite3_free(message);
        return false;
    }
    return true;
}

bool CpuMemoryBaseline::loadValues(const std::string& metricName, int windowDays,
                                   std::vector<double>& values, std::string& error) {
    values.clear();

    // metricName comes from our own fixed list, never from user input.
    std::string sql = "SELECT " + metricName + " FROM cpu_mem_readings WHERE " +
                      metricName + " IS NOT NULL AND timestamp >= datetime('now', ?);";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        error = sqliteError(db_, "prepare load values");
        return false;
    }

    std::string window = "-" + std::to_string(windowDays) + " days";
    sqlite3_bind_text(stmt, 1, window.c_str(), -1, SQLITE_TRANSIENT);

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        values.push_back(sqlite3_column_double(stmt, 0));
    }

    bool ok = (rc == SQLITE_DONE);
    if (!ok) {
        error = sqliteError(db_, "read values");
    }
    sqlite3_finalize(stmt);
    return ok;
}

bool CpuMemoryBaseline::saveStats(const BaselineStats& stats, int minSamples,
                                  int windowDays, std::string& error) {
    const char* sql =
        "INSERT OR REPLACE INTO metric_baseline "
        "(metric_name, mean, std_dev, sample_count, min_samples, ready, "
        " window_days, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, datetime('now'));";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        error = sqliteError(db_, "prepare save baseline");
        return false;
    }

    sqlite3_bind_text(stmt, 1, stats.metricName.c_str(), -1, SQLITE_TRANSIENT);
    if (stats.sampleCount >= 1) {
        sqlite3_bind_double(stmt, 2, stats.mean);
    } else {
        sqlite3_bind_null(stmt, 2);
    }
    if (stats.sampleCount >= 2) {
        sqlite3_bind_double(stmt, 3, stats.stdDev);
    } else {
        sqlite3_bind_null(stmt, 3);
    }
    sqlite3_bind_int(stmt, 4, stats.sampleCount);
    sqlite3_bind_int(stmt, 5, minSamples);
    sqlite3_bind_int(stmt, 6, stats.ready ? 1 : 0);
    sqlite3_bind_int(stmt, 7, windowDays);

    rc = sqlite3_step(stmt);
    bool ok = (rc == SQLITE_DONE);
    if (!ok) {
        error = sqliteError(db_, "save baseline");
    }
    sqlite3_finalize(stmt);
    return ok;
}

bool CpuMemoryBaseline::recompute(int windowDays, int minSamples, std::string& error) {
    if (!isOpen()) {
        error = "Database is not open";
        return false;
    }
    if (windowDays < 1 || minSamples < 2) {
        error = "windowDays must be at least 1 and minSamples at least 2";
        return false;
    }

    // Work everything out first, then save all metrics in one transaction.
    std::vector<BaselineStats> results;
    for (const std::string& name : metricNames()) {
        std::vector<double> values;
        if (!loadValues(name, windowDays, values, error)) {
            return false;
        }
        results.push_back(computeBaselineStats(name, values, minSamples));
    }

    if (sqlite3_exec(db_, "BEGIN;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        error = sqliteError(db_, "begin transaction");
        return false;
    }
    for (const BaselineStats& stats : results) {
        if (!saveStats(stats, minSamples, windowDays, error)) {
            sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }
    }
    if (sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        error = sqliteError(db_, "commit");
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }
    return true;
}

bool CpuMemoryBaseline::getBaseline(const std::string& metricName,
                                    BaselineStats& out, std::string& error) {
    out = BaselineStats();
    out.metricName = metricName;

    if (!isOpen()) {
        error = "Database is not open";
        return false;
    }

    const char* sql =
        "SELECT mean, std_dev, sample_count, ready FROM metric_baseline "
        "WHERE metric_name = ?;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        error = sqliteError(db_, "prepare get baseline");
        return false;
    }
    sqlite3_bind_text(stmt, 1, metricName.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        if (sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
            out.mean = sqlite3_column_double(stmt, 0);
        }
        if (sqlite3_column_type(stmt, 1) != SQLITE_NULL) {
            out.stdDev = sqlite3_column_double(stmt, 1);
        }
        out.sampleCount = sqlite3_column_int(stmt, 2);
        out.ready = (sqlite3_column_int(stmt, 3) != 0);
    } else if (rc != SQLITE_DONE) {
        error = sqliteError(db_, "get baseline");
        sqlite3_finalize(stmt);
        return false;
    }
    // SQLITE_DONE means no row yet: we return ready = false, count 0.

    sqlite3_finalize(stmt);
    return true;
}

bool CpuMemoryBaseline::getAllBaselines(std::vector<BaselineStats>& out,
                                        std::string& error) {
    out.clear();
    for (const std::string& name : metricNames()) {
        BaselineStats stats;
        if (!getBaseline(name, stats, error)) {
            return false;
        }
        out.push_back(stats);
    }
    return true;
}

void CpuMemoryBaseline::close() {
    if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

}  // namespace iris