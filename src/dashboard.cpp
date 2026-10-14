#include "dashboard.h"
#include "database.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <string>

namespace iris {
namespace {
std::string columnText(sqlite3_stmt* statement, int column, const char* fallback = "") {
    const auto* value = reinterpret_cast<const char*>(sqlite3_column_text(statement, column));
    return value ? value : fallback;
}

bool stepRowOrThrow(sqlite3* database, sqlite3_stmt* statement) {
    const int result = sqlite3_step(statement);
    if (result == SQLITE_ROW) return true;
    if (result == SQLITE_DONE) return false;
    throw std::runtime_error(sqlite3_errmsg(database));
}
} // namespace

void showLiveMetrics(sqlite3* database) {
    std::cout << "\nLIVE METRICS\n"
              << std::left << std::setw(26) << "Metric" << std::setw(27) << "Expected range"
              << std::setw(15) << "Current" << std::setw(13) << "Status" << "Baseline\n"
              << std::string(92, '-') << '\n';

    Statement statement(database,
        "SELECT b.metric_name, b.average_value, b.standard_deviation, b.source, lm.value "
        "FROM baselines b LEFT JOIN live_metrics lm ON lm.id = ("
        " SELECT id FROM live_metrics WHERE metric_name = b.metric_name "
        " ORDER BY timestamp DESC, id DESC LIMIT 1) ORDER BY b.metric_name;");
    while (stepRowOrThrow(database, statement.get())) {
        const std::string metric = columnText(statement.get(), 0, "unknown");
        const double average = sqlite3_column_double(statement.get(), 1);
        const double stddev = std::max(0.0, sqlite3_column_double(statement.get(), 2));
        const std::string source = columnText(statement.get(), 3, "unknown");
        const bool hasCurrent = sqlite3_column_type(statement.get(), 4) != SQLITE_NULL;
        const double current = sqlite3_column_double(statement.get(), 4);
        double lower = average - 2.0 * stddev;
const double upper = average + 2.0 * stddev;

if (metric == "cpu_percent" ||
    metric == "available_ram_mb" ||
    metric == "page_faults_per_sec" ||
    metric == "disk_queue_length" ||
    metric == "percent_disk_time") {
    lower = std::max(0.0, lower);
}
        const bool normal = hasCurrent && current >= lower && current <= upper;

        std::ostringstream range;
        range << std::fixed << std::setprecision(1) << lower << " - " << upper;
        std::cout << std::left << std::setw(26) << metric << std::fixed << std::setprecision(1)
                  << std::setw(27) << range.str();
        if (hasCurrent) std::cout << std::setw(15) << current;
        else std::cout << std::setw(15) << "Waiting";
        std::cout << std::setw(13) << (hasCurrent ? (normal ? "NORMAL" : "CHECK") : "NO READING")
                  << source << '\n';
    }
}

DiagnosisSummary getLatestDiagnosis(sqlite3* database) {
    DiagnosisSummary summary;
    Statement readings(database,
    "SELECT EXISTS(SELECT 1 FROM live_metrics);");

if (stepRowOrThrow(database, readings.get())) {
    summary.liveMetricsAvailable =
        sqlite3_column_int(readings.get(), 0) != 0;
}
    Statement statement(database,
        "SELECT e.id, e.timestamp, e.rule_matched, e.metric_values, r.recommendation_text "
        "FROM diagnostic_events e LEFT JOIN diagnostic_rules r ON r.rule_name = e.rule_matched "
        "WHERE e.timestamp >= datetime('now', '-5 minutes') "
        "ORDER BY e.timestamp DESC, e.id DESC LIMIT 1;");
    if (!stepRowOrThrow(database, statement.get())) return summary;

    summary.active = true;
    summary.eventId = sqlite3_column_int64(statement.get(), 0);
    summary.timestamp = columnText(statement.get(), 1, "unknown time");
    summary.rule = columnText(statement.get(), 2, "unknown rule");
    summary.metricValues = columnText(statement.get(), 3, "not provided");
    summary.recommendation = columnText(statement.get(), 4,
        "A diagnostic event was recorded, but no recommendation is configured for this rule.");
    return summary;
}


void showDiagnosis(const DiagnosisSummary& diagnosis) {
    std::cout << "\nCONCLUSION\n";

    if (diagnosis.active) {
        std::cout << diagnosis.recommendation
                  << "\nRule: " << diagnosis.rule
                  << "\nRecorded: " << diagnosis.timestamp << '\n';
    } else if (!diagnosis.liveMetricsAvailable) {
        std::cout << "Waiting for live metrics. "
                     "System health cannot be determined yet.\n";
    } else {
        std::cout << "No recent diagnostic event was found. "
                     "This does not guarantee that the system is healthy.\n";
    }
}


void showTechnicalDetails(sqlite3* database, const DiagnosisSummary& diagnosis) {
    std::cout << "\nTECHNICAL DETAILS\n";
    if (diagnosis.active) {
        std::cout << "Current diagnosis event #" << diagnosis.eventId
                  << " metric values: " << diagnosis.metricValues << '\n';
    } else {
        Statement readings(database,
            "SELECT lm.metric_name, lm.value, lm.timestamp FROM live_metrics lm "
            "WHERE lm.id = (SELECT id FROM live_metrics WHERE metric_name = lm.metric_name "
            "ORDER BY timestamp DESC, id DESC LIMIT 1) ORDER BY lm.metric_name;");
        std::cout << "Latest readings:\n";
        while (stepRowOrThrow(database, readings.get())) {
            std::cout << "  " << columnText(readings.get(), 0, "unknown") << ": "
                      << sqlite3_column_double(readings.get(), 1) << " ("
                      << columnText(readings.get(), 2, "unknown time") << ")\n";
        }
    }

    Statement baselines(database, "SELECT metric_name, source FROM baselines ORDER BY metric_name;");
    std::cout << "Baseline sources:\n";
    while (stepRowOrThrow(database, baselines.get())) {
        std::cout << "  " << columnText(baselines.get(), 0, "unknown") << ": "
                  << columnText(baselines.get(), 1, "unknown") << '\n';
    }
}

void runReport(sqlite3* database) {
    showLiveMetrics(database);
    const DiagnosisSummary diagnosis = getLatestDiagnosis(database);
    showDiagnosis(diagnosis);
    showTechnicalDetails(database, diagnosis);
}

} // namespace iris
