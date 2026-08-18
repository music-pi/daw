#pragma once

#include <juce_data_structures/juce_data_structures.h>

class AudioEngine;

class SetSwingCommand : public juce::UndoableAction
{
public:
    SetSwingCommand(AudioEngine& engine, double oldPercent, double newPercent);

    bool perform() override;
    bool undo() override;
    int  getSizeInUnits() override { return 1; }
    juce::String getUndoDescription() const { return "Set Swing"; }

    UndoableAction* createCoalescedAction(UndoableAction* nextAction) override;

private:
    AudioEngine& engine;
    const double oldPercent;
    const double newPercent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SetSwingCommand)
};
