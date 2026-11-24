#include "iris/disk_process_monitor.h"

#include <cmath>
#include <iostream>
#include <limits>

#define CHECK(expression) do { if (!(expression)) { \
    std::cerr << "Check failed at line " << __LINE__ << ": " #expression << '\n'; \
    return 1; } } while (false)

int main() {
    double percent = -1.0;
    // 2 seconds of process CPU over 4 seconds on a 4-core machine = 12.5%.
    CHECK(iris::calculateProcessCpuPercent(0, 20000000, 4.0, 4, percent));
    CHECK(std::abs(percent - 12.5) < 0.0001);

    // A process using a full core on a 2-core machine is 50% of total capacity.
    CHECK(iris::calculateProcessCpuPercent(100, 10000100, 1.0, 2, percent));
    CHECK(std::abs(percent - 50.0) < 0.0001);
    CHECK(iris::calculateProcessCpuPercent(0, 40000000, 1.0, 1, percent));
    CHECK(std::abs(percent - 100.0) < 0.0001); // More than machine capacity is capped.

    // New, exited, or invalidly timed processes must not create a CPU spike.
    CHECK(!iris::calculateProcessCpuPercent(20, 10, 1.0, 4, percent));
    CHECK(!iris::calculateProcessCpuPercent(0, 1, 0.0, 4, percent));
    CHECK(!iris::calculateProcessCpuPercent(0, 1, 1.0, 0, percent));
    CHECK(!iris::calculateProcessCpuPercent(
        0, 1, std::numeric_limits<double>::quiet_NaN(), 4, percent));
    return 0;
}
