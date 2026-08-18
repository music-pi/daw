#pragma once

#include <juce_data_structures/juce_data_structures.h>

class SamplerInstrument;

class SetChokeGroupCommand : public juce::UndoableAction
{
public:
    SetChokeGroupCommand(SamplerInstrument& sampler, int padId, int oldGroup, int newGroup);

    bool perform() override;
    bool undo() override;
    int getSizeInUnits() override { return 1; }
    juce::String getUndoDescription() const { return "Set Choke Group"; }

private:
    SamplerInstrument& sampler;
    const int padId;
    const int oldGroup;
    const int newGroup;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SetChokeGroupCommand)
};
