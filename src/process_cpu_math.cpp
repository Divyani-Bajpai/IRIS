#include "iris/disk_process_monitor.h"

#include <cmath>

namespace iris {
namespace {
constexpr double kFileTimeUnitsPerSecond = 10000000.0;
}

bool calculateProcessCpuPercent(std::uint64_t previousCpu100ns,
                                std::uint64_t currentCpu100ns,
                                double elapsedSeconds,
                                unsigned int logicalProcessorCount,
                                double& percent) {
    percent = 0.0;
    if (currentCpu100ns < previousCpu100ns ||
        !std::isfinite(elapsedSeconds) || elapsedSeconds <= 0.0 ||
        logicalProcessorCount == 0) {
        return false;
    }
    const double cpuSeconds =
        static_cast<double>(currentCpu100ns - previousCpu100ns)
        / kFileTimeUnitsPerSecond;
    percent = (cpuSeconds / (elapsedSeconds * logicalProcessorCount)) * 100.0;
    if (!std::isfinite(percent) || percent < 0.0) {
        percent = 0.0;
        return false;
    }
    // A process cannot consume more than the machine's total capacity. Capping
    // absorbs small scheduling/timestamp jitter in short sample intervals.
    if (percent > 100.0) percent = 100.0;
    return true;
}
}  // namespace iris
