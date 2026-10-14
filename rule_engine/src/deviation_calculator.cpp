#include <cmath>
#include "deviation_calculator.h"

using namespace std;

bool deviationCalculation(double current, double average, double sd, double &result){
    result = 0.0;

    // Deviation Must be Positive
    if (!isfinite(current) || !isfinite(average) || !isfinite(sd) || sd <= 0){ // Check for finit or Not & Standard Deviation Check
        return false;
    }

    double deviation = (current - average) / sd;  // Deviation Calculation

    // Reject invalid and overflowing result
    if (!isfinite(deviation)){
        return false;
    }

    result = deviation;
    return true;
}