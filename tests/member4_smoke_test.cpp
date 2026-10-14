#include "database.h"
#include "dashboard.h"

#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void bindTextOrThrow(sqlite3* db, sqlite3_stmt* stmt, int index, const std::string& value) {
    if (sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(db));
    }
}

void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    sqlite3* db = nullptr;
    if (sqlite3_open(":memory:", &db) != SQLITE_OK) {
        std::cerr << "Could not open in-memory SQLite database.\n";
        if (db) sqlite3_close(db);
        return 1;
    }

    try {
        iris::createSharedSchema(db);

        // Schema smoke check: all six shared tables must exist.
        iris::Statement tables(db,
            "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name IN "
            "('live_metrics','baselines','diagnostic_rules','diagnostic_events','startup_apps','boot_times');");
        expect(sqlite3_step(tables.get()) == SQLITE_ROW, "Could not query shared schema.");
        expect(sqlite3_column_int(tables.get(), 0) == 6, "One or more shared tables are missing.");

        const auto emptyDiagnosis = iris::getLatestDiagnosis(db);
        expect(!emptyDiagnosis.active, "An empty database should have no active diagnosis.");

        // A recent event with a configured rule should resolve its recommendation.
        iris::Statement rule(db,
            "INSERT INTO diagnostic_rules(rule_name, condition_logic, recommendation_text) VALUES(?,?,?);");
        bindTextOrThrow(db, rule.get(), 1, "disk_io_contention");
        bindTextOrThrow(db, rule.get(), 2, "test condition");
        bindTextOrThrow(db, rule.get(), 3, "Review disk activity.");
        rule.execute();

        iris::Statement event(db,
            "INSERT INTO diagnostic_events(timestamp, rule_matched, metric_values) VALUES(CURRENT_TIMESTAMP,?,?);");
        bindTextOrThrow(db, event.get(), 1, "disk_io_contention");
        bindTextOrThrow(db, event.get(), 2, "{\"disk_queue_length\":1.2}");
        event.execute();

        const auto activeDiagnosis = iris::getLatestDiagnosis(db);
        expect(activeDiagnosis.active, "A recent diagnostic event should be active.");
        expect(activeDiagnosis.rule == "disk_io_contention", "Wrong diagnostic rule returned.");
        expect(activeDiagnosis.recommendation == "Review disk activity.", "Rule recommendation was not joined.");

        std::cout << "Member 4 smoke tests passed: schema, empty diagnosis, recent-event lookup, recommendation join.\n";
        sqlite3_close(db);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Smoke test failed: " << error.what() << '\n';
        sqlite3_close(db);
        return 1;
    }
}
