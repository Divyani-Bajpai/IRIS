#define WIN32_LEAN_AND_MEAN
#include "iris/startup_monitor.h"

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace iris {
namespace {

std::string wideToUtf8(const wchar_t* value) {
    if (value == nullptr || *value == L'\0') return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0,
                                           nullptr, nullptr);
    if (length <= 1) return {};
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), length,
                        nullptr, nullptr);
    result.pop_back();
    return result;
}

void collectRunKey(HKEY root, const wchar_t* subkey, const char* rootName,
                   std::vector<StartupAppRecord>& apps,
                   std::vector<std::string>& warnings) {
    HKEY key = nullptr;
    const LONG openStatus = RegOpenKeyExW(root, subkey, 0, KEY_READ, &key);
    if (openStatus == ERROR_FILE_NOT_FOUND || openStatus == ERROR_PATH_NOT_FOUND) return;
    if (openStatus != ERROR_SUCCESS) {
        warnings.emplace_back(std::string("Could not read startup key ") + rootName
                              + "\\Run (Windows error "
                              + std::to_string(openStatus) + ")");
        return;
    }

    DWORD maxNameLength = 0;
    DWORD maxDataLength = 0;
    const LONG infoStatus = RegQueryInfoKeyW(key, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, &maxNameLength, &maxDataLength,
        nullptr, nullptr);
    if (infoStatus != ERROR_SUCCESS) {
        RegCloseKey(key);
        warnings.emplace_back(std::string("Could not inspect startup key ") + rootName
                              + "\\Run (Windows error "
                              + std::to_string(infoStatus) + ")");
        return;
    }

    std::vector<wchar_t> name(static_cast<std::size_t>(maxNameLength) + 2);
    std::vector<BYTE> data(static_cast<std::size_t>(maxDataLength) + sizeof(wchar_t));
    DWORD index = 0;
    for (;;) {
        DWORD nameLength = static_cast<DWORD>(name.size());
        DWORD dataLength = static_cast<DWORD>(data.size());
        DWORD type = 0;
        const LONG status = RegEnumValueW(key, index, name.data(), &nameLength,
                                           nullptr, &type, data.data(), &dataLength);
        if (status == ERROR_NO_MORE_ITEMS) break;
        if (status == ERROR_SUCCESS) {
            const std::string appName = wideToUtf8(name.data());
            if (!appName.empty()) {
                apps.push_back({appName, std::string(rootName)
                    + "\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"});
            }
            ++index;
            continue;
        }
        if (status == ERROR_MORE_DATA) {
            name.resize(name.size() * 2);
            data.resize(data.size() * 2);
            continue;
        }
        warnings.emplace_back(std::string("A startup entry could not be read (Windows error ")
                              + std::to_string(status) + ")");
        ++index;
    }
    RegCloseKey(key);
}

}  // namespace

std::vector<StartupAppRecord> collectStartupApps(
    std::vector<std::string>& warnings) {
    std::vector<StartupAppRecord> apps;
    collectRunKey(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", "HKCU",
        apps, warnings);
    collectRunKey(HKEY_LOCAL_MACHINE,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", "HKLM",
        apps, warnings);
    return apps;
}

}  // namespace iris
