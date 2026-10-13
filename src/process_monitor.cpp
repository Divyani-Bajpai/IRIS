
#include <iostream>
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <iomanip>
#include <vector>
#include <string>
#include <chrono>
#include <thread>

using namespace std;
using namespace chrono;

struct ProcessData {
    DWORD pid;
    wstring name;
    double memoryMB;
    unsigned long long cpuTime;
    steady_clock::time_point sampleTime;
};

bool getCpuTime(HANDLE processHandle, unsigned long long& cpuTime) {
    FILETIME creationTime, exitTime, kernelTime, userTime;

    if (!GetProcessTimes(
            processHandle,
            &creationTime,
            &exitTime,
            &kernelTime,
            &userTime)) {
        return false;
    }

    ULARGE_INTEGER kernel, user;
    kernel.LowPart = kernelTime.dwLowDateTime;
    kernel.HighPart = kernelTime.dwHighDateTime;
    user.LowPart = userTime.dwLowDateTime;
    user.HighPart = userTime.dwHighDateTime;

    // Windows FILETIME units are 100 nanoseconds.
    cpuTime = kernel.QuadPart + user.QuadPart;

    return true;
}

int main() {
    SYSTEM_INFO systemInfo;
    GetSystemInfo(&systemInfo);

    DWORD logicalProcessors = systemInfo.dwNumberOfProcessors;

    if (logicalProcessors == 0) {
        cout << "Could not determine logical processor count.\n";
        return 1;
    }

    vector<ProcessData> processes;

    HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPPROCESS, 0
    );

    if (snapshot == INVALID_HANDLE_VALUE) {
        cout << "Failed to create process snapshot.\n";
        return 1;
    }

    PROCESSENTRY32W process;
    process.dwSize = sizeof(PROCESSENTRY32W);

    if (!Process32FirstW(snapshot, &process)) {
        cout << "Failed to retrieve processes.\n";
        CloseHandle(snapshot);
        return 1;
    }

    // First sample: process name, memory and accumulated CPU time.
    do {
        HANDLE processHandle = OpenProcess(
            PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
            FALSE,
            process.th32ProcessID
        );

        if (processHandle == NULL) {
            continue;
        }

        PROCESS_MEMORY_COUNTERS memoryInfo;
        memoryInfo.cb = sizeof(memoryInfo);

        unsigned long long cpuTime = 0;

        bool memoryAvailable = GetProcessMemoryInfo(
            processHandle,
            &memoryInfo,
            sizeof(memoryInfo)
        );

        bool cpuAvailable = getCpuTime(processHandle, cpuTime);

        if (memoryAvailable && cpuAvailable) {
            ProcessData data;

            data.pid = process.th32ProcessID;
            data.name = process.szExeFile;
            data.memoryMB =
                static_cast<double>(memoryInfo.WorkingSetSize)
                / (1024.0 * 1024.0);
            data.cpuTime = cpuTime;
            data.sampleTime = steady_clock::now();

            processes.push_back(data);
        }

        CloseHandle(processHandle);

    } while (Process32NextW(snapshot, &process));

    CloseHandle(snapshot);

    // Allow CPU time to accumulate between samples.
    this_thread::sleep_for(seconds(1));

    cout << fixed << setprecision(2);

    wcout << left
          << setw(35) << L"Process Name"
          << setw(12) << L"PID"
          << setw(16) << L"Memory (MB)"
          << L"CPU (%)\n";

    wcout << L"--------------------------------------------------------------------------\n";

    // Second sample: calculate CPU utilization.
    for (const ProcessData& oldData : processes) {
        HANDLE processHandle = OpenProcess(
            PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
            FALSE,
            oldData.pid
        );

        if (processHandle == NULL) {
            continue;
        }

        unsigned long long currentCpuTime = 0;

        if (getCpuTime(processHandle, currentCpuTime)) {
            auto currentTime = steady_clock::now();

            double elapsedSeconds =
                duration<double>(
                    currentTime - oldData.sampleTime
                ).count();

            if (elapsedSeconds > 0 &&
                currentCpuTime >= oldData.cpuTime) {

                double cpuSeconds =
                    static_cast<double>(
                        currentCpuTime - oldData.cpuTime
                    ) / 10000000.0;

                double cpuPercent =
                    (cpuSeconds /
                     (elapsedSeconds * logicalProcessors)) * 100.0;

                wcout << left
                      << setw(35) << oldData.name
                      << setw(12) << oldData.pid
                      << setw(16) << oldData.memoryMB
                      << cpuPercent << L'\n';
            }
        }

        CloseHandle(processHandle);
    }

    return 0;
}
