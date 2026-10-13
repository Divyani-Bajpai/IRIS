
#include <iostream>
#include <windows.h>
#include <pdh.h>

using namespace std;

int main() {
    PDH_HQUERY query;
    PDH_HCOUNTER queueCounter, diskTimeCounter;

    PDH_STATUS status = PdhOpenQueryW(NULL, 0, &query);

    if (status != ERROR_SUCCESS) {
        cout << "Failed to open PDH query.\n";
        return 1;
    }

    status = PdhAddEnglishCounterW(
        query,
        L"\\PhysicalDisk(_Total)\\Current Disk Queue Length",
        0,
        &queueCounter
    );

    if (status != ERROR_SUCCESS) {
        cout << "Failed to add Disk Queue Length counter.\n";
        PdhCloseQuery(query);
        return 1;
    }

    status = PdhAddEnglishCounterW(
        query,
        L"\\PhysicalDisk(_Total)\\% Disk Time",
        0,
        &diskTimeCounter
    );

    if (status != ERROR_SUCCESS) {
        cout << "Failed to add Disk Time counter.\n";
        PdhCloseQuery(query);
        return 1;
    }

    // First collection initializes the counters.
    status = PdhCollectQueryData(query);

    if (status != ERROR_SUCCESS) {
        cout << "First data collection failed.\n";
        PdhCloseQuery(query);
        return 1;
    }

    // Allow time for Windows to calculate counter values.
    Sleep(1000);

    status = PdhCollectQueryData(query);

    if (status != ERROR_SUCCESS) {
        cout << "Second data collection failed.\n";
        PdhCloseQuery(query);
        return 1;
    }

    PDH_FMT_COUNTERVALUE queueValue;
    PDH_FMT_COUNTERVALUE diskTimeValue;

    PDH_STATUS queueStatus = PdhGetFormattedCounterValue(
        queueCounter,
        PDH_FMT_DOUBLE,
        NULL,
        &queueValue
    );

    PDH_STATUS diskTimeStatus = PdhGetFormattedCounterValue(
        diskTimeCounter,
        PDH_FMT_DOUBLE,
        NULL,
        &diskTimeValue
    );

    cout << "\nPhysical Disk (_Total) Metrics\n";
    cout << "------------------------------\n";

    if (queueValue.CStatus == 0x00000000 || queueValue.CStatus == 0x00000001) {
        cout << "Disk Queue Length: "
             << queueValue.doubleValue << '\n';
    } else {
        cout << "Disk Queue Length: Data unavailable\n";
    }

    if(diskTimeValue.CStatus == 0x00000000 ||diskTimeValue.CStatus == 0x00000001) {
        cout << "Disk Time: "
             << diskTimeValue.doubleValue << "%\n";
    } else {
        cout << "Disk Time: Data unavailable\n";
    }

    PdhCloseQuery(query);

    return 0;
}
