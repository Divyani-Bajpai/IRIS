#include "iris/dashboard_server.h"
#include "iris/disk_process_monitor.h"
#include "iris/shared_metrics_store.h"
#include "iris/startup_monitor.h"
#include "metrics_collector.h"
#include "deviation_calculator.h"
#include "diagnostic_logger.h"
#include "rule_engine.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {
std::atomic<bool> stopRequested{false};

struct DiagnosticValue {
    std::string name;
    double value = 0.0;
    double deviation = 0.0;
};

struct DiagnosticSnapshot {
    const double missing = std::numeric_limits<double>::quiet_NaN();
    MetricScores scores{missing, missing, missing, missing, missing};
    std::vector<DiagnosticValue> values;
};

bool loadDeviation(iris::SharedMetricsStore& store, const std::string& name,
                   double& deviation, DiagnosticSnapshot& snapshot,
                   std::string& error) {
    double current = 0.0;
    bool foundCurrent = false;
    if (!store.readLatestMetric(name, "", current, foundCurrent, error)) {
        return false;
    }
    if (!foundCurrent) return true;

    iris::BaselineRecord baseline;
    bool foundBaseline = false;
    if (!store.readBaseline(name, baseline, foundBaseline, error)) {
        return false;
    }
    if (!foundBaseline ||
        !deviationCalculation(current, baseline.averageValue,
                              baseline.standardDeviation, deviation)) {
        return true;
    }
    snapshot.values.push_back({name, current, deviation});
    return true;
}

DiagnosticSnapshot readDiagnosticSnapshot(iris::SharedMetricsStore& store,
                                           std::string& error,
                                           bool& succeeded) {
    DiagnosticSnapshot snapshot;
    succeeded = true;
    succeeded = loadDeviation(store, iris::kPageFaultsPerSec,
                              snapshot.scores.pageFaultZ, snapshot, error) && succeeded;
    succeeded = loadDeviation(store, iris::kAvailableRamMb,
                              snapshot.scores.availableRamZ, snapshot, error) && succeeded;
    succeeded = loadDeviation(store, iris::kCpuPercent,
                              snapshot.scores.cpuZ, snapshot, error) && succeeded;
    succeeded = loadDeviation(store, iris::kDiskQueueLength,
                              snapshot.scores.diskQueueZ, snapshot, error) && succeeded;
    succeeded = loadDeviation(store, iris::kPercentDiskTime,
                              snapshot.scores.diskTimeZ, snapshot, error) && succeeded;
    return snapshot;
}

std::string metricValuesJson(const DiagnosticSnapshot& snapshot) {
    std::ostringstream json;
    json.precision(10);
    json << '{';
    bool first = true;
    for (const DiagnosticValue& metric : snapshot.values) {
        if (!first) json << ',';
        first = false;
        json << '"' << metric.name << "\":{\"value\":" << metric.value
             << ",\"deviation\":" << metric.deviation << '}';
    }
    json << '}';
    return json.str();
}

BOOL WINAPI handleConsoleControl(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT ||
        event == CTRL_CLOSE_EVENT || event == CTRL_SHUTDOWN_EVENT) {
        stopRequested.store(true);
        return TRUE;
    }
    return FALSE;
}

unsigned int parseUnsigned(const char* text, unsigned int fallback) {
    if (text == nullptr || *text == '\0') return fallback;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(text, &end, 10);
    if (end == text || *end != '\0' || parsed == 0 ||
        parsed > static_cast<unsigned long>(UINT_MAX)) {
        return fallback;
    }
    return static_cast<unsigned int>(parsed);
}
}  // namespace

int main(int argc, char** argv) {
    const std::string databasePath = argc > 1 ? argv[1] : "iris.db";
    const unsigned int intervalSeconds =
        argc > 2 ? parseUnsigned(argv[2], 5) : 5;
    const unsigned int sessionSeconds =
        argc > 3 ? parseUnsigned(argv[3], 60) : 60;
    const bool openBrowser = argc <= 4 || std::string(argv[4]) != "--no-browser";

    iris::SharedMetricsStore store;
    std::string error;
    if (!store.open(databasePath, error)) {
        std::cerr << "Database setup failed: " << error << '\n';
        return 1;
    }
    if (!store.endDiagnosticPeriod(error)) {
        std::cerr << "Could not close an interrupted diagnostic period: "
                  << error << '\n';
    }
    MEMORYSTATUSEX memoryInfo{};
    memoryInfo.dwLength = sizeof(memoryInfo);
    if (GlobalMemoryStatusEx(&memoryInfo)) {
        const double totalRamMb = static_cast<double>(memoryInfo.ullTotalPhys)
                                / (1024.0 * 1024.0);
        if (!store.bootstrapDefaultBaselines(totalRamMb, error)) {
            std::cerr << "Could not create first-run baselines: " << error << '\n';
        }
    } else {
        std::cerr << "Could not read installed RAM; default baselines were not initialized.\n";
    }
    std::vector<std::string> startupWarnings;
    const auto startupApps = iris::collectStartupApps(startupWarnings);
    if (!store.recordStartupApps(startupApps, error)) {
        std::cerr << "Could not store startup inventory: " << error << '\n';
    } else {
        std::cout << "Startup inventory: " << startupApps.size()
                  << " Registry Run entries recorded.\n";
    }
    for (const std::string& warning : startupWarnings) {
        std::cerr << "Startup inventory: " << warning << '\n';
    }
    iris::DiskProcessMonitor monitor;
    if (!monitor.initialize(error)) {
        std::cerr << "Collector setup failed: " << error << '\n';
        return 1;
    }
    iris::MetricsCollector cpuMemoryCollector;
    std::string cpuMemoryError;
    const bool cpuMemoryReady = cpuMemoryCollector.init(cpuMemoryError);
    if (!cpuMemoryReady) {
        std::cerr << "CPU/memory collector unavailable; disk/process monitoring continues: "
                  << cpuMemoryError << '\n';
    }

    iris::DashboardServer dashboard(databasePath);
    dashboard.setSessionState(iris::DashboardSessionState::Idle);
    std::string dashboardError;
    const bool dashboardRunning = dashboard.start(dashboardError);
    if (dashboardRunning) {
        const std::string dashboardUrl = "http://127.0.0.1:" +
                                         std::to_string(dashboard.port());
        std::cout << "IRIS health report: " << dashboardUrl << '\n';
        if (openBrowser) {
            ShellExecuteA(nullptr, "open", dashboardUrl.c_str(), nullptr, nullptr,
                          SW_SHOWNORMAL);
        }
    } else {
        std::cerr << "Dashboard unavailable; collection continues: "
                  << dashboardError << '\n';
    }

    SetConsoleCtrlHandler(handleConsoleControl, TRUE);
    std::cout << "IRIS is ready. Start a " << sessionSeconds
              << "-second live check from the report. The local server stays open; "
              << "press Ctrl+C to exit. Database: " << databasePath << "\n";
    std::string lastRuleName;
    while (!stopRequested.load()) {
        if (!dashboard.consumeCollectionRequest()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        std::cout << "Preparing baseline from readings saved before this check.\n";
        if (!store.recomputeSystemBaselines(
                iris::kLearnedBaselineWindowDays,
                iris::kLearnedBaselineMinimumSamples, error)) {
            std::cerr << "Could not refresh learned baselines: " << error << '\n';
        }
        dashboard.setSessionState(iris::DashboardSessionState::Collecting);
        std::cout << "Starting a new live collection for " << sessionSeconds
                  << " seconds. The baseline will stay fixed during this check.\n";
        lastRuleName.clear();
        auto nextSample = std::chrono::steady_clock::now();
        const auto sessionEnd = nextSample + std::chrono::seconds(sessionSeconds);
        while (!stopRequested.load() &&
               std::chrono::steady_clock::now() < sessionEnd) {
            iris::CollectionResult result = monitor.collect();
            if (cpuMemoryReady) {
                iris::CpuMemReading reading;
                if (cpuMemoryCollector.sampleOnce(reading, cpuMemoryError)) {
                    if (reading.cpuValid) {
                        result.samples.push_back(
                            {reading.timestamp, iris::kCpuPercent,
                             reading.cpuPercent, {}});
                    }
                    if (reading.ramValid) {
                        result.samples.push_back(
                            {reading.timestamp, iris::kAvailableRamMb,
                             reading.ramAvailableMB, {}});
                    }
                    if (reading.pageFaultsValid) {
                        result.samples.push_back(
                            {reading.timestamp, iris::kPageFaultsPerSec,
                             reading.pageFaultsPerSec, {}});
                    }
                } else {
                    result.warnings.push_back("CPU/memory collector: " + cpuMemoryError);
                }
            }
            if (!store.insertSamples(result.samples, error)) {
                // A database lock or write failure must not terminate monitoring.
                std::cerr << "Could not store this sample batch: " << error << '\n';
            } else {
                std::size_t processRows = 0;
                for (const iris::MetricSample& sample : result.samples) {
                    if (!sample.processName.empty()) ++processRows;
                }
                std::cout << result.samples.size() << " live metric rows saved (including "
                          << processRows << " process rows) at "
                          << iris::utcTimestampNow() << " UTC\n";
                std::string diagnosticError;
                bool diagnosticReadSucceeded = false;
                const DiagnosticSnapshot snapshot =
                    readDiagnosticSnapshot(store, diagnosticError,
                                           diagnosticReadSucceeded);
                if (!diagnosticReadSucceeded) {
                    std::cerr << "Could not read diagnostic inputs: "
                              << diagnosticError << '\n';
                } else {
                    const RuleResult diagnosis = evaluateRules(snapshot.scores);
                    if (diagnosis.matched && diagnosis.ruleName != lastRuleName) {
                        if (!lastRuleName.empty() &&
                            !store.endDiagnosticPeriod(error)) {
                            std::cerr << "Could not close prior diagnostic period: "
                                      << error << '\n';
                        }
                        if (!store.beginDiagnosticPeriod(diagnosis.ruleName, error)) {
                            std::cerr << "Could not start diagnostic period: "
                                      << error << '\n';
                        }
                        lastRuleName = diagnosis.ruleName;
                        logDiagnosis(diagnosis);
                        if (!store.recordDiagnosticEvent(
                                diagnosis.ruleName, metricValuesJson(snapshot), error)) {
                            std::cerr << "Could not store diagnostic event: "
                                      << error << '\n';
                        }
                    } else if (!diagnosis.matched && !lastRuleName.empty()) {
                        if (!store.endDiagnosticPeriod(error)) {
                            std::cerr << "Could not close diagnostic period: "
                                      << error << '\n';
                        }
                        lastRuleName.clear();
                    }
                }
            }
            for (const std::string& warning : result.warnings) {
                std::cerr << "Collector: " << warning << '\n';
            }

            nextSample += std::chrono::seconds(intervalSeconds);
            const auto now = std::chrono::steady_clock::now();
            if (nextSample < now) nextSample = now + std::chrono::seconds(intervalSeconds);
            std::this_thread::sleep_until(std::min(nextSample, sessionEnd));
        }
        if (stopRequested.load()) {
            if (!store.endDiagnosticPeriod(error)) {
                std::cerr << "Could not close diagnostic period: " << error << '\n';
            }
            break;
        }
        if (!store.endDiagnosticPeriod(error)) {
            std::cerr << "Could not close diagnostic period: " << error << '\n';
        }
        lastRuleName.clear();
        if (!store.pruneRawMetrics(30, error)) {
            std::cerr << "Could not prune readings older than 30 days: " << error << '\n';
        }
        dashboard.setSessionState(iris::DashboardSessionState::Complete);
        std::cout << "Check complete. Report and local server are ready for another check.\n";
    }
    dashboard.stop();
    SetConsoleCtrlHandler(handleConsoleControl, FALSE);
    return 0;
}
