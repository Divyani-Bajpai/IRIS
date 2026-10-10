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
**Institution:** Graphic Era (Deemed to be University), Dehradun  
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
- Records approximate boot-time information.

### 6. User Interface

- Provides an interface for displaying system metrics, diagnostic results, recommendations, and technical details.
- UI integration with live data is still in progress.

## System Architecture

The intended workflow of IRIS is:

**Windows APIs → Data Collection → SQLite Database → Correlation and Rule Engine → User Interface**

## Setup and Installation

IRIS is designed for Windows because it uses Windows-specific APIs.

### Prerequisites

- Windows operating system
- Visual Studio Code
- A compatible C++ compiler and build tools
- SQLite library and headers required by the project
- Git

### Getting the Project

Clone the repository:

```bash
git clone https://github.com/Divyani-Bajpai/IRIS.git
```

Open the project folder in Visual Studio Code.

**Note:** The exact compiler, build system, dependency configuration, and build commands have not yet been confirmed. The verified build instructions will be added once the project configuration is finalized.

## Current Project Status

### Completed or Developed

- SQLite connectivity and local data-storage foundation
- Default baseline for initial diagnosis
- Startup application enumeration
- Approximate boot-time logging
- CPU and memory monitoring module
- Disk and process monitoring module
- Initial UI structure
- Initial testing of UI/data flow using dummy data

### Work in Progress

- Integration of all modules through a common data flow
- Machine-specific baseline generation using historical data
- Connection of the UI to live system data
- Completion and validation of diagnostic rules
- Correlation of multiple metrics for root-cause identification
- End-to-end testing and performance evaluation

The features listed above reflect the reported project progress. Their current implementation and working status must be verified against the source code.

## Future Work

- Complete integration of all modules.
- Improve root-cause identification through metric correlation.
- Validate diagnostic rules under normal and slow-performance conditions.
- Connect the UI to live performance data.
- Improve the reliability of baseline generation and recommendations.
- Perform end-to-end testing and performance evaluation.

## Repository

[IRIS GitHub Repository](https://github.com/Divyani-Bajpai/IRIS)
