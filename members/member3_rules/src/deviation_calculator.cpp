#include "deviation_calculator.h"

#include <cmath>

bool deviationCalculation(double current, double average, double sd,
                          double& result) {
    result = 0.0;
    if (!std::isfinite(current) || !std::isfinite(average) ||
        !std::isfinite(sd) || sd <= 0.0) {
        return false;
    }
    const double deviation = (current - average) / sd;
    if (!std::isfinite(deviation)) {
        return false;
    }
    result = deviation;
    return true;
}