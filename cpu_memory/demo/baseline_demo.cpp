// Test program for the baseline code.
// Usage:
//   baseline_demo.exe --selftest                 (checks the maths, no database)
//   baseline_demo.exe [dbPath] [minSamples]      (recalculates and prints baselines)
// dbPath defaults to iris_data.db, minSamples defaults to 720.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "baseline.h"

static int runSelfTest() {
    // Known data: mean is 5, sample std dev is sqrt(32 / 7) = 2.1381
    std::vector<double> values = {2, 4, 4, 4, 5, 5, 7, 9};

    iris::BaselineStats a = iris::computeBaselineStats("test", values, 8);
    iris::BaselineStats b = iris::computeBaselineStats("test", values, 10);

    bool meanOk = std::fabs(a.mean - 5.0) < 1e-9;
    bool stdOk = std::fabs(a.stdDev - 2.138089935) < 1e-6;
    bool readyOk = a.ready;        // 8 values, 8 needed
    bool notReadyOk = !b.ready;    // 8 values, 10 needed

    std::printf("mean   = %.6f (expected 5.000000)   %s\n", a.mean, meanOk ? "PASS" : "FAIL");
    std::printf("stdDev = %.6f (expected 2.138090)   %s\n", a.stdDev, stdOk ? "PASS" : "FAIL");
    std::printf("ready with min 8  : %s\n", readyOk ? "PASS" : "FAIL");
    std::printf("not ready, min 10 : %s\n", notReadyOk ? "PASS" : "FAIL");

    return (meanOk && stdOk && readyOk && notReadyOk) ? 0 : 1;
}

int main(int argc, char* argv[]) {
    if (argc > 1 && std::string(argv[1]) == "--selftest") {
        return runSelfTest();
    }

    std::string dbPath = "iris_data.db";
    int minSamples = iris::kDefaultMinSamples;

    if (argc > 1) {
        dbPath = argv[1];
    }
    if (argc > 2) {
        minSamples = std::atoi(argv[2]);
    }

    std::string error;
    iris::CpuMemoryBaseline baseline;
    if (!baseline.open(dbPath, error)) {
        std::printf("Could not open database: %s\n", error.c_str());
        return 1;
    }

    if (!baseline.recompute(iris::kDefaultWindowDays, minSamples, error)) {
        std::printf("Could not recompute baseline: %s\n", error.c_str());
        return 1;
    }

    std::vector<iris::BaselineStats> all;
    if (!baseline.getAllBaselines(all, error)) {
        std::printf("Could not read baseline: %s\n", error.c_str());
        return 1;
    }

    std::printf("Window %d days, minimum %d valid readings per metric\n\n",
                iris::kDefaultWindowDays, minSamples);
    std::printf("%-22s %8s %12s %12s  %s\n", "metric", "count", "mean", "stdDev", "status");
    for (const iris::BaselineStats& s : all) {
        std::printf("%-22s %8d %12.3f %12.3f  %s\n",
                    s.metricName.c_str(), s.sampleCount, s.mean, s.stdDev,
                    s.ready ? "READY" : "not ready (use default baseline)");
    }

    baseline.close();
    return 0;
}