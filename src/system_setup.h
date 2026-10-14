#pragma once

#include <chrono>
#include <sqlite3.h>

namespace iris {

void bootstrapDefaultBaselines(sqlite3* database);
void collectStartupApps(sqlite3* database);

// Integration hook: call after the first successful real metric collection.
// This measures IRIS launch-to-first-reading latency, not actual Windows boot time.
void recordFirstMetricCollection(sqlite3* database,
    std::chrono::steady_clock::time_point processStart);

} // namespace iris
