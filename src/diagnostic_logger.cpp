#include <iostream>
#include "diagnostic_logger.h"
#include "sqlite3.h"

using namespace std;

void logDiagnosis(const RuleResult &result)
{
    cout << "\n--- Diagnostic Event ---" << endl;
    cout << "Rule Name : " << result.ruleName << endl;
    cout << "Diagnosis : " << result.diagnosis << endl;
    cout << "Reason : " << result.reason << endl;
    cout << "Rule Matched : " << (result.matched ? "Yes" : "No") << endl;
}

bool saveDiagnosis(const string &dbPath, const RuleResult &result, const MetricScores &scores, string &error)
{
    sqlite3 *db = nullptr;
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK)
    {
        error = "Could not open database: ";
        error += sqlite3_errmsg(db);
        sqlite3_close(db);
        return false;
    }

    sqlite3_busy_timeout(db, 3000);

    // Create the table if it does not exist yet.
    const char *createSql =
        "CREATE TABLE IF NOT EXISTS diagnosis_log ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " timestamp TEXT NOT NULL DEFAULT (datetime('now')),"
        " rule_name TEXT NOT NULL,"
        " diagnosis TEXT,"
        " reason TEXT,"
        " page_faults_z REAL,"
        " available_ram_z REAL,"
        " cpu_z REAL,"
        " disk_queue_z REAL,"
        " disk_time_z REAL"
        ");";

    char *errMsg = nullptr;
    if (sqlite3_exec(db, createSql, nullptr, nullptr, &errMsg) != SQLITE_OK)
    {
        error = string("create table: ") + (errMsg ? errMsg : "unknown error");
        sqlite3_free(errMsg);
        sqlite3_close(db);
        return false;
    }

    const char *insertSql =
        "INSERT INTO diagnosis_log "
        "(rule_name, diagnosis, reason, page_faults_z, available_ram_z, cpu_z, disk_queue_z, disk_time_z) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, insertSql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        error = string("prepare insert: ") + sqlite3_errmsg(db);
        sqlite3_close(db);
        return false;
    }

    sqlite3_bind_text(stmt, 1, result.ruleName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, result.diagnosis.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, result.reason.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 4, scores.pageFaultsZ);
    sqlite3_bind_double(stmt, 5, scores.availableRamZ);
    sqlite3_bind_double(stmt, 6, scores.cpuZ);
    sqlite3_bind_double(stmt, 7, scores.diskQueueZ);
    sqlite3_bind_double(stmt, 8, scores.diskTimeZ);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    if (!ok)
        error = string("insert: ") + sqlite3_errmsg(db);

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return ok;
}
