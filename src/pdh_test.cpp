#include <iostream>
#include <windows.h>
#include <pdh.h>
#include <vector>
#include <string>

#pragma comment(lib, "pdh.lib")

using namespace std;

struct DriveCounter {
    char driveLetter;

    PDH_HCOUNTER readCounter;
    PDH_HCOUNTER writeCounter;
};

int main() {

    // 1. Find all logical drives
    DWORD drives = GetLogicalDrives();

    if (drives == 0) {
        cout << "Failed to get logical drives." << endl;
        return 1;
    }

    // 2. Open PDH query
    PDH_HQUERY query;

    PDH_STATUS status = PdhOpenQueryW(
        nullptr,
        0,
        &query
    );

    if (status != ERROR_SUCCESS) {
        cout << "Failed to open PDH query." << endl;
        return 1;
    }

    vector<DriveCounter> counters;

    // 3. Check all possible drives A-Z
    for (int i = 0; i < 26; i++) {

        // Drive does not exist
        if (!(drives & (1 << i))) {
            continue;
        }

        char driveLetter = 'A' + i;

        // Build drive path
        wstring drivePath;
        drivePath += static_cast<wchar_t>(driveLetter);
        drivePath += L":\\";

        // Check drive type
        UINT driveType = GetDriveTypeW(drivePath.c_str());

        // Only monitor fixed drives
        if (driveType != DRIVE_FIXED) {
            continue;
        }

        // Build PDH counter paths
        wstring readPath =
            L"\\LogicalDisk(" +
            wstring(1, static_cast<wchar_t>(driveLetter)) +
            L":)\\Disk Read Bytes/sec";

        wstring writePath =
            L"\\LogicalDisk(" +
            wstring(1, static_cast<wchar_t>(driveLetter)) +
            L":)\\Disk Write Bytes/sec";

        PDH_HCOUNTER readCounter;
        PDH_HCOUNTER writeCounter;

        // Add read counter
        status = PdhAddEnglishCounterW(
            query,
            readPath.c_str(),
            0,
            &readCounter
        );

        if (status != ERROR_SUCCESS) {
            cout << "Failed to add read counter for " << driveLetter << ":" << endl;
            continue;
        }

        // Add write counter
        status = PdhAddEnglishCounterW(
            query,
            writePath.c_str(),
            0,
            &writeCounter
        );

        if (status != ERROR_SUCCESS) {
            cout << "Failed to add write counter for "
                 << driveLetter << ":" << endl;
            continue;
        }

        counters.push_back({
            driveLetter,
            readCounter,
            writeCounter
        });
    }

    if (counters.empty()) {
        cout << "No fixed drives found." << endl;
        PdhCloseQuery(query);
        return 1;
    }

    // 4. First sample
    status = PdhCollectQueryData(query);

    if (status != ERROR_SUCCESS) {
        cout << "Failed to collect first sample." << endl;
        PdhCloseQuery(query);
        return 1;
    }

    // 5. Wait 1 second
    Sleep(1000);

    // 6. Second sample
    status = PdhCollectQueryData(query);

    if (status != ERROR_SUCCESS) {
        cout << "Failed to collect second sample." << endl;
        PdhCloseQuery(query);
        return 1;
    }

    // 7. Get values for every drive
    for (const DriveCounter& drive : counters) {

        PDH_FMT_COUNTERVALUE readValue;
        PDH_FMT_COUNTERVALUE writeValue;

        status = PdhGetFormattedCounterValue(
            drive.readCounter,
            PDH_FMT_DOUBLE,
            nullptr,
            &readValue
        );

        if (status != ERROR_SUCCESS) {
            cout << "Failed to get read value for " << drive.driveLetter << ":" << endl;
            continue;
        }

        status = PdhGetFormattedCounterValue(drive.writeCounter, PDH_FMT_DOUBLE,nullptr,&writeValue);

        if (status != ERROR_SUCCESS) {
            cout << "Failed to get write value for "<< drive.driveLetter << ":" << endl;
            continue;
        }

        cout << "\nDrive: "<< drive.driveLetter<< ":\\" << endl;

        cout << "Disk Read: "<< readValue.doubleValue<< " bytes/sec" << endl;

        cout << "Disk Write: "<< writeValue.doubleValue<< " bytes/sec" << endl;
    }

    // 8. Close PDH query
    PdhCloseQuery(query);

    return 0;
}