#include <iostream>
#include "diagnostic_logger.h"

using namespace std;

void logDiagnosis(const RuleResult &result) {
    cout << "\n---Diagnostic Event---" << endl;
    cout << "Diagnosis : " << result.diagnosis << endl;
    cout << "Reason : " << result.reason << endl;
    cout << "Rule Matched : " << (result.matched ? "Yes" : "No") << endl;
}