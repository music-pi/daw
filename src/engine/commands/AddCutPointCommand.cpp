#include "AddCutPointCommand.h"

#include <algorithm>
#include <cmath>

AddCutPointCommand::AddCutPointCommand(std::vector<double> cutPointsCopy,
                                       double cutPosition,
                                       double sampleRate,
                                       double totalSeconds,
                                       std::function<void(const std::vector<double>&)> onChange)
    : cutPoints(std::move(cutPointsCopy))
    , cutPosition(cutPosition)
    , sampleRate(sampleRate > 0.0 ? sampleRate : 44100.0)
    , totalSeconds(totalSeconds)
    , onChange(std::move(onChange))
{
}

bool AddCutPointCommand::perform()
{
    if (performed)
        return true;

    const double minSampleStep = 1.0 / sampleRate;

    // Clamp and round to sample boundary
    cutPosition = juce::jlimit(0.0, totalSeconds, cutPosition);
    cutPosition = std::round(cutPosition * sampleRate) / sampleRate;

    // Adjust if too close to existing cut points
    for (const double existingCut : cutPoints)
    {
        if (std::abs(cutPosition - existingCut) < minSampleStep)
        {
            if (cutPosition > existingCut)
                cutPosition = existingCut + minSampleStep;
            else
                cutPosition = existingCut - minSampleStep;
            cutPosition = juce::jlimit(0.0, totalSeconds, cutPosition);
            cutPosition = std::round(cutPosition * sampleRate) / sampleRate;
        }
    }

    // Add and sort
    cutPoints.push_back(cutPosition);
    std::sort(cutPoints.begin(), cutPoints.end());

    // Remove duplicates within sample precision
    cutPoints.erase(
        std::unique(cutPoints.begin(), cutPoints.end(),
            [minSampleStep](double a, double b) { return std::abs(a - b) < minSampleStep * 0.5; }),
        cutPoints.end());

    performed = true;

    if (onChange) onChange(cutPoints);

    return true;
}

bool AddCutPointCommand::undo()
{
    if (!performed)
        return false;

    const double minSampleStep = 1.0 / sampleRate;

    // Remove the cut point
    cutPoints.erase(
        std::remove_if(cutPoints.begin(), cutPoints.end(),
            [this, minSampleStep](double cp)
            {
                return std::abs(cp - cutPosition) < minSampleStep * 0.5;
            }),
        cutPoints.end());

    performed = false;

    if (onChange) onChange(cutPoints);

    return true;
}
