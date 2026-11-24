# IRIS: Intelligent Root-cause Identification System

IRIS is a Windows-based application designed to identify the possible reasons behind a computer's slow performance. It continuously monitors system performance, stores data locally, and aims to identify root causes by correlating multiple performance metrics.

## Problem Statement

When a computer becomes slow, it can be difficult to identify the actual reason. High CPU usage, memory pressure, disk activity, page faults, and background processes can all affect system performance. IRIS aims to analyse these factors together and help users understand the root cause and possible solution.

## Objectives

1. To study existing Windows diagnostic tools and identify their capabilities and limitations.
2. To continuously monitor key system metrics  such as CPU, memory, disk, page faults, and processes.
3. To correlate multiple performance metrics to identify the actual cause of system slowdowns.
4. To store historical performance data using SQLite and build a machine-specific baseline.
5. To present results in simple language so that non-technical users can understand the root cause and solution.

## Team Members

- **Saurav Singh Samant** – Team Lead, UI and Integration
- **Divyani Bajpai** – Disk Activity and Process Monitoring
- **Mohit Kumar Yadav** – CPU and Memory Monitoring
- **Aditya Mastwal** – Rule Engine and Diagnostic Rules

**Team Name:** Team Elite  
**Project ID:** OSDBMS-V-2026-T014  
**Institution:** Graphic Era Deemed to be University, Dehradun  
**Academic Session:** 2026–27

## Technologies and Tools Used

- C++
- Windows APIs
- PDH for performance monitoring
- PSAPI and ToolHelp32 for process-related information, where applicable
- SQLite for local data storage
- Visual Studio Code
- Git and GitHub

## Major Features and Modules

### 1. CPU and Memory Monitoring

- Collects CPU and RAM usage information.
- Monitors performance-related metrics, including page faults.

### 2. Disk and Process Monitoring

- Collects disk activity information.
- Tracks process-level disk and memory usage.

### 3. Baseline Generation

- Uses a default baseline when sufficient historical data is unavailable.
- Aims to build a machine-specific baseline from historical performance data using averages and standard deviation.

### 4. Rule Engine and Root-Cause Diagnosis

- Correlates performance metrics to identify possible causes of system slowdowns.
- Includes diagnostic rules targeting Memory Thrashing and Disk I/O Contention.
- Aims to provide understandable diagnostic results and recommendations.

### 5. Startup Analysis

- Identifies startup applications using Windows system information.

### 6. User Interface

- Provides an interface for displaying system metrics, diagnostic results, recommendations, and technical details.
- UI integration with live data is still in progress.

## System Architecture

The intended workflow of IRIS is:

**Windows APIs → Data Collection → SQLite Database → Correlation and Rule Engine → User Interface**

## Setup and Installation

IRIS is designed for Windows because it uses Windows-specific APIs.

## Prerequisites

- Windows
- Git for Windows
- MSYS2 with the MINGW64 environment
- Visual Studio Code (optional)

In the MSYS2 MINGW64 terminal, install the required packages:

```bash
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-sqlite3 mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja
```

## Get, build, and run IRIS

Clone the repository:

```bash
git clone https://github.com/Divyani-Bajpai/IRIS.git
cd IRIS
```

Build the project in the MSYS2 MINGW64 terminal:

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

Run the included tests:

```bash
ctest --test-dir build --output-on-failure
```

Start IRIS:

```bash
./build/iris_monitor.exe ./iris.db
```

IRIS opens its report in a browser at `http://127.0.0.1:8765` or the next available local port. Click **Start 60-second check** to collect readings. Keep the terminal open while using the report. Press Ctrl+C in the terminal to close IRIS.

## Current project status

The project includes:

- CPU, memory, disk, and process collectors
- Local SQLite storage for readings, baselines, startup apps, and diagnoses
- Starting baseline estimates and machine-specific baselines learned from saved history
- Two diagnosis rules: possible memory thrashing and possible disk I/O contention
- A local browser dashboard with live collection details and suggested next steps
- Tests for CPU calculations, SQLite storage, diagnosis rules, and process collection

Learned baselines require at least seven days of history and 500 eligible readings per metric. Until then, IRIS uses provisional starting estimates. The baseline stays fixed during each check.

IRIS records startup applications from Windows Registry Run entries. It does **not** measure Windows boot duration. Per-process I/O readings can include cached I/O, and Windows may prevent access to some protected processes.

## Future work

- Add and validate more diagnosis rules.
- Evaluate the recommendations using more real-world PC conditions.
- Expand tests for complete collection and reporting.

## Repository

[IRIS GitHub Repository](https://github.com/Divyani-Bajpai/IRIS)
