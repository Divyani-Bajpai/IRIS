#include <iostream>
#include <cmath>

using namespace std;

bool deviationCalculation(double current, double average, double sd, double &result)
{
    result = 0.0;

    // Deviation Must be Positive
    if (!isfinite(current) || !isfinite(average) || !isfinite(sd) || sd <= 0)
    {
        return false;
    }

    double deviation = (current - average) / sd;

    // Reject invalid and overflowing result
    if (!isfinite(deviation))
    {
        return false;
    }

    result = deviation;
    return true;
}

int main()
{
    double result;

    // Test 1 : Normal Calculation
    if (deviationCalculation(120, 40, 20, result))
    {
        cout << "Test 1 : " << result << endl;
    }
    else
    {
        cout << "Test 1 : Invalid input" << endl;
    }

    // Test 2 : Current value equal to average
    if (deviationCalculation(40, 40, 20, result))
    {
        cout << "Test 2 : " << result << endl;
    }
    else
    {
        cout << "Test 2 : Invalid input" << endl;
    }

    // Test 3 : Standard deviation is zero
    if (deviationCalculation(120, 40, 0, result))
    {
        cout << "Test 3 : " << result << endl;
    }
    else
    {
        cout << "Test 3 : Invalid input" << endl;
    }

    // Test 4 : Deviation is Negative
    if (deviationCalculation(120, 40, -5, result))
    {
        cout << "Test 4 : " << result << endl;
    }
    else
    {
        cout << "Test 4 : Invalid input" << endl;
    }

    return 0;
}
