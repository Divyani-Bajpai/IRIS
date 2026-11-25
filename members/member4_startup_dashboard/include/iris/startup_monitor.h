#pragma once

#include <string>
#include <vector>

#include "iris/shared_metrics_store.h"

namespace iris {

// Reads the current user's and local machine's Registry Run entries.
std::vector<StartupAppRecord> collectStartupApps(
    std::vector<std::string>& warnings);

}  // namespace iris
