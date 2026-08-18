#pragma once

#include <juce_data_structures/juce_data_structures.h>

class AudioEngine;

class ToggleSequencerStepCommand : public juce::UndoableAction
{
public:
    ToggleSequencerStepCommand(AudioEngine& engine, int patternIndex, int padId, int stepIndex, bool newState);

    bool perform() override;
    bool undo() override;
    int getSizeInUnits() override { return 1; }
    juce::String getUndoDescription() const { return "Toggle Step"; }

private:
    AudioEngine& engine;
    const int patternIndex;
    const int padId;
    const int stepIndex;
    const bool newState;
    bool oldState { false };
    bool oldStateCaptured { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToggleSequencerStepCommand)
};
