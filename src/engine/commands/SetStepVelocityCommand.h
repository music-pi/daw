#pragma once

#include <juce_data_structures/juce_data_structures.h>

class AudioEngine;

class SetStepVelocityCommand : public juce::UndoableAction
{
public:
    SetStepVelocityCommand(AudioEngine& engine,
                           int patternIndex,
                           int padId,
                           int stepIndex,
                           int newVelocity);

    bool perform() override;
    bool undo() override;
    int getSizeInUnits() override { return 1; }
    juce::String getUndoDescription() const { return "Set Step Velocity"; }

private:
    AudioEngine& engine;
    const int patternIndex;
    const int padId;
    const int stepIndex;
    const int newVelocity;
    int oldVelocity { 0 };
    bool oldVelocityCaptured { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SetStepVelocityCommand)
};
