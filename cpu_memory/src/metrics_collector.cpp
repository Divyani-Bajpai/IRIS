#include "metrics_collector.h"

#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <cstdio>

namespace iris {

namespace {

std::string pdhErrorText(const char* what, PDH_STATUS status) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%s failed (PDH error 0x%08lX)",
                  what, static_cast<unsigned long>(status));
    return buf;
}

std::string nowUtc() {
    SYSTEMTIME st;
    GetSystemTime(&st);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

// Reads one counter as a double. Returns false if the value is not valid data.
bool readCounter(void* counter, double& value) {
    PDH_FMT_COUNTERVALUE result;
    DWORD type = 0;
    PDH_STATUS status = PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, &type, &result);
    if (status != ERROR_SUCCESS) {
        return false;
    }
    if (result.CStatus != PDH_CSTATUS_VALID_DATA && result.CStatus != PDH_CSTATUS_NEW_DATA) {
        return false;
    }
    value = result.doubleValue;
    return true;
}

// Adds a counter using its English path, so it works on any Windows language.
bool addCounter(void* query, const wchar_t* path, void** counter,
                const char* name, std::string& error) {
    PDH_STATUS status = PdhAddEnglishCounterW(query, path, 0, counter);
    if (status != ERROR_SUCCESS) {
        error = pdhErrorText(name, status);
        return false;
    }
    return true;
}

}  // namespace

MetricsCollector::MetricsCollector() {}

MetricsCollector::~MetricsCollector() {
    close();
}

bool MetricsCollector::init(std::string& error) {
    close();

    // Total RAM is not a PDH counter, so read it once here.
    MEMORYSTATUSEX mem;
    mem.dwLength = sizeof(mem);
    if (!GlobalMemoryStatusEx(&mem)) {
        error = "GlobalMemoryStatusEx failed";
        return false;
    }
    totalRamMB_ = static_cast<double>(mem.ullTotalPhys) / (1024.0 * 1024.0);

    PDH_STATUS status = PdhOpenQueryW(nullptr, 0, &query_);
    if (status != ERROR_SUCCESS) {
        query_ = nullptr;
        error = pdhErrorText("PdhOpenQuery", status);
        return false;
    }

    if (!addCounter(query_, L"\\Processor(_Total)\\% Processor Time",
                    &cpuCounter_, "CPU counter", error) ||
        !addCounter(query_, L"\\Memory\\Available MBytes",
                    &availableCounter_, "Available MBytes counter", error) ||
        !addCounter(query_, L"\\Memory\\% Committed Bytes In Use",
                    &committedCounter_, "Committed bytes counter", error) ||
        !addCounter(query_, L"\\Memory\\Page Faults/sec",
                    &pageFaultCounter_, "Page faults counter", error)) {
        close();
        return false;
    }

    // First sample only sets the starting point for the rate counters.
    status = PdhCollectQueryData(query_);
    if (status != ERROR_SUCCESS) {
        error = pdhErrorText("PdhCollectQueryData (first sample)", status);
        close();
        return false;
    }
    return true;
}

bool MetricsCollector::sampleOnce(CpuMemReading& reading, std::string& error) {
    reading = CpuMemReading();
    reading.timestamp = nowUtc();

    if (!isOpen()) {
        error = "Collector is not initialised";
        return false;
    }

    PDH_STATUS status = PdhCollectQueryData(query_);
    if (status != ERROR_SUCCESS) {
        error = pdhErrorText("PdhCollectQueryData", status);
        return false;
    }

    double value = 0.0;

    // CPU: total processor time in percent.
    if (readCounter(cpuCounter_, value) && value >= 0.0) {
        reading.cpuPercent = (value > 100.0) ? 100.0 : value;
        reading.cpuValid = true;
    }

    // RAM: available MB from PDH, used % worked out from total RAM.
    if (readCounter(availableCounter_, value) && value >= 0.0 && totalRamMB_ > 0.0) {
        if (value > totalRamMB_) {
            value = totalRamMB_;
        }
        reading.ramTotalMB = totalRamMB_;
        reading.ramAvailableMB = value;
        reading.ramUsedPercent = (totalRamMB_ - value) / totalRamMB_ * 100.0;
        reading.ramValid = true;
    }

    // Committed memory is virtual memory, kept separate from physical RAM.
    if (readCounter(committedCounter_, value) && value >= 0.0) {
        reading.committedPercent = value;
        reading.committedValid = true;
    }

    // Page faults per second (soft and hard faults together).
    if (readCounter(pageFaultCounter_, value) && value >= 0.0) {
        reading.pageFaultsPerSec = value;
        reading.pageFaultsValid = true;
    }

    return true;
}

void MetricsCollector::close() {
    // Closing the query also releases all counters added to it.
    if (query_ != nullptr) {
        PdhCloseQuery(query_);
    }
    query_ = nullptr;
    cpuCounter_ = nullptr;
    availableCounter_ = nullptr;
    committedCounter_ = nullptr;
    pageFaultCounter_ = nullptr;
}

}  // namespace iris