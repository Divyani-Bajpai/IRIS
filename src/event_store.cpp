#include "event_store.h"
#include "rule_engine.h"
#include "sqlite3.h"

using namespace std;

namespace
{

string sqliteError(sqlite3 *db, const char *what)
{
    string text = what;
    text += ": ";
    text += (db != nullptr) ? sqlite3_errmsg(db) : "no database handle";
    return text;
}

struct RuleDefinition
{
    const char *name;
    const char *logic;
    const char *recommendation;
};

// The two rules required by the project.
const RuleDefinition RULES[] = {
    {RULE_MEMORY_THRASHING,
     "page_faults_per_sec>+2.0 AND available_ram_mb<-2.0 AND cpu_percent between -1.0 and +1.0",
     "Memory Thrashing detected. Your PC does not have enough free memory for what "
     "is currently running, so it keeps moving data between RAM and the disk. "
     "Close programs and browser tabs you are not using. If this happens often, "
     "adding more RAM will help."},

    {RULE_DISK_IO_CONTENTION,
     "disk_queue_length>+2.0 AND percent_disk_time>+2.0 AND available_ram_mb between -1.0 and +1.0",
     "Disk I/O Contention detected. Something is heavily using your disk right now. "
     "Check for background downloads, backups, antivirus scans or Windows updates "
     "and pause them. Moving to an SSD also helps a lot."}};

} // namespace

EventStore::EventStore() {}

EventStore::~EventStore()
{
    close();
}

bool EventStore::open(const string &dbPath, string &error)
{
    close();

    if (sqlite3_open(dbPath.c_str(), &db_) != SQLITE_OK)
    {
        error = sqliteError(db_, "sqlite3_open");
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }

    // Same settings as the other modules.
    sqlite3_busy_timeout(db_, 3000);
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    if (!createTables(error) || !seedRules(error))
    {
        close();
        return false;
    }
    return true;
}

bool EventStore::createTables(string &error)
{
    const char *sql =
        "CREATE TABLE IF NOT EXISTS diagnostic_rules ("
        " rule_name TEXT PRIMARY KEY,"
        " condition_logic TEXT,"
        " recommendation_text TEXT"
        ");"
        "CREATE TABLE IF NOT EXISTS diagnostic_events ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " timestamp TEXT NOT NULL,"        // UTC, YYYY-MM-DD HH:MM:SS
        " rule_matched TEXT NOT NULL,"
        " metric_values TEXT"              // evidence as JSON text
        ");";

    char *message = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &message) != SQLITE_OK)
    {
        error = string("create tables: ") + (message ? message : "unknown error");
        sqlite3_free(message);
        return false;
    }
    return true;
}

bool EventStore::seedRules(string &error)
{
    const char *sql =
        "INSERT OR IGNORE INTO diagnostic_rules "
        "(rule_name, condition_logic, recommendation_text) VALUES (?, ?, ?);";

    for (const RuleDefinition &rule : RULES)
    {
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            error = sqliteError(db_, "prepare seed rule");
            return false;
        }

        sqlite3_bind_text(stmt, 1, rule.name, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, rule.logic, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, rule.recommendation, -1, SQLITE_TRANSIENT);

        bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        if (!ok)
            error = sqliteError(db_, "seed rule");
        sqlite3_finalize(stmt);

        if (!ok)
            return false;
    }
    return true;
}

bool EventStore::saveEvent(const string &ruleName, const string &evidenceJson, string &error)
{
    if (db_ == nullptr)
    {
        error = "Database is not open";
        return false;
    }

    const char *sql =
        "INSERT INTO diagnostic_events (timestamp, rule_matched, metric_values) "
        "VALUES (datetime('now'), ?, ?);";   // datetime('now') is UTC

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        error = sqliteError(db_, "prepare save event");
        return false;
    }

    sqlite3_bind_text(stmt, 1, ruleName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, evidenceJson.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    if (!ok)
        error = sqliteError(db_, "save event");
    sqlite3_finalize(stmt);
    return ok;
}

bool EventStore::getRecommendation(const string &ruleName, string &text, string &error)
{
    text.clear();
    if (db_ == nullptr)
    {
        error = "Database is not open";
        return false;
    }

    const char *sql = "SELECT recommendation_text FROM diagnostic_rules WHERE rule_name = ?;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        error = sqliteError(db_, "prepare get recommendation");
        return false;
    }

    sqlite3_bind_text(stmt, 1, ruleName.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW)
    {
        const unsigned char *value = sqlite3_column_text(stmt, 0);
        text = value ? reinterpret_cast<const char *>(value) : "";
    }
    bool ok = (rc == SQLITE_ROW || rc == SQLITE_DONE);
    if (!ok)
        error = sqliteError(db_, "get recommendation");
    sqlite3_finalize(stmt);
    return ok;
}

bool EventStore::getLatestEvent(string &ruleName, string &evidenceJson, string &error)
{
    ruleName.clear();
    evidenceJson.clear();
    if (db_ == nullptr)
    {
        error = "Database is not open";
        return false;
    }

    const char *sql =
        "SELECT rule_matched, metric_values FROM diagnostic_events "
        "ORDER BY timestamp DESC, id DESC LIMIT 1;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        error = sqliteError(db_, "prepare latest event");
        return false;
    }

    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW)
    {
        const unsigned char *rule = sqlite3_column_text(stmt, 0);
        const unsigned char *evidence = sqlite3_column_text(stmt, 1);
        ruleName = rule ? reinterpret_cast<const char *>(rule) : "";
        evidenceJson = evidence ? reinterpret_cast<const char *>(evidence) : "";
    }
    bool ok = (rc == SQLITE_ROW || rc == SQLITE_DONE);
    if (!ok)
        error = sqliteError(db_, "read latest event");
    sqlite3_finalize(stmt);
    return ok;
}

bool EventStore::countRows(const char *sql, long long &count, string &error)
{
    count = 0;
    if (db_ == nullptr)
    {
        error = "Database is not open";
        return false;
    }

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        error = sqliteError(db_, "prepare count");
        return false;
    }

    bool ok = (sqlite3_step(stmt) == SQLITE_ROW);
    if (ok)
        count = sqlite3_column_int64(stmt, 0);
    else
        error = sqliteError(db_, "count rows");
    sqlite3_finalize(stmt);
    return ok;
}

bool EventStore::countEvents(long long &count, string &error)
{
    return countRows("SELECT COUNT(*) FROM diagnostic_events;", count, error);
}

bool EventStore::countRules(long long &count, string &error)
{
    return countRows("SELECT COUNT(*) FROM diagnostic_rules;", count, error);
}

void EventStore::close()
{
    if (db_ != nullptr)
    {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}
