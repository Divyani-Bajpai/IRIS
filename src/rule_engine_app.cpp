// IRIS - Rule Engine (Member 3)
// Reads the shared database every few seconds, checks the two rules
// (Memory Thrashing, Disk I/O Contention) and saves matches.
//
// Usage:
//   rule_engine_app.exe [dbPath] [intervalSeconds] [cycleCount] [minSamples]
//     dbPath          default iris_data.db  (must be the SAME file the other modules use)
//     intervalSeconds default 5             (use 5 to 10)
//     cycleCount      default 0             (0 = run until Ctrl+C)
//     minSamples      default 720           (readings needed before a baseline is used)
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>
#include <thread>

#include "database_reader.h"
#include "diagnosis_cycle.h"
#include "diagnostic_logger.h"
#include "event_store.h"

using namespace std;

const int DEFAULT_MIN_SAMPLES = 720;

static volatile sig_atomic_t stopRequested = 0;
static void onStopSignal(int) { stopRequested = 1; }

// Waits in small steps so Ctrl+C is noticed quickly.
static void waitSeconds(int seconds)
{
    for (int i = 0; i < seconds * 10 && !stopRequested; ++i)
        this_thread::sleep_for(chrono::milliseconds(100));
}

static string showZ(bool valid, double z)
{
    if (!valid)
        return "n/a";
    char text[24];
    snprintf(text, sizeof(text), "%+.2f", z);
    return text;
}

static void printCycle(const CycleResult &c)
{
    string time = utcTimeText(std::time(nullptr));

    cout << "[" << time << " UTC] "
         << "PageFaults " << showZ(c.scores.pageFaultsValid, c.scores.pageFaultsZ)
         << " | RAM " << showZ(c.scores.availableRamValid, c.scores.availableRamZ)
         << " | CPU " << showZ(c.scores.cpuValid, c.scores.cpuZ)
         << " | DiskQ " << showZ(c.scores.diskQueueValid, c.scores.diskQueueZ)
         << " | DiskTime " << showZ(c.scores.diskTimeValid, c.scores.diskTimeZ)
         << " -> ";

    if (!c.dataReady)
        cout << "waiting: " << c.message << "\n";
    else if (!c.rule.matched)
        cout << "no rule matched\n";
    else
        cout << c.rule.diagnosis << "\n";

    if (c.rule.matched)
    {
        logDiagnosis(c.rule);
        cout << "Advice : " << c.recommendation << "\n";
        cout << "Saved to diagnostic_events: " << (c.eventSaved ? "Yes" : "No") << "\n\n";
    }
}

int main(int argc, char **argv)
{
    string dbPath = "iris_data.db";
    int intervalSeconds = 5;
    long cycleCount = 0;
    int minSamples = DEFAULT_MIN_SAMPLES;

    if (argc > 1) dbPath = argv[1];
    if (argc > 2) intervalSeconds = atoi(argv[2]);
    if (argc > 3) cycleCount = atol(argv[3]);
    if (argc > 4) minSamples = atoi(argv[4]);
    if (intervalSeconds < 1) intervalSeconds = 5;
    if (minSamples < 2) minSamples = DEFAULT_MIN_SAMPLES;

    signal(SIGINT, onStopSignal);

    string error;
    DatabaseReader reader;
    EventStore store;

    if (!reader.open(dbPath, error))
    {
        cout << "Could not open database: " << error << endl;
        return 1;
    }
    if (!store.open(dbPath, error))
    {
        cout << "Could not prepare rule tables: " << error << endl;
        return 1;
    }

    cout << "IRIS Rule Engine started. Database: " << dbPath
         << ", every " << intervalSeconds << " s. Press Ctrl+C to stop.\n";

    // Baselines are recalculated at start-up and then about once an hour.
    const int RECOMPUTE_EVERY_SECONDS = 3600;
    time_t lastRecompute = 0;

    for (long cycle = 0; !stopRequested && (cycleCount == 0 || cycle < cycleCount); ++cycle)
    {
        if (time(nullptr) - lastRecompute >= RECOMPUTE_EVERY_SECONDS)
        {
            if (!reader.recomputeBaselines(minSamples, error))
                cout << "Baseline recalculation failed: " << error << "\n";
            lastRecompute = time(nullptr);
        }

        CycleResult result;
        if (!runDiagnosisCycle(reader, store, result, error))
            cout << "Database error (cycle skipped): " << error << "\n";
        else
            printCycle(result);

        if (cycleCount == 0 || cycle + 1 < cycleCount)
            waitSeconds(intervalSeconds);
    }

    reader.close();
    store.close();
    cout << "Rule Engine stopped.\n";
    return 0;
}
