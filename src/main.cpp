#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "dashboard.h"
#include "database.h"
#include "system_setup.h"

#include <chrono>
#include <iostream>
#include <stdexcept>

int main() {
    // Keep this timestamp for the future collector callback. It is not the Windows boot time.
    const auto processStart = std::chrono::steady_clock::now();
    (void)processStart; // Used when Member 1/2 collectors are connected.

    sqlite3* database = nullptr;
    try {
        const int openResult = sqlite3_open(iris::kDatabasePath, &database);
        if (openResult != SQLITE_OK) {
            const std::string message = database ? sqlite3_errmsg(database) : "Could not open database.";
            if (database) sqlite3_close(database);
            database = nullptr;
            throw std::runtime_error(message);
        }
        sqlite3_busy_timeout(database, 5000);
        iris::createSharedSchema(database);
        iris::bootstrapDefaultBaselines(database);
        iris::collectStartupApps(database);

        // This Member 4 shell displays a one-time report. In the integrated app,
        // call iris::recordFirstMetricCollection(database, processStart) from the
        // collector's first-success callback, then run the UI refresh loop.
        iris::runReport(database);

        const int closeResult = sqlite3_close(database);
        database = nullptr;
        if (closeResult != SQLITE_OK) throw std::runtime_error("Could not close SQLite database cleanly.");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "IRIS Member 4 error: " << error.what() << '\n';
        if (database) sqlite3_close(database);
        return 1;
    }
}
