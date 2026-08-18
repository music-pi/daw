#pragma once

#include <juce_data_structures/juce_data_structures.h>

class AudioEngine;

class SetPanCommand : public juce::UndoableAction
{
public:
    SetPanCommand(AudioEngine& engine, juce::String channelId, float newPan);

    bool perform() override;
    bool undo() override;
    int getSizeInUnits() override { return 1; }
    juce::String getUndoDescription() const { return "Set Pan"; }

private:
    bool apply(float pan);

    AudioEngine& engine;
    const juce::String channelId;
    const float newPan;
    float oldPan { 0.0f };
    bool oldPanCaptured { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SetPanCommand)
};
