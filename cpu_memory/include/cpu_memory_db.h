#pragma once

#include <string>
#include <vector>

#include "metric_types.h"

// Forward declarations, so SQLite's header stays out of this file.
struct sqlite3;
struct sqlite3_stmt;

namespace iris {

// Saves and loads CPU / memory readings in a SQLite file.
// Metrics that were invalid are stored as NULL, never as 0.
class CpuMemoryDb {
public:
    CpuMemoryDb();
    ~CpuMemoryDb();

    CpuMemoryDb(const CpuMemoryDb&) = delete;
    CpuMemoryDb& operator=(const CpuMemoryDb&) = delete;

    // Opens (or creates) the database file and makes sure the table exists.
    bool open(const std::string& dbPath, std::string& error);

    // Inserts one reading. Fails if the reading has no valid metric at all.
    bool insertReading(const CpuMemReading& reading, std::string& error);

    // Total number of rows in cpu_mem_readings.
    bool countReadings(long long& count, std::string& error);

    // Loads readings with timestamp >= sinceTimestamp ("YYYY-MM-DD HH:MM:SS", UTC),
    // oldest first. Used by the baseline code in Stage 3.
    bool getReadingsSince(const std::string& sinceTimestamp,
                          std::vector<CpuMemReading>& out,
                          std::string& error);

    // Safe to call more than once.
    void close();

    bool isOpen() const { return db_ != nullptr; }

private:
    bool createSchema(std::string& error);

    sqlite3*      db_ = nullptr;
    sqlite3_stmt* insertStmt_ = nullptr;
};

}  // namespace iris