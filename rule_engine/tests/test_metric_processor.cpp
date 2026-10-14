
#include <iostream>
#include "metric_processor.h"

using namespace std;

int main()
{
    MetricInputs inputs = {
        {130, 70, 20, true},
        {19, 60, 20, true},
        {50, 50, 10, true},
        {10, 10, 5, true},
        {40, 40, 10, true}
    };

    MetricScores scores;
    const char *errorMessage = "";

    // Test 1: Process valid metric data
    if (!processMetrics(inputs, scores, errorMessage))
    {
        cout << "Processing failed: "
             << errorMessage << endl;
        return 1;
    }

    cout << "Page Faults Z-score: "
         << scores.pageFaultsZ << endl;

    cout << "Available RAM Z-score: "
         << scores.availableRamZ << endl;

    cout << "CPU Z-score: "
         << scores.cpuZ << endl;

    cout << "Disk Queue Z-score: "
         << scores.diskQueueZ << endl;

    cout << "Disk Time Z-score: "
         << scores.diskTimeZ << endl;

    // Test 2: Check the diagnosis
    RuleResult result = evaluateRules(scores);

    cout << "\nDiagnosis: "
         << result.diagnosis << endl;

    cout << "Reason: "
         << result.reason << endl;

    // Test 3: Missing disk time should not stop
    // the memory rule from being evaluated
    inputs.diskTime.available = false;

    if (processMetrics(inputs, scores, errorMessage))
    {
        cout << "\nMissing disk time test passed" << endl;
    }
    else
    {
        cout << "\nMissing disk time test failed: "
             << errorMessage << endl;
        return 1;
    }

    // Test 4: Remove required data from both rules
    inputs.pageFaults.available = false;
    inputs.cpu.available = false;

    if (!processMetrics(inputs, scores, errorMessage))
    {
        cout << "Insufficient data test passed" << endl;
        cout << "Message: " << errorMessage << endl;
    }
    else
    {
        cout << "Insufficient data test failed" << endl;
        return 1;
    }

    cout << "\nAll metric processor tests passed!" << endl;

    return 0;
}
