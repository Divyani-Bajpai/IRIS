// Test program for the CPU and memory module.
// Usage: cpu_memory_demo.exe [intervalSeconds] [sampleCount] [dbPath]
// sampleCount 0 (default) means run until Ctrl+C.
// dbPath defaults to iris_data.db in the current folder.

#include <windows.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "cpu_memory_db.h"
#include "metrics_collector.h"

static volatile std::sig_atomic_t g_stop = 0;

static void onCtrlC(int) {
    g_stop = 1;
}

// Sleeps in small steps so Ctrl+C is noticed quickly.
static void waitMs(int totalMs) {
    int waited = 0;
    while (waited < totalMs && !g_stop) {
        Sleep(100);
        waited += 100;
    }
}

static std::string show(bool valid, double value, const char* unit) {
    if (!valid) {
        return "n/a";
    }
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.1f%s", value, unit);
    return buf;
}

int main(int argc, char* argv[]) {
    int intervalSeconds = 5;
    int sampleCount = 0;
    std::string dbPath = "iris_data.db";

    if (argc > 1) {
        intervalSeconds = std::atoi(argv[1]);
    }
    if (argc > 2) {
        sampleCount = std::atoi(argv[2]);
    }
    if (argc > 3) {
        dbPath = argv[3];
    }
    if (intervalSeconds < 1) {
        intervalSeconds = 5;
    }

    std::signal(SIGINT, onCtrlC);

    std::string error;

    iris::CpuMemoryDb db;
    if (!db.open(dbPath, error)) {
        std::printf("Could not open database: %s\n", error.c_str());
        return 1;
    }

    iris::MetricsCollector collector;
    if (!collector.init(error)) {
        std::printf("Could not start collector: %s\n", error.c_str());
        return 1;
    }

    std::printf("Sampling every %d s, saving to %s. Press Ctrl+C to stop.\n",
                intervalSeconds, dbPath.c_str());

    int taken = 0;
    int saved = 0;
    while (!g_stop && (sampleCount == 0 || taken < sampleCount)) {
        waitMs(intervalSeconds * 1000);
        if (g_stop) {
            break;
        }

        iris::CpuMemReading r;
        if (!collector.sampleOnce(r, error)) {
            std::printf("Sample failed: %s\n", error.c_str());
            continue;
        }
        taken++;

        bool stored = db.insertReading(r, error);
        if (stored) {
            saved++;
        } else {
            std::printf("Not saved: %s\n", error.c_str());
        }

        std::printf("%s | CPU %s | RAM used %s (free %s of %s) | Committed %s | PageFaults/s %s | %s\n",
                    r.timestamp.c_str(),
                    show(r.cpuValid, r.cpuPercent, "%").c_str(),
                    show(r.ramValid, r.ramUsedPercent, "%").c_str(),
                    show(r.ramValid, r.ramAvailableMB, " MB").c_str(),
                    show(r.ramValid, r.ramTotalMB, " MB").c_str(),
                    show(r.committedValid, r.committedPercent, "%").c_str(),
                    show(r.pageFaultsValid, r.pageFaultsPerSec, "").c_str(),
                    stored ? "saved" : "NOT saved");
    }

    collector.close();

    long long total = 0;
    if (db.countReadings(total, error)) {
        std::printf("Saved %d rows this run, %lld rows in the database in total.\n",
                    saved, total);
    } else {
        std::printf("Could not count rows: %s\n", error.c_str());
    }

    db.close();
    std::printf("Stopped.\n");
    return 0;
}