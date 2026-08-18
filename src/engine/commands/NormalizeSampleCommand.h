#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_core/juce_core.h>

class AudioEngine;

class NormalizeSampleCommand : public juce::UndoableAction
{
public:
    NormalizeSampleCommand(AudioEngine& engine, int padId, double targetDb);
    ~NormalizeSampleCommand() override = default;

    bool perform() override;
    bool undo() override;
    int getSizeInUnits() override { return 1; }
    juce::String getUndoDescription() const { return "Normalize Sample"; }

private:
    AudioEngine& engine;
    const int padId;
    const double targetDb;
    float previousGainDb { 0.0f };
    bool performed { false };
    bool hasCapturedState { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NormalizeSampleCommand)
};
