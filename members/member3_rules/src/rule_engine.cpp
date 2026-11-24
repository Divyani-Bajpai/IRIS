#include "rule_engine.h"

#include <cmath>

RuleResult evaluateRules(const MetricScores& scores) {
    RuleResult result;
    result.diagnosis = "No matching rule";
    result.reason = "No rule conditions matched";

    // Each rule checks only the metrics it needs. This lets disk diagnosis work
    // when an unrelated CPU or page-fault counter is temporarily unavailable.
    if (std::isfinite(scores.pageFaultZ) &&
        std::isfinite(scores.availableRamZ) &&
        std::isfinite(scores.cpuZ) &&
        scores.pageFaultZ > 2.0 &&
        scores.availableRamZ < -2.0 &&
        scores.cpuZ >= -1.0 && scores.cpuZ <= 1.0) {
        result.matched = true;
        result.ruleName = "memory_thrashing";
        result.diagnosis = "Possible Memory Thrashing";
        result.reason = "Page faults are much higher than normal, available RAM is much lower, and CPU usage is near its baseline.";
        result.recommendationText =
            "Memory Thrashing detected. Your PC does not have enough free memory for what is currently running, so it is constantly swapping data to disk. Close memory-heavy apps or add more RAM.";
        result.suggestions = {
            "Close apps and browser tabs you are not using. In Task Manager, open the Processes tab and sort by Memory to see which ones use the most.",
            "Restart the apps that use the most memory. Some apps slowly use more memory the longer they stay open.",
            "Restart your PC if it has been running for many days without a restart. This clears memory that apps have not released.",
            "Turn off startup apps you do not need (Task Manager > Startup apps) so less memory is used right after sign-in.",
            "Avoid running many large programs at the same time, such as several browser windows, video editors, games or virtual machines.",
            "If this keeps happening, install more RAM. It is the lasting fix when your PC regularly runs out of free memory."
        };
        return result;
    }

    if (std::isfinite(scores.diskQueueZ) &&
        std::isfinite(scores.diskTimeZ) &&
        std::isfinite(scores.availableRamZ) &&
        scores.diskQueueZ > 2.0 &&
        scores.diskTimeZ > 2.0 &&
        scores.availableRamZ >= -1.0 &&
        scores.availableRamZ <= 1.0) {
        result.matched = true;
        result.ruleName = "disk_io_contention";
        result.diagnosis = "Possible Disk I/O Contention";
        result.reason = "Disk queue and disk activity are high while available RAM is near its baseline.";
        result.recommendationText =
            "Disk I/O Contention detected. Something is heavily using your disk right now. Check for background downloads, backups, or updates.";
        result.suggestions = {
            "Find what is using the disk: in Task Manager, open the Processes tab and sort by Disk. The IRIS process activity table shows the same read and write activity.",
            "Let Windows Update, backups, cloud sync (such as OneDrive) and antivirus scans finish, or pause them until you are done working.",
            "Pause large downloads and large file copies while you use your PC.",
            "Turn off startup apps you do not need (Task Manager > Startup apps) so fewer programs read and write the disk at sign-in.",
            "Keep free space on your drive. A nearly full drive slows down; remove files you no longer need or move them to another drive.",
            "If your PC uses a hard disk drive (HDD), upgrading to a solid-state drive (SSD) is the lasting fix for disk slowdowns."
        };
        return result;
    }
    return result;
}