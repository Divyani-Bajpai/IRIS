#define NOMINMAX
#include "iris/disk_process_monitor.h"

#include <psapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>
#include <vector>

#if defined(_MSC_VER)
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "psapi.lib")
#endif

namespace iris {
namespace {

constexpr double kBytesPerMegabyte = 1024.0 * 1024.0;
// PDH_CSTATUS_VALID_DATA and PDH_CSTATUS_NEW_DATA values, also used by SDK
// headers that do not expose the symbolic names.
constexpr DWORD kPdhStatusValidData = 0x00000000;
constexpr DWORD kPdhStatusNewData = 0x00000001;

std::uint64_t fileTimeValue(const FILETIME& value) {
    ULARGE_INTEGER result{};
    result.LowPart = value.dwLowDateTime;
    result.HighPart = value.dwHighDateTime;
    return result.QuadPart;
}

std::string wideToUtf8(const wchar_t* value) {
    if (value == nullptr || *value == L'\0') {
        return {};
    }
    const int required = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0,
                                             nullptr, nullptr);
    if (required <= 1) {
        return {};
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), required,
                        nullptr, nullptr);
    result.pop_back();
    return result;
}

void addWarning(CollectionResult& result, const char* operation, DWORD code) {
    std::ostringstream message;
    message << operation << " failed (Windows error " << code << ")";
    result.warnings.push_back(message.str());
}

}  // namespace

std::string utcTimestampNow() {
    SYSTEMTIME time{};
    GetSystemTime(&time);
    std::ostringstream value;
    value << std::setfill('0') << std::setw(4) << time.wYear << '-'
          << std::setw(2) << time.wMonth << '-' << std::setw(2) << time.wDay
          << ' ' << std::setw(2) << time.wHour << ':' << std::setw(2)
          << time.wMinute << ':' << std::setw(2) << time.wSecond;
    return value.str();
}

DiskProcessMonitor::DiskProcessMonitor() {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    logicalProcessorCount_ = systemInfo.dwNumberOfProcessors;
}

DiskProcessMonitor::~DiskProcessMonitor() {
    close();
}

bool DiskProcessMonitor::initialize(std::string& error) {
    close();
    PDH_STATUS status = PdhOpenQueryW(nullptr, 0, &query_);
    if (status != ERROR_SUCCESS) {
        diskInitializationWarning_ = "PdhOpenQueryW failed (PDH status "
                                     + std::to_string(status) + ")";
        error = diskInitializationWarning_;
        initialized_ = true; // Process monitoring can still run.
        return true;
    }

    auto addCounter = [&](const wchar_t* path, PDH_HCOUNTER* counter,
                          const char* label) {
        const PDH_STATUS englishStatus =
            PdhAddEnglishCounterW(query_, path, 0, counter);
        if (englishStatus == ERROR_SUCCESS) return true;
        const PDH_STATUS localizedStatus =
            PdhAddCounterW(query_, path, 0, counter);
        if (localizedStatus == ERROR_SUCCESS) return true;
        if (!diskInitializationWarning_.empty()) {
            diskInitializationWarning_ += "; ";
        }
        diskInitializationWarning_ += std::string("Could not add ") + label
            + " counter (English status " + std::to_string(englishStatus)
            + ", localized status " + std::to_string(localizedStatus) + ")";
        return false;
    };
    addCounter(L"\\PhysicalDisk(_Total)\\Current Disk Queue Length",
               &queueCounter_, "PhysicalDisk Current Disk Queue Length");
    addCounter(L"\\PhysicalDisk(_Total)\\% Disk Time",
               &diskTimeCounter_, "PhysicalDisk % Disk Time");
    addCounter(L"\\LogicalDisk(_Total)\\Disk Read Bytes/sec",
               &diskReadCounter_, "LogicalDisk Disk Read Bytes/sec");
    addCounter(L"\\LogicalDisk(_Total)\\Disk Write Bytes/sec",
               &diskWriteCounter_, "LogicalDisk Disk Write Bytes/sec");

    if (queueCounter_ == nullptr && diskTimeCounter_ == nullptr) {
        error = diskInitializationWarning_;
        PdhCloseQuery(query_);
        query_ = nullptr;
        initialized_ = true;
        return true;
    }

    // Prime the rate counter. The next collection, after the caller's normal
    // sampling interval, can produce a valid % Disk Time value.
    status = PdhCollectQueryData(query_);
    if (status != ERROR_SUCCESS) {
        if (!diskInitializationWarning_.empty()) {
            diskInitializationWarning_ += "; ";
        }
        diskInitializationWarning_ += "Could not prime physical disk counters (PDH status "
                                      + std::to_string(status) + ")";
        error = diskInitializationWarning_;
    } else {
        error = diskInitializationWarning_;
    }

    lastDiskCollection_ = std::chrono::steady_clock::now();
    if (logicalProcessorCount_ == 0) {
        if (!error.empty()) error += "; ";
        error += "Could not determine logical processor count; process CPU samples are disabled";
    }
    initialized_ = true;
    return true;
}

CollectionResult DiskProcessMonitor::collect() {
    CollectionResult result;
    const std::string timestamp = utcTimestampNow();
    if (!initialized_) {
        result.warnings.push_back("DiskProcessMonitor is not initialized");
        return result;
    }

    if (!diskInitializationWarning_.empty() &&
        !diskInitializationWarningReported_) {
        result.warnings.push_back(diskInitializationWarning_);
        diskInitializationWarningReported_ = true;
    }
    collectDiskMetrics(timestamp, result);
    collectDiskCapacityMetrics(timestamp, result);
    collectProcessMetrics(timestamp, result);
    return result;
}

void DiskProcessMonitor::collectDiskMetrics(const std::string& timestamp,
                                            CollectionResult& result) {
    if (query_ == nullptr) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    // PDH rate counters need a meaningful interval between observations.
    if (now - lastDiskCollection_ < std::chrono::seconds(1)) {
        return;
    }
    const PDH_STATUS status = PdhCollectQueryData(query_);
    if (status != ERROR_SUCCESS) {
        if (!diskCollectionWarningReported_) {
            result.warnings.push_back("PdhCollectQueryData failed (PDH status "
                                      + std::to_string(status) + ")");
            diskCollectionWarningReported_ = true;
        }
        return;
    }
    lastDiskCollection_ = now;
    diskCollectionWarningReported_ = false;

    double value = 0.0;
    if (queueCounter_ != nullptr && readCounter(queueCounter_, value)) {
        result.samples.push_back({timestamp, kDiskQueueLength, value, {}});
    } else if (queueCounter_ != nullptr) {
        result.warnings.push_back("Disk queue length is not available for this sample");
    }
    if (diskTimeCounter_ != nullptr && readCounter(diskTimeCounter_, value)) {
        result.samples.push_back({timestamp, kPercentDiskTime, value, {}});
    } else if (diskTimeCounter_ != nullptr) {
        result.warnings.push_back("Disk time is not available for this sample");
    }
    constexpr double bytesPerMegabyte = 1024.0 * 1024.0;
    if (diskReadCounter_ != nullptr && readCounter(diskReadCounter_, value)) {
        result.samples.push_back({timestamp, kDiskReadMbPerSec,
                                  value / bytesPerMegabyte, {}});
    }
    if (diskWriteCounter_ != nullptr && readCounter(diskWriteCounter_, value)) {
        result.samples.push_back({timestamp, kDiskWriteMbPerSec,
                                  value / bytesPerMegabyte, {}});
    }
}

void DiskProcessMonitor::collectDiskCapacityMetrics(
    const std::string& timestamp, CollectionResult& result) {
    const DWORD drives = GetLogicalDrives();
    if (drives == 0) return;
    constexpr double bytesPerGb = 1024.0 * 1024.0 * 1024.0;
    ULONGLONG totalBytes = 0;
    ULONGLONG freeBytes = 0;
    for (unsigned int i = 0; i < 26; ++i) {
        if ((drives & (1u << i)) == 0) continue;
        const wchar_t drivePath[] = {static_cast<wchar_t>(L'A' + i), L':', L'\\', L'\0'};
        if (GetDriveTypeW(drivePath) != DRIVE_FIXED) continue;
        ULARGE_INTEGER total{}, available{}, free{};
        if (GetDiskFreeSpaceExW(drivePath, &available, &total, &free)) {
            totalBytes += total.QuadPart;
            freeBytes += free.QuadPart;
        }
    }
    if (totalBytes == 0) return;
    const double usedPercent =
        100.0 * static_cast<double>(totalBytes - freeBytes)
        / static_cast<double>(totalBytes);
    result.samples.push_back({timestamp, kDiskUsedPercent, usedPercent, {}});
    result.samples.push_back({timestamp, kDiskFreeGb,
                              static_cast<double>(freeBytes) / bytesPerGb, {}});
}

bool DiskProcessMonitor::readCounter(PDH_HCOUNTER counter, double& value) const {
    PDH_FMT_COUNTERVALUE formatted{};
    const PDH_STATUS status = PdhGetFormattedCounterValue(
        counter, PDH_FMT_DOUBLE, nullptr, &formatted);
    if (status != ERROR_SUCCESS ||
        (formatted.CStatus != kPdhStatusValidData &&
         formatted.CStatus != kPdhStatusNewData) ||
        !std::isfinite(formatted.doubleValue)) {
        return false;
    }
    value = formatted.doubleValue;
    return true;
}

void DiskProcessMonitor::collectProcessMetrics(const std::string& timestamp,
                                               CollectionResult& result) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        addWarning(result, "CreateToolhelp32Snapshot", GetLastError());
        return;
    }
    const auto sampleTime = std::chrono::steady_clock::now();
    std::unordered_map<DWORD, ProcessState> currentProcesses;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot, &entry)) {
        const DWORD error = GetLastError();
        CloseHandle(snapshot);
        addWarning(result, "Process32FirstW", error);
        return;
    }

    do {
        HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                                     FALSE, entry.th32ProcessID);
        if (process == nullptr) {
            // Access denied and short-lived processes are normal on Windows.
            // Their prior state is deliberately discarded below.
            continue;
        }

        FILETIME creation{}, exit{}, kernel{}, user{};
        const bool cpuValid = GetProcessTimes(process, &creation, &exit,
                                               &kernel, &user) != FALSE;
        PROCESS_MEMORY_COUNTERS memory{};
        memory.cb = sizeof(memory);
        const bool memoryValid = GetProcessMemoryInfo(
            process, &memory, sizeof(memory)) != FALSE;
        IO_COUNTERS io{};
        const bool ioValid = GetProcessIoCounters(process, &io) != FALSE;

        if (memoryValid) {
            const std::string name = wideToUtf8(entry.szExeFile);
            if (!name.empty()) {
                const double memoryMb = static_cast<double>(memory.WorkingSetSize)
                                        / kBytesPerMegabyte;
                result.samples.push_back(
                    {timestamp, kProcessMemoryMb, memoryMb, name});
            }
        }

        if (cpuValid) {
            const std::uint64_t creationTime = fileTimeValue(creation);
            const std::uint64_t cpuTime = fileTimeValue(kernel) + fileTimeValue(user);
            ProcessState current{creationTime, cpuTime,
                io.ReadTransferCount, io.WriteTransferCount, ioValid, sampleTime};
            const auto previous = previousProcesses_.find(entry.th32ProcessID);
            if (previous != previousProcesses_.end() &&
                previous->second.creationTime100ns == creationTime) {
                const double elapsed = std::chrono::duration<double>(
                    sampleTime - previous->second.sampledAt).count();
                double percent = 0.0;
                if (calculateProcessCpuPercent(previous->second.cpuTime100ns,
                                               cpuTime, elapsed,
                                               logicalProcessorCount_, percent)) {
                    const std::string name = wideToUtf8(entry.szExeFile);
                    if (!name.empty()) {
                        result.samples.push_back(
                            {timestamp, kProcessCpuPercent, percent, name});
                    }
                }
                if (ioValid && previous->second.ioCountersValid && elapsed > 0.0) {
                    constexpr double bytesPerMb = 1024.0 * 1024.0;
                    const std::string name = wideToUtf8(entry.szExeFile);
                    if (!name.empty() && io.ReadTransferCount >= previous->second.readBytes) {
                        result.samples.push_back({timestamp, kProcessDiskReadMbPerSec,
                            static_cast<double>(io.ReadTransferCount - previous->second.readBytes)
                                / elapsed / bytesPerMb, name});
                    }
                    if (!name.empty() && io.WriteTransferCount >= previous->second.writeBytes) {
                        result.samples.push_back({timestamp, kProcessDiskWriteMbPerSec,
                            static_cast<double>(io.WriteTransferCount - previous->second.writeBytes)
                                / elapsed / bytesPerMb, name});
                    }
                }
            }
            currentProcesses[entry.th32ProcessID] = current;
        }

        CloseHandle(process);
    } while (Process32NextW(snapshot, &entry));

    CloseHandle(snapshot);
    // Replacing the map drops processes that exited, became inaccessible, or
    // were restarted with the same PID. Their first subsequent sample has no
    // CPU delta and therefore cannot produce a bogus percentage.
    previousProcesses_.swap(currentProcesses);
}

void DiskProcessMonitor::close() {
    initialized_ = false;
    diskInitializationWarning_.clear();
    diskInitializationWarningReported_ = false;
    diskCollectionWarningReported_ = false;
    lastDiskCollection_ = {};
    queueCounter_ = nullptr;
    diskTimeCounter_ = nullptr;
    diskReadCounter_ = nullptr;
    diskWriteCounter_ = nullptr;
    if (query_ != nullptr) {
        PdhCloseQuery(query_);
        query_ = nullptr;
    }
    previousProcesses_.clear();
}

}  // namespace iris
