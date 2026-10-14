#ifndef EVENT_STORE_H
#define EVENT_STORE_H

#include <string>

struct sqlite3;

// Handles the two tables owned by the rule engine:
//   diagnostic_rules  - the rule names, their logic and the advice text
//   diagnostic_events - one row each time a rule matches
class EventStore
{
public:
    EventStore();
    ~EventStore();

    EventStore(const EventStore &) = delete;
    EventStore &operator=(const EventStore &) = delete;

    // Opens the database, creates the tables if missing and adds the two
    // rules (safe to call again and again: no duplicates).
    bool open(const std::string &dbPath, std::string &error);

    // Saves one matched rule with its evidence (JSON text).
    bool saveEvent(const std::string &ruleName, const std::string &evidenceJson,
                   std::string &error);

    // Advice text of a rule (used by the UI and the console).
    bool getRecommendation(const std::string &ruleName, std::string &text,
                           std::string &error);

    // Newest event (for the UI). Returns true with empty strings if none.
    bool getLatestEvent(std::string &ruleName, std::string &evidenceJson,
                        std::string &error);

    bool countEvents(long long &count, std::string &error);
    bool countRules(long long &count, std::string &error);

    void close();

private:
    bool createTables(std::string &error);
    bool seedRules(std::string &error);
    bool countRows(const char *sql, long long &count, std::string &error);

    sqlite3 *db_ = nullptr;
};

#endif
