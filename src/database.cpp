#include "database.h"

#include <stdexcept>

namespace iris {

Statement::Statement(sqlite3* database, const char* sql) : database_(database) {
    if (!database_) throw std::invalid_argument("Database connection is null.");
    const int result = sqlite3_prepare_v2(database_, sql, -1, &statement_, nullptr);
    if (result != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(database_));
}

Statement::~Statement() { sqlite3_finalize(statement_); }

void Statement::execute() {
    const int result = sqlite3_step(statement_);
    if (result != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
    sqlite3_reset(statement_);
    sqlite3_clear_bindings(statement_);
}

void executeSql(sqlite3* database, const char* sql) {
    char* error = nullptr;
    if (sqlite3_exec(database, sql, nullptr, nullptr, &error) != SQLITE_OK) {
        const std::string message = error ? error : sqlite3_errmsg(database);
        sqlite3_free(error);
        throw std::runtime_error(message);
    }
}

void createSharedSchema(sqlite3* database) {
    // Foreign-key enforcement is connection-specific in SQLite.
    executeSql(database,
        "PRAGMA foreign_keys = ON;"
        "CREATE TABLE IF NOT EXISTS live_metrics ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp DATETIME NOT NULL,"
        " metric_name TEXT NOT NULL, value REAL NOT NULL, process_name TEXT);"
        "CREATE TABLE IF NOT EXISTS baselines ("
        " metric_name TEXT PRIMARY KEY, average_value REAL NOT NULL,"
        " standard_deviation REAL NOT NULL, last_updated DATETIME NOT NULL,"
        " source TEXT NOT NULL DEFAULT 'default');"
        "CREATE TABLE IF NOT EXISTS diagnostic_rules ("
        " rule_name TEXT PRIMARY KEY, condition_logic TEXT NOT NULL,"
        " recommendation_text TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS diagnostic_events ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp DATETIME NOT NULL,"
        " rule_matched TEXT NOT NULL, metric_values TEXT,"
        " FOREIGN KEY (rule_matched) REFERENCES diagnostic_rules(rule_name));"
        "CREATE TABLE IF NOT EXISTS startup_apps ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp DATETIME NOT NULL,"
        " app_name TEXT NOT NULL, registry_location TEXT);"
        "CREATE TABLE IF NOT EXISTS boot_times ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp DATETIME NOT NULL,"
        " boot_duration_seconds REAL NOT NULL);");
}

} // namespace iris
