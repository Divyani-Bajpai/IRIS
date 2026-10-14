#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "database.h"
#include "system_setup.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace iris {
namespace {
constexpr double kBytesPerGb = 1024.0 * 1024.0 * 1024.0;
constexpr double kBytesPerMb = 1024.0 * 1024.0;

struct BaselineDefaults {
    const char* tier;
    double cpuAverage, cpuStdDev;
    double pageFaultsAverage, pageFaultsStdDev;
    double diskQueueAverage, diskQueueStdDev;
    double diskTimeAverage, diskTimeStdDev;
};

BaselineDefaults defaultsForRam(double ramGb) {
    // The project brief does not specify 5–7 GB or 17–31 GB; these use
    // Mid and High respectively as a documented fallback.
    if (ramGb <= 4.0) return {"Low", 15.0, 12.0, 60.0, 50.0, 0.25, 0.35, 12.0, 18.0};
    if (ramGb <= 16.0) return {"Mid", 15.0, 12.0, 45.0, 40.0, 0.20, 0.30, 10.0, 15.0};
    return {"High", 12.0, 10.0, 35.0, 30.0, 0.15, 0.25, 8.0, 12.0};
}

void insertBaselineIfMissing(sqlite3* db, const char* name, double average, double stddev) {
    Statement statement(db,
        "INSERT OR IGNORE INTO baselines"
        " (metric_name, average_value, standard_deviation, last_updated, source)"
        " VALUES (?, ?, ?, CURRENT_TIMESTAMP, 'default');");
    sqlite3_bind_text(statement.get(), 1, name, -1, SQLITE_STATIC);
    sqlite3_bind_double(statement.get(), 2, average);
    sqlite3_bind_double(statement.get(), 3, stddev);
    statement.execute();
}

std::string wideToUtf8(const wchar_t* value) {
    if (!value || !*value) return {};
    const int required = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1) return {};
    std::string result(static_cast<std::size_t>(required), '\0');
    const int written = WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), required, nullptr, nullptr);
    if (written <= 1) return {};
    result.resize(static_cast<std::size_t>(written - 1)); // remove trailing NUL
    return result;
}

void insertStartupApp(sqlite3* db, const std::string& appName, const std::string& location) {
    Statement statement(db,
        "INSERT INTO startup_apps (timestamp, app_name, registry_location)"
        " VALUES (CURRENT_TIMESTAMP, ?, ?);");
    sqlite3_bind_text(statement.get(), 1, appName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement.get(), 2, location.c_str(), -1, SQLITE_TRANSIENT);
    statement.execute();
}

void collectRunKey(sqlite3* db, HKEY root, const wchar_t* subkey, const char* rootName) {
    HKEY key = nullptr;
    const LONG openResult = RegOpenKeyExW(root, subkey, 0, KEY_READ, &key);
    if (openResult == ERROR_FILE_NOT_FOUND || openResult == ERROR_PATH_NOT_FOUND) return;
    if (openResult != ERROR_SUCCESS) {
        std::cerr << "Could not read startup key " << rootName << "\\Run (error " << openResult << ").\n";
        return;
    }

    DWORD maxNameLength = 0, maxDataLength = 0;
    const LONG infoResult = RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, &maxNameLength, &maxDataLength, nullptr, nullptr);
    if (infoResult != ERROR_SUCCESS) {
        RegCloseKey(key);
        std::cerr << "Could not inspect startup key " << rootName << "\\Run.\n";
        return;
    }

    std::vector<wchar_t> name(static_cast<std::size_t>(maxNameLength) + 2);
    std::vector<BYTE> data(static_cast<std::size_t>(std::max<DWORD>(maxDataLength, 2)) + sizeof(wchar_t));
    DWORD index = 0;
    for (;;) {
        DWORD nameLength = static_cast<DWORD>(name.size());
        DWORD dataLength = static_cast<DWORD>(data.size());
        DWORD valueType = 0;
        const LONG result = RegEnumValueW(key, index, name.data(), &nameLength, nullptr,
            &valueType, data.data(), &dataLength);
        if (result == ERROR_NO_MORE_ITEMS) break;
        if (result == ERROR_MORE_DATA) {
            name.resize(name.size() * 2);
            data.resize(data.size() * 2);
            continue; // retry the same registry value after resizing buffers
        }
        if (result != ERROR_SUCCESS) {
            std::cerr << "A startup registry value could not be read (error " << result << ").\n";
            ++index;
            continue;
        }
        insertStartupApp(db, wideToUtf8(name.data()),
            std::string(rootName) + "\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
        ++index;
    }
    RegCloseKey(key);
}
} // namespace

void bootstrapDefaultBaselines(sqlite3* database) {
    MEMORYSTATUSEX memoryInfo{};
    memoryInfo.dwLength = sizeof(memoryInfo);
    if (!GlobalMemoryStatusEx(&memoryInfo)) throw std::runtime_error("GlobalMemoryStatusEx failed.");

    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const double ramGb = static_cast<double>(memoryInfo.ullTotalPhys) / kBytesPerGb;
    const double ramMb = static_cast<double>(memoryInfo.ullTotalPhys) / kBytesPerMb;
    const BaselineDefaults defaults = defaultsForRam(ramGb);

    // Fill missing rows only: never overwrite a learned baseline or a row
    // already created by another module. A partial DB is repaired safely.
    executeSql(database, "BEGIN IMMEDIATE TRANSACTION;");
    try {
        insertBaselineIfMissing(database, "cpu_percent", defaults.cpuAverage, defaults.cpuStdDev);
        insertBaselineIfMissing(database, "available_ram_mb", ramMb * 0.35, ramMb * 0.15);
        insertBaselineIfMissing(database, "page_faults_per_sec", defaults.pageFaultsAverage, defaults.pageFaultsStdDev);
        insertBaselineIfMissing(database, "disk_queue_length", defaults.diskQueueAverage, defaults.diskQueueStdDev);
        insertBaselineIfMissing(database, "percent_disk_time", defaults.diskTimeAverage, defaults.diskTimeStdDev);
        executeSql(database, "COMMIT;");
    } catch (...) {
        try { executeSql(database, "ROLLBACK;"); } catch (...) {}
        throw;
    }

    std::cout << "Default baseline bootstrap checked for " << defaults.tier << " tier ("
              << ramGb << " GB RAM, " << systemInfo.dwNumberOfProcessors << " logical CPU cores).\n";
}

void collectStartupApps(sqlite3* database) {
    // This records a snapshot on each scan. If the UI needs only the latest
    // snapshot, it should query rows from the latest timestamp.
    executeSql(database, "BEGIN IMMEDIATE TRANSACTION;");
    try {
        collectRunKey(database, HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", "HKCU");
        collectRunKey(database, HKEY_LOCAL_MACHINE,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", "HKLM");
        executeSql(database, "COMMIT;");
    } catch (...) {
        try { executeSql(database, "ROLLBACK;"); } catch (...) {}
        throw;
    }
}

void recordFirstMetricCollection(sqlite3* database,
    std::chrono::steady_clock::time_point processStart) {
    static bool recorded = false;
    if (recorded) return;
    const double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - processStart).count();
    Statement statement(database,
        "INSERT INTO boot_times (timestamp, boot_duration_seconds) VALUES (CURRENT_TIMESTAMP, ?);");
    sqlite3_bind_double(statement.get(), 1, elapsed);
    statement.execute();
    recorded = true;
}

} // namespace iris
