#include <iostream>
#include <windows.h>
#include <iomanip>
#include <vector>
#include <string>

using namespace std;

struct DiskMetrics {
    char driveLetter;
    double totalGB;
    double freeGB;
    double usedGB;
    double usedPercentage;
};

class DiskMonitor {
public:
    vector<DiskMetrics> collect() {

        vector<DiskMetrics> allDrives;

        DWORD drives = GetLogicalDrives();

        if (drives == 0) {
            return allDrives;
        }

        const double BYTES_PER_GB = 1024.0 * 1024.0 * 1024.0;

        for (int i = 0; i < 26; i++) {

            if (!(drives & (1 << i))) {
                continue;
            }

            char driveLetter = 'A' + i;

            wstring drivePath;
            drivePath += static_cast<wchar_t>(driveLetter);
            drivePath += L":\\";

            UINT driveType = GetDriveTypeW(drivePath.c_str());

            if (driveType != DRIVE_FIXED) {
                continue;
            }

            ULARGE_INTEGER totalBytes;
            ULARGE_INTEGER totalFreeBytes;

            BOOL result = GetDiskFreeSpaceExW(
                drivePath.c_str(),
                nullptr,
                &totalBytes,
                &totalFreeBytes
            );

            if (!result) {
                continue;
            }

            double totalGB = totalBytes.QuadPart / BYTES_PER_GB;
            double freeGB = totalFreeBytes.QuadPart / BYTES_PER_GB;
            double usedGB = totalGB - freeGB;

            double usedPercentage = 0.0;

            if (totalGB > 0.0) {
                usedPercentage = (usedGB / totalGB) * 100.0;
            }

            DiskMetrics metrics{
                driveLetter,
                totalGB,
                freeGB,
                usedGB,
                usedPercentage
            };

            allDrives.push_back(metrics);
        }

        return allDrives;
    }
};

int main() {

    DiskMonitor monitor;

    vector<DiskMetrics> allDrives = monitor.collect();

    cout << fixed << setprecision(2);

    for (const DiskMetrics& metrics : allDrives) {

        cout << "\nDrive: "
             << metrics.driveLetter
             << ":\\" << endl;

        cout << "Total Disk Space: "
             << metrics.totalGB << " GB" << endl;

        cout << "Free Disk Space: "
             << metrics.freeGB << " GB" << endl;

        cout << "Used Disk Space: "
             << metrics.usedGB << " GB" << endl;

        cout << "Disk Usage: "
             << metrics.usedPercentage << "%" << endl;
    }

    return 0;
}