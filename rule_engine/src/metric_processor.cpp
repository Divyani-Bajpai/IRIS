
#include "metric_processor.h"
#include "deviation_calculator.h"

bool processMetrics(
    const MetricInputs &inputs, MetricScores &scores, const char *&errorMessage) {
    scores = MetricScores{};

    double result;

    if (inputs.pageFaults.available && deviationCalculation(inputs.pageFaults.current, inputs.pageFaults.average, inputs.pageFaults.sd, result))
    {
        scores.pageFaultsZ = result;
        scores.pageFaultsValid = true;
    }

    if (inputs.availableRam.available && deviationCalculation(inputs.availableRam.current, inputs.availableRam.average, inputs.availableRam.sd, result)) {
        scores.availableRamZ = result;
        scores.availableRamValid = true;
    }

    if (inputs.cpu.available && deviationCalculation(inputs.cpu.current, inputs.cpu.average, inputs.cpu.sd, result)) {
        scores.cpuZ = result;
        scores.cpuValid = true;
    }

    if (inputs.diskQueue.available && deviationCalculation(inputs.diskQueue.current, inputs.diskQueue.average, inputs.diskQueue.sd, result)) {
        scores.diskQueueZ = result;
        scores.diskQueueValid = true;
    }

    if (inputs.diskTime.available && deviationCalculation(inputs.diskTime.current, inputs.diskTime.average, inputs.diskTime.sd, result)) {
        scores.diskTimeZ = result;
        scores.diskTimeValid = true;
    }

    bool memoryInputsReady = scores.pageFaultsValid && scores.availableRamValid && scores.cpuValid;

    bool diskInputsReady = scores.diskQueueValid && scores.diskTimeValid && scores.availableRamValid;

    if (!memoryInputsReady && !diskInputsReady)
    {
        errorMessage = "Not enough valid metrics to evaluate rules";
        return false;
    }

    errorMessage = "";
    return true;
}
