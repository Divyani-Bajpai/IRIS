#include <iostream>
#include "diagnostic_logger.h"

using namespace std;

void logDiagnosis(const RuleResult &result) {
    cout << "\n---Diagnostic Event---" << endl;
    cout << "Rule ID : " << result.ruleName << endl;
    cout << "Diagnosis : " << result.diagnosis << endl;
    cout << "Reason : " << result.reason << endl;
    if (!result.recommendationText.empty()) {
        cout << "Recommendation : " << result.recommendationText << endl;
    }
    if (!result.suggestions.empty()) {
        cout << "Suggestions :" << endl;
        for (size_t i = 0; i < result.suggestions.size(); ++i) {
            cout << "  " << (i + 1) << ". " << result.suggestions[i] << endl;
        }
    }
    cout << "Rule Matched : " << (result.matched ? "Yes" : "No") << endl;
}
