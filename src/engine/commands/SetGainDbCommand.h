#pragma once

#include <juce_data_structures/juce_data_structures.h>

class AudioEngine;
class SamplerInstrument;

class SetGainDbCommand : public juce::UndoableAction
{
public:
    SetGainDbCommand(SamplerInstrument& sampler, int padIndex, float oldDb, float newDb);
    SetGainDbCommand(AudioEngine& engine, juce::String channelId, float newDb);

    bool perform() override;
    bool undo() override;
    int getSizeInUnits() override { return 1; }
    juce::String getUndoDescription() const { return "Set Pad Gain"; }

private:
    bool apply(float db);

    SamplerInstrument* sampler { nullptr };
    AudioEngine* engine { nullptr };
    const int padIndex { -1 };
    const juce::String channelId;
    float oldDb { 0.0f };
    const float newDb;
    bool oldDbCaptured { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SetGainDbCommand)
};
