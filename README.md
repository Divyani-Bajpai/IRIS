
# IRIS: Intelligent Root-cause Identification System

IRIS is a Windows-based application designed to identify possible reasons behind a computer's slow performance. It collects system performance data, stores it locally, and aims to provide useful diagnostic results and recommendations.

## Problem Statement

When a computer becomes slow, it can be difficult to identify the actual reason. High CPU usage, memory pressure, disk activity, and background processes can all affect performance. IRIS aims to analyse these factors and help users understand the possible root cause.

## Team Members

- **Saurav Singh Samant** – Team Lead, UI and Integration
- **Divyani Bajpai** – Disk Activity and Process Monitoring
- **Mohit Kumar Yadav** – CPU and Memory Monitoring
- **Aditya Mastwal** – Rule Engine and Diagnostic Rules

**Team Name:** Team Elite
**Project ID:** OSDBMS-V-2026-T014
**Institution:** Graphic Era (Deemed to be University), Dehradun

## Technologies Used

- C++
- Windows APIs
- SQLite
- Visual Studio Code
- Git and GitHub

## Key Features

### 1. CPU and Memory Monitoring
- Collects CPU and RAM usage information.
- Monitors performance-related metrics, including page faults.

### 2. Disk and Process Monitoring
- Collects disk activity information.
- Tracks process-level disk and memory usage.

### 3. Baseline Generation
- Uses a default baseline when sufficient historical data is unavailable.
- Plans to build machine-specific baselines using averages and standard deviation.

### 4. Rule-Based Diagnosis
- Uses performance metrics and diagnostic rules to identify possible issues.
- Includes rules targeting Memory Thrashing and Disk I/O Contention.

### 5. Startup Analysis
- Identifies startup applications using Windows system information.
- Records approximate boot-time information.

### 6. User Interface
- Provides a developing interface for performance metrics, diagnostic results, recommendations, and technical details.

## System Architecture

The planned workflow is:

Windows APIs → Data Collection → SQLite Database → Correlation and Rule Engine → User Interface

## Setup Requirements

IRIS is designed for Windows because it uses Windows-specific APIs.

Before building the project, you will need:

- Windows OS
- Visual Studio Code
- A compatible C++ compiler and build tools
- SQLite library and headers required by the project
- Git

### Getting the Project

Clone the repository:

```bash
git clone https://github.com/Divyani-Bajpai/IRIS.git
```

Open the downloaded project folder in Visual Studio Code.

**Note:** The exact compiler, build system, and build commands are still being finalized. They will be added after the project configuration is confirmed.

## Current Project Status

### Completed or Developed
- SQLite connectivity and local data-storage foundation
- Default baseline for initial diagnosis
- Startup application enumeration
- Approximate boot-time logging
- Initial CPU and memory monitoring
- Disk and process monitoring modules
- Initial UI structure and testing with dummy data

### Work in Progress
- Integration of all modules through a common data flow
- Machine-specific baseline generation
- Connection of the UI to live system data
- Completion and validation of diagnostic rules
- End-to-end testing and performance evaluation

## Future Work

- Complete integration of all modules.
- Improve root-cause identification and diagnostic accuracy.
- Validate rules under normal and slow-performance conditions.
- Connect the UI to live performance data.
- Test the complete application for reliability and performance.

## Repository

GitHub: https://github.com/Divyani-Bajpai/IRIS
````
