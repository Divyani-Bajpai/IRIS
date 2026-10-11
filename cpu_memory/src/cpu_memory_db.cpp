#include "cpu_memory_db.h"

#include "sqlite3.h"

namespace iris {

namespace {

std::string sqliteError(sqlite3* db, const char* what) {
    std::string text = what;
    text += ": ";
    text += (db != nullptr) ? sqlite3_errmsg(db) : "no database handle";
    return text;
}

// Binds a number if the metric is valid, otherwise binds NULL.
void bindMetric(sqlite3_stmt* stmt, int index, bool valid, double value) {
    if (valid) {
        sqlite3_bind_double(stmt, index, value);
    } else {
        sqlite3_bind_null(stmt, index);
    }
}

// Reads a column. NULL in the database means the metric was invalid.
void readMetric(sqlite3_stmt* stmt, int column, bool& valid, double& value) {
    if (sqlite3_column_type(stmt, column) == SQLITE_NULL) {
        valid = false;
        value = 0.0;
    } else {
        valid = true;
        value = sqlite3_column_double(stmt, column);
    }
}

}  // namespace

CpuMemoryDb::CpuMemoryDb() {}

CpuMemoryDb::~CpuMemoryDb() {
    close();
}

bool CpuMemoryDb::open(const std::string& dbPath, std::string& error) {
    close();

    int rc = sqlite3_open(dbPath.c_str(), &db_);
    if (rc != SQLITE_OK) {
        error = sqliteError(db_, "sqlite3_open");
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }

    // Wait a little if another module is writing at the same time.
    sqlite3_busy_timeout(db_, 3000);
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    if (!createSchema(error)) {
        close();
        return false;
    }

    const char* insertSql =
        "INSERT INTO cpu_mem_readings "
        "(timestamp, cpu_percent, ram_total_mb, ram_available_mb, "
        " ram_used_percent, committed_percent, page_faults_per_sec) "
        "VALUES (?, ?, ?, ?, ?, ?, ?);";
    rc = sqlite3_prepare_v2(db_, insertSql, -1, &insertStmt_, nullptr);
    if (rc != SQLITE_OK) {
        error = sqliteError(db_, "prepare insert");
        insertStmt_ = nullptr;
        close();
        return false;
    }
    return true;
}

bool CpuMemoryDb::createSchema(std::string& error) {
    const char* sql =
        "CREATE TABLE IF NOT EXISTS cpu_mem_readings ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " timestamp TEXT NOT NULL,"            // UTC, YYYY-MM-DD HH:MM:SS
        " cpu_percent REAL,"                   // 0-100
        " ram_total_mb REAL,"                  // installed physical RAM, MB
        " ram_available_mb REAL,"              // free physical RAM, MB
        " ram_used_percent REAL,"              // physical RAM used, 0-100
        " committed_percent REAL,"             // committed virtual memory, %
        " page_faults_per_sec REAL"            // faults per second (rate)
        ");"
        "CREATE INDEX IF NOT EXISTS idx_cpu_mem_readings_time "
        "ON cpu_mem_readings(timestamp);";

    char* message = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &message);
    if (rc != SQLITE_OK) {
        error = std::string("create schema: ") + (message ? message : "unknown error");
        sqlite3_free(message);
        return false;
    }
    return true;
}

bool CpuMemoryDb::insertReading(const CpuMemReading& r, std::string& error) {
    if (!isOpen() || insertStmt_ == nullptr) {
        error = "Database is not open";
        return false;
    }

    // Do not save a row that has nothing valid in it.
    if (!r.cpuValid && !r.ramValid && !r.committedValid && !r.pageFaultsValid) {
        error = "Reading has no valid metric, not saved";
        return false;
    }

    sqlite3_reset(insertStmt_);
    sqlite3_clear_bindings(insertStmt_);

    sqlite3_bind_text(insertStmt_, 1, r.timestamp.c_str(), -1, SQLITE_TRANSIENT);
    bindMetric(insertStmt_, 2, r.cpuValid, r.cpuPercent);
    bindMetric(insertStmt_, 3, r.ramValid, r.ramTotalMB);
    bindMetric(insertStmt_, 4, r.ramValid, r.ramAvailableMB);
    bindMetric(insertStmt_, 5, r.ramValid, r.ramUsedPercent);
    bindMetric(insertStmt_, 6, r.committedValid, r.committedPercent);
    bindMetric(insertStmt_, 7, r.pageFaultsValid, r.pageFaultsPerSec);

    int rc = sqlite3_step(insertStmt_);
    sqlite3_reset(insertStmt_);
    if (rc != SQLITE_DONE) {
        error = sqliteError(db_, "insert reading");
        return false;
    }
    return true;
}

bool CpuMemoryDb::countReadings(long long& count, std::string& error) {
    if (!isOpen()) {
        error = "Database is not open";
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM cpu_mem_readings;",
                                -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        error = sqliteError(db_, "prepare count");
        return false;
    }

    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int64(stmt, 0);
        ok = true;
    } else {
        error = sqliteError(db_, "count readings");
    }
    sqlite3_finalize(stmt);
    return ok;
}

bool CpuMemoryDb::getReadingsSince(const std::string& sinceTimestamp,
                                   std::vector<CpuMemReading>& out,
                                   std::string& error) {
    out.clear();
    if (!isOpen()) {
        error = "Database is not open";
        return false;
    }

    const char* sql =
        "SELECT timestamp, cpu_percent, ram_total_mb, ram_available_mb, "
        "       ram_used_percent, committed_percent, page_faults_per_sec "
        "FROM cpu_mem_readings WHERE timestamp >= ? ORDER BY timestamp;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        error = sqliteError(db_, "prepare select");
        return false;
    }
    sqlite3_bind_text(stmt, 1, sinceTimestamp.c_str(), -1, SQLITE_TRANSIENT);

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        CpuMemReading r;
        const unsigned char* ts = sqlite3_column_text(stmt, 0);
        r.timestamp = ts ? reinterpret_cast<const char*>(ts) : "";

        readMetric(stmt, 1, r.cpuValid, r.cpuPercent);

        // RAM is valid only if all three RAM columns are present.
        bool totalOk = false, availOk = false, usedOk = false;
        readMetric(stmt, 2, totalOk, r.ramTotalMB);
        readMetric(stmt, 3, availOk, r.ramAvailableMB);
        readMetric(stmt, 4, usedOk, r.ramUsedPercent);
        r.ramValid = totalOk && availOk && usedOk;

        readMetric(stmt, 5, r.committedValid, r.committedPercent);
        readMetric(stmt, 6, r.pageFaultsValid, r.pageFaultsPerSec);

        out.push_back(r);
    }

    bool ok = (rc == SQLITE_DONE);
    if (!ok) {
        error = sqliteError(db_, "read readings");
    }
    sqlite3_finalize(stmt);
    return ok;
}

void CpuMemoryDb::close() {
    // Statement must be finalized before the connection is closed.
    if (insertStmt_ != nullptr) {
        sqlite3_finalize(insertStmt_);
        insertStmt_ = nullptr;
    }
    if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

}  // namespace iris