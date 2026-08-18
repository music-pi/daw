#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

class AudioEngine;

class SetPadInstrumentCommand : public juce::UndoableAction
{
public:
    SetPadInstrumentCommand(AudioEngine& engine,
                            int padIndex,
                            juce::PluginDescription desc);

    bool perform() override;
    bool undo() override;
    int getSizeInUnits() override { return 1; }
    juce::String getUndoDescription() const { return "Set Pad Instrument"; }

private:
    AudioEngine& engine_;
    const int padIndex_;
    const juce::PluginDescription desc_;
    te::Plugin::Ptr savedSamplerPlugin_;
    te::Plugin::Ptr savedRoundRobinPlugin_;
    te::Plugin::Ptr instrumentPlugin_;
    juce::ValueTree pluginParentState_;
    int samplerTreeIndex_ { -1 };
    int roundRobinTreeIndex_ { -1 };
    int instrumentTreeIndex_ { -1 };
    juce::String savedPadName_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SetPadInstrumentCommand)
};
