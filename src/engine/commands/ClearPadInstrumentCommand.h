#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <tracktion_engine/tracktion_engine.h>

class AudioEngine;

class ClearPadInstrumentCommand : public juce::UndoableAction
{
public:
    ClearPadInstrumentCommand(AudioEngine& engine, int padIndex);

    bool perform() override;
    bool undo() override;
    int getSizeInUnits() override { return 1; }
    juce::String getUndoDescription() const { return "Clear Pad Instrument"; }

private:
    AudioEngine& engine_;
    const int padIndex_;
    tracktion::engine::Plugin::Ptr instrumentPlugin_;
    tracktion::engine::Plugin::Ptr samplerPlugin_;
    tracktion::engine::Plugin::Ptr roundRobinPlugin_;
    juce::ValueTree pluginParentState_;
    int instrumentTreeIndex_ { -1 };
    int samplerTreeIndex_ { -1 };
    int roundRobinTreeIndex_ { -1 };
    juce::String savedPadName_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClearPadInstrumentCommand)
};
