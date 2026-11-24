#include "iris/disk_process_monitor.h"

#include <windows.h>

#include <iostream>
#include <string>
#include <vector>

namespace {
struct ChildProcess {
    PROCESS_INFORMATION info{};
};

bool startChild(const std::wstring& path, ChildProcess& child) {
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    std::wstring command = L"\"" + path + L"\"";
    return CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE,
                          CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                          &child.info) != FALSE;
}

void stopChild(ChildProcess& child) {
    if (child.info.hProcess != nullptr) {
        DWORD exitCode = 0;
        if (GetExitCodeProcess(child.info.hProcess, &exitCode) &&
            exitCode == STILL_ACTIVE) {
            TerminateProcess(child.info.hProcess, 0);
        }
        WaitForSingleObject(child.info.hProcess, 2000);
        CloseHandle(child.info.hProcess);
    }
    if (child.info.hThread != nullptr) CloseHandle(child.info.hThread);
}

void terminateChild(ChildProcess& child) {
    if (child.info.hProcess == nullptr) return;
    DWORD exitCode = 0;
    if (GetExitCodeProcess(child.info.hProcess, &exitCode) &&
        exitCode == STILL_ACTIVE) {
        TerminateProcess(child.info.hProcess, 0);
    }
    WaitForSingleObject(child.info.hProcess, 2000);
}

bool hasMetricFor(const std::vector<iris::MetricSample>& samples,
                  const std::string& metric, const std::string& process) {
    for (const auto& sample : samples) {
        if (sample.metricName == metric && sample.processName == process) {
            return true;
        }
    }
    return false;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Expected paths to two long fixtures and one short fixture\n";
        return 2;
    }
    ChildProcess alpha, beta, shortLived;
    const std::wstring alphaPath(argv[1], argv[1] + std::char_traits<char>::length(argv[1]));
    const std::wstring betaPath(argv[2], argv[2] + std::char_traits<char>::length(argv[2]));
    const std::wstring shortPath(argv[3], argv[3] + std::char_traits<char>::length(argv[3]));
    if (!startChild(alphaPath, alpha) || !startChild(betaPath, beta) ||
        !startChild(shortPath, shortLived)) {
        stopChild(alpha);
        stopChild(beta);
        stopChild(shortLived);
        std::cerr << "Could not start the process fixtures\n";
        return 1;
    }
    Sleep(80);

    iris::DiskProcessMonitor monitor;
    std::string error;
    if (!monitor.initialize(error)) {
        stopChild(alpha); stopChild(beta); stopChild(shortLived);
        std::cerr << error << '\n';
        return 1;
    }
    iris::CollectionResult first = monitor.collect();
    // Guarantee the short-lived process is present in the first observation,
    // then make its exit occur between observations instead of relying on a
    // fragile fixed sleep during Windows process startup.
    terminateChild(shortLived);
    Sleep(900); // Allow another CPU interval after its deliberate exit.
    iris::CollectionResult second = monitor.collect();
    stopChild(alpha);
    stopChild(beta);
    stopChild(shortLived);

    const bool alphaMemory =
        hasMetricFor(first.samples, iris::kProcessMemoryMb,
                     "iris_process_fixture_alpha.exe");
    const bool betaMemory =
        hasMetricFor(first.samples, iris::kProcessMemoryMb,
                     "iris_process_fixture_beta.exe");
    const bool shortMemory =
        hasMetricFor(first.samples, iris::kProcessMemoryMb,
                     "iris_process_fixture_short.exe");
    const bool alphaCpu =
        hasMetricFor(second.samples, iris::kProcessCpuPercent,
                     "iris_process_fixture_alpha.exe");
    const bool betaCpu =
        hasMetricFor(second.samples, iris::kProcessCpuPercent,
                     "iris_process_fixture_beta.exe");
    const bool shortCpu =
        hasMetricFor(second.samples, iris::kProcessCpuPercent,
                     "iris_process_fixture_short.exe");
    std::size_t queueSamples = 0;
    std::size_t diskTimeSamples = 0;
    for (const auto& sample : first.samples) {
        if (sample.metricName == iris::kDiskQueueLength) ++queueSamples;
        if (sample.metricName == iris::kPercentDiskTime) ++diskTimeSamples;
    }
    for (const auto& sample : second.samples) {
        if (sample.metricName == iris::kDiskQueueLength) ++queueSamples;
        if (sample.metricName == iris::kPercentDiskTime) ++diskTimeSamples;
    }
    if (!(alphaMemory && betaMemory && shortMemory && alphaCpu && betaCpu) ||
        shortCpu) {
        std::cerr << "Live collection check failed: alpha memory/cpu="
                  << alphaMemory << '/' << alphaCpu << ", beta="
                  << betaMemory << '/' << betaCpu << ", exited process memory/cpu="
                  << shortMemory << '/' << shortCpu << '\n';
        return 1;
    }
    std::cout << "Live process collection passed for two continuing processes; "
                 "the process that exited between samples produced no stale CPU row. "
              << "Physical disk samples: queue=" << queueSamples
              << ", disk-time=" << diskTimeSamples << ".\n";
    return 0;
}
